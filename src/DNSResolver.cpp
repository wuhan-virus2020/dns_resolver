#include "DNSResolver.h"
#include "CaresQueryStrategy.h"
#include "LRUCache.h"
#include "PluginManager.h"

namespace leigod::dns {

    namespace {
        // 常量定义
        constexpr int MAX_HOSTNAME_LENGTH = 253;
        constexpr int MAX_LABEL_LENGTH = 63;
        constexpr auto CONTEXT_CLEANUP_INTERVAL = std::chrono::seconds(60);

        // 辅助函数
        bool isValidHostnameLabel(const std::string &label) {
            if (label.empty() || label.length() > MAX_LABEL_LENGTH) {
                return false;
            }

            if (!std::isalnum(label.front()) || !std::isalnum(label.back())) {
                return false;
            }

            return std::all_of(label.begin(), label.end(), [](char c) {
                return std::isalnum(c) || c == '-';
            });
        }

        bool isValidHostname(const std::string &hostname) {
            if (hostname.empty() || hostname.length() > MAX_HOSTNAME_LENGTH) {
                return false;
            }

            std::istringstream stream(hostname);
            std::string label;
            while (std::getline(stream, label, '.')) {
                if (!isValidHostnameLabel(label)) {
                    return false;
                }
            }
            return true;
        }

        bool validateConfig(const DNSResolverConfig &config) {
            // 验证服务器配置
            if (config.servers.empty()) {
                return false;
            }

            // 验证超时设置
            if (config.query_timeout_ms < 100 || config.query_timeout_ms > 30000) {
                return false;
            }

            // 验证重试配置
            if (config.retry.max_attempts < 1 ||
                config.retry.max_attempts > 10 ||
                config.retry.base_delay_ms < 10 ||
                config.retry.max_delay_ms < config.retry.base_delay_ms) {
                return false;
            }

            return true;
        }
    }// namespace

    bool DNSResolver::initialize() {
        bool expected = false;
        if (!initialized_.compare_exchange_strong(expected, true)) {
            DNS_LOGGER_WARN(logger_, "DNSResolver already initialized");
            return true;
        }

        try {
            auto config = configManager_->getConfig();

            // 验证配置
            if (!validateConfig(config)) {
                DNS_LOGGER_ERROR(logger_, "Invalid configuration");
                initialized_ = false;
                return false;
            }

            // 创建并初始化插件管理器
            pluginManager_ = std::make_shared<PluginManager>(logger_);

            // 设置插件配置
            PluginConfig pluginConfig;
            pluginConfig.auto_load = config.plugins.auto_load;
            pluginConfig.config_path = config.plugins.config_path;
            pluginConfig.allowed_plugins = config.plugins.allowed_plugins;
            pluginConfig.reload_interval = config.plugins.reload_interval;
            pluginManager_->setPluginConfig(pluginConfig);

            // 注册内置查询策略
            pluginManager_->registerQueryStrategyFactory("cares",
                                                         [this](const DNSResolverConfig &config) {
                                                             return std::make_shared<CaresQueryStrategy>(config, logger_);
                                                         });

            // 注册内置缓存
            pluginManager_->registerCacheFactory("lru",
                                                 [](const CacheConfig &config) {
                                                     return std::make_shared<LRUCache>(config.max_size, config.ttl);
                                                 });

            // 创建并设置活动插件
            activeQueryStrategy_ = pluginManager_->createQueryStrategy("cares", config);
            if (!activeQueryStrategy_) {
                DNS_LOGGER_ERROR(logger_, "Failed to create query strategy");
                initialized_ = false;
                return false;
            }

            activeCache_ = pluginManager_->createCache("lru", config.cache);
            if (!activeCache_) {
                DNS_LOGGER_ERROR(logger_, "Failed to create cache");
                initialized_ = false;
                return false;
            }
#if 0
            // 加载自定义插件
            if (config.plugins.auto_load) {
                pluginManager_->loadPluginsFromDirectory(config.plugins.config_path);
            }

            // 启用插件热重载
            if (config.plugins.hot_reload_enabled) {
                pluginManager_->enableHotReload();
            }
#endif
            // 注册配置变更处理器
            configManager_->registerConfigChangeHandler(
                    [this](const DNSResolverConfig &config) {
                        handleConfigChange(config);
                    });
            DNS_LOGGER_INFO(logger_, "DNSResolver initialized successfully");
            return true;

        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Failed to initialize DNSResolver: {}", e.what());
            initialized_ = false;
            return false;
        }
    }

