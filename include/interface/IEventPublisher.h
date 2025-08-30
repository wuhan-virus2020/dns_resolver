#pragma once

#include "Common.h"
#include <chrono>
#include <string>
#include <vector>

namespace leigod::dns {
    /**
     * DNS address changed event
     */
    struct DNSAddressEvent {
        std::string hostname;
        std::vector<std::string> old_addresses;
        std::vector<std::string> new_addresses;
        std::chrono::system_clock::time_point timestamp;
        std::string source;
        int64_t ttl;
        std::string record_type;
        bool is_authoritative;
    };

    class IEventPublisher {
    public:
        virtual ~IEventPublisher() = default;
        virtual void publishAddressChanged(const DNSAddressEvent &event) = 0;
        virtual void publishQueryStarted(const std::string &hostname) = 0;
        virtual void publishQueryCompleted(const std::string &hostname, const std::vector<std::string> &ips,
                                           bool success) = 0;
    };
}// namespace leigod::dns
