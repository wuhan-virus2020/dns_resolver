
#include "EventPublisher.h"

namespace leigod::dns {
    void EventPublisher::publishAddressChanged(const DNSAddressEvent &event) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &handler: addressChangeHandlers_) {
            try {
                handler(event);
            } catch ([[maybe_unused]] const std::exception &e) {
                // 忽略处理程序异常
            }
        }
    }

    void EventPublisher::publishQueryStarted(const std::string &hostname) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &handler: queryStartHandlers_) {
            try {
                handler(hostname);
            } catch ([[maybe_unused]] const std::exception &e) {
                // 忽略处理程序异常
            }
        }
    }

    void EventPublisher::publishQueryCompleted(const std::string &hostname,
                                               const std::vector<std::string> &ips,
                                               bool success) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &handler: queryCompleteHandlers_) {
            try {
                handler(hostname, ips, success);
            } catch ([[maybe_unused]] const std::exception &e) {
                // 忽略处理程序异常
            }
        }
    }

    void EventPublisher::subscribeAddressChange(AddressChangeHandler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        addressChangeHandlers_.push_back(handler);
    }

    void EventPublisher::subscribeQueryStart(QueryStartHandler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        queryStartHandlers_.push_back(handler);
    }

    void EventPublisher::subscribeQueryComplete(QueryCompleteHandler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        queryCompleteHandlers_.push_back(handler);
    }

    void EventPublisher::unsubscribeAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        addressChangeHandlers_.clear();
        queryStartHandlers_.clear();
        queryCompleteHandlers_.clear();
    }

}// namespace leigod::dns