    void DNSResolver::resolve(const std::string &hostname, const ResolveCallback &callback) {
        if (!initialized_) {
            ResolveResult result;
            result.status = ARES_ENOTINITIALIZED;
            result.hostname = hostname;
            result.resolution_time = 0;
            result.error = ares_strerror(result.status);
            callback(result);
            return;
        }

        // 验证主机名
        if (!isValidHostname(hostname)) {
            ResolveResult result;
            result.status = ARES_EBADNAME;
            result.hostname = hostname;
            result.resolution_time = 0;
            result.error = ares_strerror(result.status);
            callback(result);
            return;
        }

        // 检查并发限制
        {
            std::lock_guard<std::mutex> lock(contexts_mutex_);
            if (active_contexts_.size() >= configManager_->getConfig().max_concurrent_queries) {
                ResolveResult result;
                result.status = ARES_EOF;
                result.hostname = hostname;
                result.resolution_time = 0;
                result.error = ares_strerror(result.status);
                callback(result);
                return;
            }
        }

        auto start_time = std::chrono::steady_clock::now();

        // 发布查询开始事件
        if (eventPublisher_) {
            eventPublisher_->publishQueryStarted(hostname);
        }

        // 检查缓存
        std::vector<std::string> cached_ips;
        bool cache_hit = false;

        if (activeCache_) {
            cache_hit = activeCache_->get(hostname, cached_ips);
        }

        if (cache_hit) {
            // 缓存命中
            if (metrics_) {
                metrics_->recordCacheHit(hostname);
            }

            ResolveResult result;
            result.status = ARES_SUCCESS;
            result.hostname = hostname;
            result.ip_addresses = cached_ips;
            result.resolution_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - start_time)
                                             .count();
            result.from_cache = true;

            callback(result);

            // 发布查询完成事件
            if (eventPublisher_) {
                eventPublisher_->publishQueryCompleted(hostname, cached_ips, true);
            }

            return;
        }

        // 缓存未命中，执行DNS查询
        if (metrics_) {
            metrics_->recordCacheMiss(hostname);
        }

