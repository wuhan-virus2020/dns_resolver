#pragma once

#include "interface/IDNSQueryStrategy.h"
#include "interface/ILogger.h"
#include <ares.h>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

namespace leigod::dns {

    class CaresQueryStrategy : public IDNSQueryStrategy, public std::enable_shared_from_this<CaresQueryStrategy> {
    public:
        CaresQueryStrategy(DNSResolverConfig config,
                           std::shared_ptr<ILogger> logger)
            : config_(std::move(config)), logger_(std::move(logger)) {
            initialize();
        }

        ~CaresQueryStrategy() override = default;

        // 实现 IDNSQueryStrategy 接口
        void query(const std::string &hostname, DNSQueryCallback callback) override;
        void processEvents() override;
        void shutdown() override;
        bool isInitialized() const override;

    private:
        struct QueryMetrics {
            std::chrono::steady_clock::time_point start_time;
            uint32_t retry_count{0};
            std::chrono::milliseconds total_time{0};
        };

        void initialize();
        void handleResult(QueryContext *context, int status, ares_addrinfo *result);
        void cleanupCompletedContexts();
        std::string selectServer();
        bool verifyServerHealth(const std::string &server);
        void updateServerMetrics(const std::string &server,
                                 const std::chrono::milliseconds &latency);

        // 配置和状态
        DNSResolverConfig config_;
        std::shared_ptr<ILogger> logger_;
        ares_channel channel_{nullptr};
        std::atomic<bool> initialized_{false};

        // 查询上下文管理
        std::vector<std::shared_ptr<QueryContext>> active_contexts_;
        std::mutex contexts_mutex_;

        // 服务器健康检查
        struct ServerHealth {
            bool healthy{true};
            std::chrono::steady_clock::time_point last_check;
            std::chrono::milliseconds avg_latency{0};
            uint32_t error_count{0};
        };
        std::map<std::string, ServerHealth> server_health_;
        std::mutex health_mutex_;

        // 性能指标
        std::map<std::string, std::queue<std::chrono::milliseconds>> latency_history_;
        static constexpr size_t MAX_LATENCY_SAMPLES = 100;
        std::mutex metrics_mutex_;
    };

}// namespace leigod::dns