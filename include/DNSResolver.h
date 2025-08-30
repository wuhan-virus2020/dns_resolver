#pragma once

#include "ConfigManager.h"
#include "PluginManager.h"
#include "interface/ICache.h"
#include "interface/IDNSQueryStrategy.h"
#include "interface/IEventPublisher.h"
#include "interface/ILogger.h"
#include "interface/IMetrics.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace leigod::dns {

    class DNSResolver : public std::enable_shared_from_this<DNSResolver> {
    public:
        using ResolveCallback = std::function<void(ResolveResult)>;

        DNSResolver(std::shared_ptr<ILogger> logger,
                    std::shared_ptr<ConfigManager> configManager,
                    std::shared_ptr<IMetrics> metrics = nullptr,
                    std::shared_ptr<IEventPublisher> eventPublisher = nullptr)
            : logger_(std::move(logger)),
              configManager_(std::move(configManager)),
              metrics_(std::move(metrics)),
              eventPublisher_(std::move(eventPublisher)) {}

        ~DNSResolver() {
            shutdown();
        }

        // 初始化和关闭
        bool initialize();
        void shutdown();

        // DNS 解析
        void resolve(const std::string &hostname, const ResolveCallback &callback);
        void processEvents();

        // 配置管理
        void updateConfig(const DNSResolverConfig &config);
        DNSResolverConfig getConfig() const;

        // 组件访问
        std::shared_ptr<ICache> getCache() const;
        std::shared_ptr<IMetrics> getMetrics() const;
        std::shared_ptr<ILogger> getLogger() const;
        std::shared_ptr<IEventPublisher> getEventPublisher() const;

    private:
        // 内部方法
        void handleQueryResult(int retry_count, ResolveResult result, const ResolveCallback &callback);
        void handleConfigChange(const DNSResolverConfig &config);
        void notifyAddressChange(const std::string &hostname,
                                 const std::vector<std::string> &old_addresses,
                                 const std::vector<std::string> &new_addresses);

        // 核心组件
        std::shared_ptr<ILogger> logger_;
        std::shared_ptr<ConfigManager> configManager_;
        std::shared_ptr<IMetrics> metrics_;
        std::shared_ptr<IEventPublisher> eventPublisher_;
        std::shared_ptr<PluginManager> pluginManager_;

        // 活动策略和缓存
        std::shared_ptr<IDNSQueryStrategy> activeQueryStrategy_;
        std::shared_ptr<ICache> activeCache_;

        // 活动查询上下文管理
        std::vector<std::shared_ptr<QueryContext>> active_contexts_;
        mutable std::mutex contexts_mutex_;
        mutable std::mutex mutex_;

        // 状态标志
        std::atomic<bool> initialized_{false};
    };

}// namespace leigod::dns