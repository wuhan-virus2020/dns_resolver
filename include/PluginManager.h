#pragma once

#include "interface/ICache.h"
#include "interface/IDNSQueryStrategy.h"
#include "interface/ILogger.h"


#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <utility>

namespace leigod::dns {

    using CacheFactory = std::function<std::shared_ptr<ICache>(const CacheConfig &)>;
    using QueryStrategyFactory = std::function<std::shared_ptr<IDNSQueryStrategy>(const DNSResolverConfig &)>;

    class PluginManager {
    public:
        explicit PluginManager(std::shared_ptr<ILogger> logger) : logger_(std::move(logger)) {}

        // 动态库插件加载
        bool loadPlugin(const std::string &path);

        bool unloadPlugin(const std::string &name);

        // 查询策略插件管理
        void registerQueryStrategyFactory(const std::string &name, const QueryStrategyFactory &factory);

        std::shared_ptr<IDNSQueryStrategy> createQueryStrategy(
                const std::string &name, const DNSResolverConfig &config);

        // 缓存插件管理
        void registerCacheFactory(const std::string &name, const CacheFactory &factory);

        std::shared_ptr<ICache> createCache(const std::string &name, const CacheConfig &config);

        // 获取可用插件列表
        std::vector<std::string> getAvailableQueryStrategies() const;

        std::vector<std::string> getAvailableCaches() const;

        void setPluginConfig(const PluginConfig &config);

        void shutdown();

    private:
        std::shared_ptr<ILogger> logger_;
        mutable std::mutex mutex_;

        PluginConfig config_{};

        // 插件注册表
        std::map<std::string, QueryStrategyFactory> queryStrategyFactories_;
        std::map<std::string, CacheFactory> cacheFactories_;

        // 已加载插件
        std::map<std::string, void *> loadedPlugins_;
    };

}// namespace leigod::dns
