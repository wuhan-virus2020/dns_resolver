#pragma once

#include "Common.h"
#include <string>
#include <vector>

namespace leigod::dns {
    /**
     * DNS缓存接口
     */
    class ICache {
    public:
        virtual ~ICache() = default;
        virtual bool get(const std::string &hostname, std::vector<std::string> &ips) = 0;
        virtual void update(const std::string &hostname, const std::vector<std::string> &ips) = 0;
        virtual void remove(const std::string &hostname) = 0;
        virtual void clear() = 0;
        virtual size_t size() const = 0;
        virtual double hit_rate() const = 0;
    };
}// namespace leigod::dns
