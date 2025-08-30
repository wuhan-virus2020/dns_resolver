#include "PluginManager.h"

namespace leigod::dns {
    // 动态库插件加载
    bool PluginManager::loadPlugin(const std::string &path) {
        // 在实际实现中，这里会使用dlopen/dlsym或LoadLibrary/GetProcAddress
        // 加载动态库并注册其中的工厂函数
        DNS_LOGGER_INFO(logger_, "Plugin loading not implemented: {}", path);
        return false;
    }

    bool PluginManager::unloadPlugin(const std::string &name) {
        DNS_LOGGER_INFO(logger_, "Plugin unloading not implemented: {}", name);
        return false;
    }

    // 查询策略插件管理
    void PluginManager::registerQueryStrategyFactory(const std::string &name, const QueryStrategyFactory &factory) {
        std::lock_guard<std::mutex> lock(mutex_);
        queryStrategyFactories_[name] = factory;
        DNS_LOGGER_INFO(logger_, "Registered query strategy factory: {}", name);
    }

    std::shared_ptr<IDNSQueryStrategy> PluginManager::createQueryStrategy(
            const std::string &name, const DNSResolverConfig &config) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = queryStrategyFactories_.find(name);
        if (it == queryStrategyFactories_.end()) {
            DNS_LOGGER_ERROR(logger_, "Query strategy factory not found: {}", name);
            return nullptr;
        }

        try {
            auto strategy = it->second(config);
            DNS_LOGGER_INFO(logger_, "Created query strategy: {}", name);
            return strategy;
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Failed to create query strategy {}: {}", name, e.what());
            return nullptr;
        }
    }

    // 缓存插件管理
    void PluginManager::registerCacheFactory(const std::string &name, const CacheFactory &factory) {
        std::lock_guard<std::mutex> lock(mutex_);
        cacheFactories_[name] = factory;
        DNS_LOGGER_INFO(logger_, "Registered cache factory: {}", name);
    }

    std::shared_ptr<ICache> PluginManager::createCache(const std::string &name, const CacheConfig &config) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cacheFactories_.find(name);
        if (it == cacheFactories_.end()) {
            DNS_LOGGER_ERROR(logger_, "Cache factory not found: {}", name);
            return nullptr;
        }

        try {
            auto cache = it->second(config);
            DNS_LOGGER_INFO(logger_, "Created cache: {}", name);
            return cache;
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Failed to create cache {}: {}", name, e.what());
            return nullptr;
        }
    }

    // 获取可用插件列表
    std::vector<std::string> PluginManager::getAvailableQueryStrategies() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto &pair: queryStrategyFactories_) {
            names.push_back(pair.first);
        }
        return names;
    }

    std::vector<std::string> PluginManager::getAvailableCaches() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto &pair: cacheFactories_) {
            names.push_back(pair.first);
        }
        return names;
    }

    void PluginManager::setPluginConfig(const PluginConfig &config) {
        config_ = config;
    }

    void PluginManager::shutdown() {
    }

}// namespace leigod::dns