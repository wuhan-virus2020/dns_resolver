#pragma once

#include "interface/ILogger.h"
#include "interface/IMetrics.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace leigod::dns {
    class BasicMetrics : public IMetrics {
    public:
        explicit BasicMetrics(std::shared_ptr<ILogger> logger)
            : logger_(std::move(logger)),
              last_cleanup_(std::chrono::steady_clock::now()),
              last_performance_update_(std::chrono::steady_clock::now()) {}

        ~BasicMetrics() override = default;

        // IMetrics 接口实现
        void recordQuery(const std::string &hostname, int64_t duration, bool success) override;
        void recordCacheHit(const std::string &hostname) override;
        void recordCacheMiss(const std::string &hostname) override;
        void recordError(const std::string &type, const std::string &detail) override;
        void recordRetry(const std::string &hostname, uint32_t attempt) override;
        Stats getStats() const override;
        void resetStats() override;

        // 扩展功能
        void recordServerLatency(const std::string &server, int64_t latency);
        std::string getPrometheusMetrics() const;

        // 性能指标结构
        struct PerformanceMetrics {
            double queries_per_second{0.0};
            double cache_hit_rate{0.0};
            double avg_response_time{0.0};
            double error_rate{0.0};
            std::chrono::system_clock::time_point measurement_time;
        };

        // 告警阈值结构
        struct AlertThresholds {
            double max_error_rate{0.1};                 // 10%
            double min_cache_hit_rate{0.5};             // 50%
            std::chrono::milliseconds max_latency{1000};// 1秒
            uint32_t max_retry_count{3};
        };

        // 获取性能指标
        PerformanceMetrics getPerformanceMetrics(std::chrono::seconds window = std::chrono::seconds(60)) const;

        // 设置告警阈值
        void setAlertThresholds(const AlertThresholds &thresholds);

        // 检查告警条件
        std::vector<std::string> checkAlertConditions() const;

    private:
        // 内部方法
        void cleanupOldStats();
        void updatePerformanceMetrics();

        // 基础计数器
        std::atomic<uint64_t> total_queries_{0};
        std::atomic<uint64_t> successful_queries_{0};
        std::atomic<uint64_t> failed_queries_{0};
        std::atomic<uint64_t> cache_hits_{0};
        std::atomic<uint64_t> cache_misses_{0};
        std::atomic<uint64_t> total_retries_{0};

        // 运行时统计
        RunningStats query_stats_;

        // 详细统计数据
        std::map<std::string, HostStats> hostname_stats_;
        std::map<std::string, ServerStats> server_stats_;
        std::map<std::string, std::vector<double>> server_latencies_;
        std::map<std::string, ErrorStats> error_stats_;
        std::map<std::string, std::vector<uint32_t>> retry_attempts_;
        std::deque<double> query_durations_;

        // 性能指标和配置
        mutable PerformanceMetrics current_performance_;
        AlertThresholds alert_thresholds_;

        // 时间戳
        std::chrono::steady_clock::time_point last_cleanup_;
        std::chrono::steady_clock::time_point last_performance_update_;

        // 互斥锁
        mutable std::mutex mutex_;

        // 日志记录器
        std::shared_ptr<ILogger> logger_;

        // 配置常量
        static constexpr size_t MAX_SAMPLES = 1000;
        static constexpr size_t MAX_RETRY_HISTORY = 100;
        static constexpr auto PERFORMANCE_UPDATE_INTERVAL = std::chrono::minutes(1);
        static constexpr auto CLEANUP_INTERVAL = std::chrono::hours(1);

    public:
        // 禁用拷贝和赋值
        BasicMetrics(const BasicMetrics &) = delete;
        BasicMetrics &operator=(const BasicMetrics &) = delete;
    };

}// namespace leigod::dns