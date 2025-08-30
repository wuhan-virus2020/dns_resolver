#include "BasicMetrics.h"
#include <algorithm>
#include <format>
#include <sstream>

namespace leigod::dns {

    void BasicMetrics::recordQuery(const std::string &hostname, int64_t duration, bool success) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);

            // 更新基本计数器
            ++total_queries_;
            if (success) {
                ++successful_queries_;
            } else {
                ++failed_queries_;
            }

            auto duration_ms = static_cast<double>(duration);

            // 更新查询时间统计
            query_stats_.update(duration_ms);

            // 更新域名级别统计
            auto &host_stats = hostname_stats_[hostname];
            host_stats.query_count++;
            host_stats.last_query_time = std::chrono::system_clock::now();
            host_stats.running_stats.update(duration_ms);

            // 保持最近样本数量
            query_durations_.push_back(duration_ms);
            if (query_durations_.size() > MAX_SAMPLES) {
                query_durations_.pop_front();
            }

            // 定期清理和更新
            cleanupOldStats();
            updatePerformanceMetrics();

            DNS_LOGGER_DEBUG(logger_, "Recorded query for {} - duration: {}ms, success: {}",
                             hostname, duration, success);
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error recording query: {}", e.what());
        }
    }

    void BasicMetrics::recordCacheHit(const std::string &hostname) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            ++cache_hits_;

            auto &host_stats = hostname_stats_[hostname];
            host_stats.cache_hits++;
            host_stats.last_cache_hit_time = std::chrono::system_clock::now();

            updatePerformanceMetrics();
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error recording cache hit: {}", e.what());
        }
    }

    void BasicMetrics::recordCacheMiss(const std::string &hostname) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            ++cache_misses_;

            auto &host_stats = hostname_stats_[hostname];
            host_stats.cache_misses++;
            host_stats.last_cache_miss_time = std::chrono::system_clock::now();

            updatePerformanceMetrics();
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error recording cache miss: {}", e.what());
        }
    }

    void BasicMetrics::recordServerLatency(const std::string &server, int64_t latency) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            auto svr_latency = static_cast<double>(latency);
            auto &server_stats = server_stats_[server];
            server_stats.avg_latency = svr_latency;
            server_stats.last_update_time = std::chrono::system_clock::now();

            auto &latencies = server_latencies_[server];
            latencies.push_back(svr_latency);
            if (latencies.size() > MAX_SAMPLES) {
                latencies.erase(latencies.begin());
            }

            if (latency > alert_thresholds_.max_latency.count()) {
                DNS_LOGGER_WARN(logger_, "Server {} latency ({} ms) exceeded threshold ({} ms)",
                                server, latency,
                                alert_thresholds_.max_latency.count());
            }
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error recording server latency: {}", e.what());
        }
    }

    void BasicMetrics::recordError(const std::string &type, const std::string &detail) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);

            auto &error_stats = error_stats_[type];
            error_stats.count++;
            error_stats.last_occurrence = std::chrono::system_clock::now();
            error_stats.last_detail = detail;

            updatePerformanceMetrics();

            if (current_performance_.error_rate > alert_thresholds_.max_error_rate) {
                DNS_LOGGER_WARN(logger_, "Error rate ({:.2f}%) exceeded threshold ({:.2f}%)",
                                current_performance_.error_rate * 100,
                                alert_thresholds_.max_error_rate * 100);
            }
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error recording error stats: {}", e.what());
        }
    }

    void BasicMetrics::recordRetry(const std::string &hostname, uint32_t attempt) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            ++total_retries_;

            auto &host_stats = hostname_stats_[hostname];
            host_stats.retry_count++;
            host_stats.last_retry_time = std::chrono::system_clock::now();

            retry_attempts_[hostname].push_back(attempt);

            if (retry_attempts_[hostname].size() > MAX_RETRY_HISTORY) {
                retry_attempts_[hostname].erase(
                        retry_attempts_[hostname].begin(),
                        retry_attempts_[hostname].begin() +
                                (retry_attempts_[hostname].size() - MAX_RETRY_HISTORY));
            }

            if (attempt > alert_thresholds_.max_retry_count) {
                DNS_LOGGER_WARN(logger_, "Hostname {} exceeded retry threshold: {} attempts",
                                hostname, attempt);
            }
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error recording retry: {}", e.what());
        }
    }

    IMetrics::Stats BasicMetrics::getStats() const {
        try {
            Stats stats;
            std::lock_guard<std::mutex> lock(mutex_);

            // 基本统计
            stats.total_queries = total_queries_;
            stats.successful_queries = successful_queries_;
            stats.failed_queries = failed_queries_;
            stats.cache_hits = cache_hits_;
            stats.cache_misses = cache_misses_;
            stats.total_retries = total_retries_;

            // 缓存命中率
            const double total = stats.cache_hits + stats.cache_misses;
            stats.cache_hit_rate = total > 0 ? static_cast<double>(stats.cache_hits) / total : 0;

            // 查询时间统计
            if (query_stats_.count() > 0) {
                stats.avg_query_time_ms = query_stats_.mean();
                stats.query_time_stddev_ms = query_stats_.stddev();
                stats.min_query_time_ms = static_cast<int>(query_stats_.min());
                stats.max_query_time_ms = static_cast<int>(query_stats_.max());
            }

            // 服务器延迟统计
            for (const auto &[server, server_stat]: server_stats_) {
                ServerStats lat_stats;
                lat_stats.avg_latency = server_stat.avg_latency;
                lat_stats.stddev = 0.0;// 需要计算标准差
                lat_stats.sample_count = server_latencies_.at(server).size();
                lat_stats.last_update_time = std::chrono::system_clock::now();
                stats.server_latencies[server] = server_stat.avg_latency;
            }

            // 复制其他统计数据
            stats.error_stats = error_stats_;
            stats.hostname_stats = hostname_stats_;
            stats.retry_attempts = retry_attempts_;

            return stats;
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error getting stats: {}", e.what());
            return Stats{};
        }
    }

    void BasicMetrics::resetStats() {
        try {
            std::lock_guard<std::mutex> lock(mutex_);

            // 重置所有计数器
            total_queries_ = 0;
            successful_queries_ = 0;
            failed_queries_ = 0;
            cache_hits_ = 0;
            cache_misses_ = 0;
            total_retries_ = 0;

            // 清空统计数据
            query_durations_.clear();
            error_stats_.clear();
            server_latencies_.clear();
            retry_attempts_.clear();
            hostname_stats_.clear();
            server_stats_.clear();

            // 重置运行时统计
            query_stats_ = RunningStats();

            // 重置性能指标
            current_performance_ = PerformanceMetrics{};
            last_performance_update_ = std::chrono::steady_clock::now();
            last_cleanup_ = std::chrono::steady_clock::now();

            DNS_LOGGER_INFO(logger_, "All metrics have been reset");
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error resetting stats: {}", e.what());
        }
    }

    std::string BasicMetrics::getPrometheusMetrics() const {
        try {
            std::stringstream ss;
            std::lock_guard<std::mutex> lock(mutex_);

            // 基本计数器
            ss << "# TYPE dns_total_queries counter\n"
               << "dns_total_queries " << total_queries_ << "\n"
               << "# TYPE dns_successful_queries counter\n"
               << "dns_successful_queries " << successful_queries_ << "\n"
               << "# TYPE dns_failed_queries counter\n"
               << "dns_failed_queries " << failed_queries_ << "\n"
               << "# TYPE dns_cache_hits counter\n"
               << "dns_cache_hits " << cache_hits_ << "\n"
               << "# TYPE dns_cache_misses counter\n"
               << "dns_cache_misses " << cache_misses_ << "\n"
               << "# TYPE dns_total_retries counter\n"
               << "dns_total_retries " << total_retries_ << "\n";

            // 查询时间直方图
            ss << "# TYPE dns_query_time_ms histogram\n";
            if (!query_durations_.empty()) {
                std::vector<double> sorted_durations(query_durations_.begin(),
                                                     query_durations_.end());
                std::sort(sorted_durations.begin(), sorted_durations.end());

                auto p50 = sorted_durations[sorted_durations.size() * 0.5];
                auto p90 = sorted_durations[sorted_durations.size() * 0.9];
                auto p99 = sorted_durations[sorted_durations.size() * 0.99];

                ss << "dns_query_time_ms{quantile=\"0.50\"} " << p50 << "\n"
                   << "dns_query_time_ms{quantile=\"0.90\"} " << p90 << "\n"
                   << "dns_query_time_ms{quantile=\"0.99\"} " << p99 << "\n"
                   << "dns_query_time_ms_count " << query_durations_.size() << "\n";
            }

            // 服务器延迟
            ss << "# TYPE dns_server_latency_ms gauge\n";
            for (const auto &[server, stats]: server_stats_) {
                ss << "dns_server_latency_ms{server=\"" << server
                   << "\",type=\"avg\"} " << stats.avg_latency << "\n";
            }

            // 错误统计
            ss << "# TYPE dns_errors counter\n";
            for (const auto &[type, stats]: error_stats_) {
                ss << "dns_errors{type=\"" << type << "\"} "
                   << stats.count << "\n";
            }

            return ss.str();
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error generating Prometheus metrics: {}", e.what());
            return "";
        }
    }

    BasicMetrics::PerformanceMetrics BasicMetrics::getPerformanceMetrics(std::chrono::seconds window) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        if (now - last_performance_update_ < PERFORMANCE_UPDATE_INTERVAL) {
            return current_performance_;
        }

        return current_performance_;
    }

    void BasicMetrics::setAlertThresholds(const AlertThresholds &thresholds) {
        std::lock_guard<std::mutex> lock(mutex_);
        alert_thresholds_ = thresholds;
        DNS_LOGGER_INFO(logger_, "Alert thresholds updated");
    }

    std::vector<std::string> BasicMetrics::checkAlertConditions() const {
        std::vector<std::string> alerts;

        // 检查错误率
        if (current_performance_.error_rate > alert_thresholds_.max_error_rate) {
            alerts.push_back(
                    std::format("Error rate {:.2f}% exceeded threshold {:.2f}%",
                                current_performance_.error_rate * 100,
                                alert_thresholds_.max_error_rate * 100));
        }

        // 检查缓存命中率
        if (current_performance_.cache_hit_rate < alert_thresholds_.min_cache_hit_rate) {
            alerts.push_back(
                    std::format("Cache hit rate {:.2f}% below threshold {:.2f}%",
                                current_performance_.cache_hit_rate * 100,
                                alert_thresholds_.min_cache_hit_rate * 100));
        }

        return alerts;
    }

    void BasicMetrics::updatePerformanceMetrics() {
        auto now = std::chrono::steady_clock::now();
        if (now - last_performance_update_ < PERFORMANCE_UPDATE_INTERVAL) {
            return;
        }

        auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(
                                 now - last_performance_update_)
                                 .count();

        if (time_diff > 0) {
            current_performance_.queries_per_second = total_queries_ * 1.0 / time_diff;
            const auto total = cache_hits_ + cache_misses_;
            current_performance_.cache_hit_rate = total > 0 ? cache_hits_ * 1.0 / total : 0;
            current_performance_.avg_response_time = query_stats_.mean();
            current_performance_.error_rate = total_queries_ > 0 ? failed_queries_ * 1.0 / total_queries_ : 0;
            current_performance_.measurement_time = std::chrono::system_clock::now();
        }

        last_performance_update_ = now;
    }

    void BasicMetrics::cleanupOldStats() {
        auto now = std::chrono::steady_clock::now();
        auto sys_now = std::chrono::system_clock::now();

        if (now - last_cleanup_ < CLEANUP_INTERVAL) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // 清理过期的主机名统计
        auto host_it = hostname_stats_.begin();
        while (host_it != hostname_stats_.end()) {
            if (sys_now - host_it->second.last_query_time > CLEANUP_INTERVAL) {
                host_it = hostname_stats_.erase(host_it);
            } else {
                ++host_it;
            }
        }

        // 清理过期的重试记录
        auto retry_it = retry_attempts_.begin();
        while (retry_it != retry_attempts_.end()) {
            if (!hostname_stats_.contains(retry_it->first)) {
                retry_it = retry_attempts_.erase(retry_it);
            } else {
                ++retry_it;
            }
        }

        // 清理过期的服务器统计
        auto server_it = server_stats_.begin();
        while (server_it != server_stats_.end()) {
            if (sys_now - server_it->second.last_update_time > CLEANUP_INTERVAL) {
                server_it = server_stats_.erase(server_it);
            } else {
                ++server_it;
            }
        }

        last_cleanup_ = now;
    }
}// namespace leigod::dns