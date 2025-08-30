#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace leigod {
    namespace dns {
        // 解析结果回调
        struct ResolveResult {
            int status{};
            std::string hostname{};
            std::vector<std::string> ip_addresses{};
            int64_t resolution_time{};
            std::string error{};
            bool from_cache = false;
            NLOHMANN_DEFINE_TYPE_INTRUSIVE(ResolveResult, status, hostname, ip_addresses, resolution_time, error, from_cache)
        };

        struct PluginConfig {
            bool auto_load = false;
            std::string config_path;
            std::vector<std::string> allowed_plugins;
            int64_t reload_interval = 60 * 1000;// in milliseconds
            NLOHMANN_DEFINE_TYPE_INTRUSIVE(PluginConfig, auto_load, config_path, allowed_plugins, reload_interval)
        };

        using ResolveCallback = std::function<void(const ResolveResult &)>;

        struct DNSServerConfig {
            std::string address{};
            uint16_t port = 53;
            uint32_t weight = 1;
            uint32_t timeout_ms = 2000;
            bool enabled = true;
            NLOHMANN_DEFINE_TYPE_INTRUSIVE(DNSServerConfig, address, port, weight, timeout_ms, enabled)
        };

        struct CacheConfig {
            bool enabled = true;
            int64_t ttl = 300 * 1000;// in milliseconds
            size_t max_size = 10000;
            bool persistent = false;
            std::string cache_file{};
            NLOHMANN_DEFINE_TYPE_INTRUSIVE(CacheConfig, enabled, ttl, max_size, persistent, cache_file)
        };

        struct RetryConfig {
            uint32_t max_attempts = 3;
            uint32_t base_delay_ms = 100;
            uint32_t max_delay_ms = 1000;
            NLOHMANN_DEFINE_TYPE_INTRUSIVE(RetryConfig, max_attempts, base_delay_ms, max_delay_ms)
        };

        struct MetricsConfig {
            bool enabled = true;
            std::string metrics_file{};
            uint32_t report_interval_sec = 60;
            NLOHMANN_DEFINE_TYPE_INTRUSIVE(MetricsConfig, enabled, metrics_file, report_interval_sec)
        };

        struct DNSResolverConfig {
            std::vector<DNSServerConfig> servers{};
            CacheConfig cache;
            RetryConfig retry;
            MetricsConfig metrics;
            PluginConfig plugins;
            uint32_t query_timeout_ms = 5000;
            uint32_t max_concurrent_queries = 100;
            bool ipv6_enabled = false;
            uint32_t server_error_threshold = 10;

            NLOHMANN_DEFINE_TYPE_INTRUSIVE(DNSResolverConfig, servers, cache, retry, metrics, plugins, query_timeout_ms,
                                           max_concurrent_queries, ipv6_enabled, server_error_threshold)
        };

        class IDNSQueryStrategy;

        // 查询上下文
        struct QueryContext {
            std::string hostname;
            std::function<void(ResolveResult)> callback = nullptr;
            std::chrono::steady_clock::time_point start_time{};
            std::vector<std::string> old_addresses;
            std::shared_ptr<IDNSQueryStrategy> strategy;
            uint32_t retry_count{};
            std::atomic_bool completed = false;
        };

    }// namespace dns
}// namespace leigod
