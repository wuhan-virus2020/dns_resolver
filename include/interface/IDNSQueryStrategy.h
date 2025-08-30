#pragma once

#include "Common.h"
#include <functional>
#include <string>

namespace leigod::dns {
    class IDNSQueryStrategy {
    public:
        using DNSQueryCallback = std::function<void(ResolveResult)>;
        virtual ~IDNSQueryStrategy() = default;
        virtual void query(const std::string &hostname, DNSQueryCallback callback) = 0;
        virtual void processEvents() = 0;
        virtual void shutdown() = 0;
        virtual bool isInitialized() const = 0;
    };
}// namespace leigod::dns
