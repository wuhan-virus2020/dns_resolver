#pragma once

#include "Common.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace leigod::dns {
    // 运行时统计计算类
    class RunningStats {
    private:
        double count_ = 0;
        double mean_ = 0;
        double m2_ = 0;
        double min_ = std::numeric_limits<double>::max();
        double max_ = std::numeric_limits<double>::lowest();

    public:
        void update(double value) {
            count_++;
            double delta = value - mean_;
            mean_ += delta / count_;
            double delta2 = value - mean_;
            m2_ += delta * delta2;
            min_ = std::min(min_, value);
            max_ = std::max(max_, value);
        }

        double mean() const { return mean_; }
        double variance() const { return count_ > 1 ? m2_ / (count_ - 1) : 0; }
        double stddev() const { return std::sqrt(variance()); }
        size_t count() const { return static_cast<size_t>(count_); }
        double min() const { return min_; }
        double max() const { return max_; }

        void reset() {
            count_ = 0;
            mean_ = 0;
            m2_ = 0;
            min_ = std::numeric_limits<double>::max();
            max_ = std::numeric_limits<double>::lowest();
        }
    };

    /**
     * DNS解析器度量指标接口
     */
    class IMetrics {
    public:
        // 主机名统计信息
        struct HostStats {
            uint64_t query_count{0};
            uint64_t cache_hits{0};
            uint64_t cache_misses{0};
            uint64_t retry_count{0};
            std::chrono::system_clock::time_point last_query_time;
            std::chrono::system_clock::time_point last_cache_hit_time;
            std::chrono::system_clock::time_point last_cache_miss_time;
            std::chrono::system_clock::time_point last_retry_time;
            RunningStats running_stats{};
            double avg_resolution_time{};
        };

        // 服务器延迟统计信息
        struct ServerStats {
            double avg_latency{0.0};
            double stddev{0.0};
            double min_latency{0.0};
            double max_latency{0.0};
            size_t sample_count{0};
            RunningStats running_stats{};
            std::chrono::system_clock::time_point last_update_time;
        };

        // 错误统计信息
        struct ErrorStats {
            uint64_t count{0};
            std::chrono::system_clock::time_point last_occurrence;
            std::string last_detail;
        };

        // 综合统计信息
        struct Stats {
            uint64_t total_queries{0};
            uint64_t successful_queries{0};
            uint64_t failed_queries{0};
            uint64_t cache_hits{0};
            uint64_t cache_misses{0};
            uint64_t total_retries{0};
            double cache_hit_rate{0.0};
            double avg_query_time_ms{0.0};
            double query_time_stddev_ms{0.0};
            int min_query_time_ms{0};
            int max_query_time_ms{0};

            std::map<std::string, double> server_latencies;
            std::map<std::string, std::vector<uint32_t>> retry_attempts;
            std::map<std::string, ErrorStats> error_stats;
            std::map<std::string, HostStats> hostname_stats;
        };

        virtual ~IMetrics() = default;

        // 核心记录方法
        virtual void recordQuery(const std::string &hostname, int64_t duration, bool success) = 0;
        virtual void recordCacheHit(const std::string &hostname) = 0;
        virtual void recordCacheMiss(const std::string &hostname) = 0;
        virtual void recordServerLatency(const std::string &server, int64_t latency) = 0;
        virtual void recordError(const std::string &type, const std::string &detail) = 0;
        virtual void recordRetry(const std::string &hostname, uint32_t attempt) = 0;

        // 统计查询方法
        virtual Stats getStats() const = 0;
        virtual void resetStats() = 0;
    };

}// namespace leigod::dns