        // 执行查询
        if (activeQueryStrategy_) {
            auto self = shared_from_this();
            activeQueryStrategy_->query(hostname,
                                        [self, callback](const ResolveResult &result) {
                                            self->handleQueryResult(0, result, callback);
                                        });
        } else {
            ResolveResult result;
            result.status = ARES_ENODATA;
            result.hostname = hostname;
            result.resolution_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - start_time)
                                             .count();
            result.error = ares_strerror(result.status);
            callback(result);
        }
    }

    void DNSResolver::handleQueryResult(int retry_count, ResolveResult result, const ResolveCallback &callback) {
        std::vector<std::string> old_addresses;
        // 保存旧地址用于检测变化
        if (activeCache_) {
            activeCache_->get(result.hostname, old_addresses);
        }

        // 记录指标
        if (metrics_) {
            metrics_->recordQuery(result.hostname, result.resolution_time, result.status == ARES_SUCCESS);

            if (result.status != ARES_SUCCESS) {
                metrics_->recordError("resolution_failure", ares_strerror(result.status));
            }
        }

        if (result.status == ARES_SUCCESS && !result.ip_addresses.empty()) {
            // 更新缓存
            if (activeCache_) {
                activeCache_->update(result.hostname, result.ip_addresses);
            }

            // 检查地址是否发生变化
            if (old_addresses != result.ip_addresses) {
                notifyAddressChange(result.hostname, old_addresses, result.ip_addresses);
            }
        } else if (result.status != ARES_SUCCESS && result.status != ARES_ENODATA && result.status != ARES_ENOTFOUND) {
            // 实施重试策略
            auto config = configManager_->getConfig();
            if (retry_count < config.retry.max_attempts) {
                retry_count++;

                if (metrics_) {
                    metrics_->recordRetry(result.hostname, retry_count);
                }

                // 使用指数退避
                auto delay = config.retry.base_delay_ms * (1 << (retry_count - 1));
                delay = std::min(delay, config.retry.max_delay_ms);

                std::this_thread::sleep_for(std::chrono::milliseconds(delay));

                if (activeQueryStrategy_) {
                    auto self = shared_from_this();
                    activeQueryStrategy_->query(result.hostname, [self, retry_count, callback](const ResolveResult &result_) {
                        self->handleQueryResult(retry_count, result_, callback);
                    });
                }
                return;
            }
        }

        // 调用回调
        if (callback) {
            callback(result);
        }

        // 发布查询完成事件
        if (eventPublisher_) {
            eventPublisher_->publishQueryCompleted(result.hostname, result.ip_addresses, result.status == ARES_SUCCESS);
        }
    }

    void DNSResolver::processEvents() {
        if (!initialized_) return;

        // 处理查询策略事件
        if (activeQueryStrategy_) {
            activeQueryStrategy_->processEvents();
        }
    }

    void DNSResolver::shutdown() {
        bool expected = true;
        if (!initialized_.compare_exchange_strong(expected, false)) {
            DNS_LOGGER_WARN(logger_, "DNSResolver is already shutting down");
            return;
        }

        DNS_LOGGER_INFO(logger_, "Shutting down DNSResolver");

        // 关闭查询策略
        if (activeQueryStrategy_) {
            activeQueryStrategy_->shutdown();
        }

        // 关闭插件管理器
        if (pluginManager_) {
            pluginManager_->shutdown();
        }

        // 保存缓存（如果配置了持久化）
        if (activeCache_ && configManager_->getConfig().cache.persistent) {
            // TODO: 实现缓存持久化
        }

        initialized_ = false;
        DNS_LOGGER_INFO(logger_, "DNSResolver shutdown completed");
    }

    void DNSResolver::handleConfigChange(const DNSResolverConfig &config) {
        std::lock_guard<std::mutex> lock(mutex_);
        DNS_LOGGER_INFO(logger_, "Applying configuration changes");

        try {
            // 验证新配置
            if (!validateConfig(config)) {
                DNS_LOGGER_ERROR(logger_, "Invalid configuration update");
                return;
            }
#if 0
            // 更新查询策略配置
            if (activeQueryStrategy_) {
                activeQueryStrategy_->updateConfig(config);
            }

            // 更新缓存配置
            if (activeCache_) {
                activeCache_->updateConfig(config.cache);
            }
#endif
            // 更新插件配置
            if (pluginManager_) {
                pluginManager_->setPluginConfig(config.plugins);
            }

            DNS_LOGGER_INFO(logger_, "Configuration update completed successfully");
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error applying configuration changes: {}",
                             e.what());
        }
    }

    void DNSResolver::notifyAddressChange(const std::string &hostname,
                                          const std::vector<std::string> &old_addresses,
                                          const std::vector<std::string> &new_addresses) {
        if (!eventPublisher_) return;

        DNSAddressEvent event;
        event.hostname = hostname;
        event.old_addresses = old_addresses;
        event.new_addresses = new_addresses;
        event.timestamp = std::chrono::system_clock::now();
        event.source = "dns_resolver";
        event.ttl = configManager_->getConfig().cache.ttl;
        event.record_type = new_addresses[0].find(':') != std::string::npos ? "AAAA" : "A";
        event.is_authoritative = false;

        eventPublisher_->publishAddressChanged(event);
    }

    // 配置管理
    void DNSResolver::updateConfig(const DNSResolverConfig &config) {
#if 0
        configManager_->setConfig(config);
#endif
        handleConfigChange(config);
    }

    DNSResolverConfig DNSResolver::getConfig() const {
        return configManager_->getConfig();
    }

    // 组件访问
    std::shared_ptr<ICache> DNSResolver::getCache() const {
        return activeCache_;
    }

    std::shared_ptr<IMetrics> DNSResolver::getMetrics() const {
        return metrics_;
    }

    std::shared_ptr<ILogger> DNSResolver::getLogger() const {
        return logger_;
    }

    std::shared_ptr<IEventPublisher> DNSResolver::getEventPublisher() const {
        return eventPublisher_;
    }

}// namespace leigod::dns