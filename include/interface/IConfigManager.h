#pragma once

#include "Common.h"
#include <functional>

namespace leigod::dns {
    struct DNSResolverConfig;

    /**
     * 配置管理接口
     */
    class IConfigManager {
    public:
        virtual ~IConfigManager() = default;
        virtual DNSResolverConfig getConfig() const = 0;
        virtual void updateConfig(const DNSResolverConfig &config) = 0;
        virtual void registerConfigChangeHandler(
                std::function<void(const DNSResolverConfig &)> handler) = 0;
    };
}// namespace leigod::dns
