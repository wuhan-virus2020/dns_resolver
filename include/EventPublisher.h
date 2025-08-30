#pragma once

#include "interface/IEventPublisher.h"
#include <functional>
#include <mutex>

namespace leigod::dns {

    class EventPublisher : public IEventPublisher {
    public:
        using AddressChangeHandler = std::function<void(const DNSAddressEvent &)>;
        using QueryStartHandler = std::function<void(const std::string &)>;
        using QueryCompleteHandler = std::function<void(const std::string &,
                                                        const std::vector<std::string> &,
                                                        bool)>;

        void publishAddressChanged(const DNSAddressEvent &event) override;

        void publishQueryStarted(const std::string &hostname) override;

        void publishQueryCompleted(const std::string &hostname,
                                   const std::vector<std::string> &ips,
                                   bool success) override;

        void subscribeAddressChange(AddressChangeHandler handler);

        void subscribeQueryStart(QueryStartHandler handler);

        void subscribeQueryComplete(QueryCompleteHandler handler);

        void unsubscribeAll();

    private:
        mutable std::mutex mutex_;
        std::vector<AddressChangeHandler> addressChangeHandlers_;
        std::vector<QueryStartHandler> queryStartHandlers_;
        std::vector<QueryCompleteHandler> queryCompleteHandlers_;
    };

}// namespace leigod::dns
