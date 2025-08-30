#include "CaresQueryStrategy.h"
#include <algorithm>
#include <mutex>
#include <ranges>
#include <vector>

namespace leigod::dns {

    void CaresQueryStrategy::initialize() {
        bool expected = false;
        if (!initialized_.compare_exchange_strong(expected, true)) {
            DNS_LOGGER_ERROR(logger_, "C-ares already initialized");
            return;
        }

        int status = ares_library_init(ARES_LIB_INIT_ALL);
        if (status != ARES_SUCCESS) {
            DNS_LOGGER_ERROR(logger_, "Failed to initialize c-ares library: {}", ares_strerror(status));
            return;
        }

        if (!ares_threadsafety()) {
            DNS_LOGGER_ERROR(logger_, "c-ares not compiled with thread support");
            return;
        }

        DNS_LOGGER_INFO(logger_, "c-ares library version: {}", ares_version(NULL));

        ares_options options{};
        int optmask = 0;

        // 设置c-ares选项
        memset(&options, 0, sizeof(options));
        options.flags = ARES_FLAG_NOCHECKRESP;
        options.timeout = config_.query_timeout_ms;
        options.tries = config_.retry.max_attempts;
        options.ndots = 1;
        optmask = ARES_OPT_FLAGS | ARES_OPT_TIMEOUT | ARES_OPT_TRIES | ARES_OPT_NDOTS;

        status = ares_init_options(&channel_, &options, optmask);
        if (status != ARES_SUCCESS) {
            DNS_LOGGER_ERROR(logger_, "Failed to set c-ares option: {}", ares_strerror(status));
            return;
        }

        // 初始化服务器健康状态
        std::lock_guard<std::mutex> lock(health_mutex_);
        for (const auto &server: config_.servers) {
            if (server.enabled) {
                server_health_[server.address] = ServerHealth{};
            }
        }

        initialized_ = true;
        DNS_LOGGER_INFO(logger_, "C-ares initialized successfully");
    }

    void CaresQueryStrategy::query(const std::string &hostname, DNSQueryCallback callback) {

        if (!initialized_) {
            DNS_LOGGER_ERROR(logger_, "C-ares not initialized, cannot query: {}", hostname);
            callback({.status = ARES_ENOTINITIALIZED});
            return;
        }

        // 创建查询上下文
        auto context = std::make_shared<QueryContext>();
        context->hostname = hostname;
        context->callback = callback;
        context->strategy = shared_from_this();
        context->start_time = std::chrono::steady_clock::now();

        // 选择最佳服务器
        std::string selected_server = selectServer();
        if (selected_server.empty()) {
            DNS_LOGGER_ERROR(logger_, "No healthy DNS servers available");
            callback({.status = ARES_ESERVFAIL});
            return;
        }

        // 设置查询参数
        struct ares_addrinfo_hints hints = {};
        hints.ai_family = config_.ipv6_enabled ? AF_UNSPEC : AF_INET;
        hints.ai_flags = ARES_AI_CANONNAME;

        {
            // 保存上下文
            std::lock_guard<std::mutex> lock(contexts_mutex_);
            active_contexts_.push_back(context);
        }

        // 开始查询计时
        auto query_metrics = std::make_shared<QueryMetrics>();
        query_metrics->start_time = std::chrono::steady_clock::now();

        // 执行查询
        ares_getaddrinfo(channel_, hostname.c_str(), nullptr, &hints, [](void *arg, int status, int timeouts, struct ares_addrinfo *result) {
            auto* ctx = static_cast<QueryContext*>(arg);
            if (ctx && ctx->strategy) {
                auto* strategy = reinterpret_cast<CaresQueryStrategy*>(ctx->strategy.get());
                strategy->handleResult(ctx, status, result);
            }
            if (result) {
                ares_freeaddrinfo(result);
            } }, context.get());
    }

    void CaresQueryStrategy::handleResult(QueryContext *context, int status, struct ares_addrinfo *result) {
        std::vector<std::string> ips;
        if (status == ARES_SUCCESS && result) {
            for (auto *node = result->nodes; node != nullptr; node = node->ai_next) {
                char ip[INET6_ADDRSTRLEN];
                const void *addr;

                if (node->ai_family == AF_INET) {
                    const auto *addr_in = reinterpret_cast<struct sockaddr_in *>(node->ai_addr);
                    addr = &(addr_in->sin_addr);
                } else if (node->ai_family == AF_INET6) {
                    const auto *addr_in6 = reinterpret_cast<struct sockaddr_in6 *>(node->ai_addr);
                    addr = &(addr_in6->sin6_addr);
                } else {
                    continue;
                }

                if (inet_ntop(node->ai_family, addr, ip, sizeof(ip))) {
                    ips.emplace_back(ip);
                }
            }
        }

        // 更新服务器性能指标
        auto query_end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(query_end - context->start_time);

        if (status == ARES_SUCCESS) {
            updateServerMetrics(selectServer(), latency);
        } else {
            DNS_LOGGER_DEBUG(logger_, "DNS query for {} failed: {}", context->hostname, ares_strerror(status));

            // 更新服务器健康状态
            auto selected_server = selectServer();
            {
                std::lock_guard<std::mutex> lock(health_mutex_);
                auto &health = server_health_[selected_server];
                health.error_count++;
                if (health.error_count > config_.server_error_threshold) {
                    health.healthy = false;
                    DNS_LOGGER_WARN(logger_, "Server {} marked as unhealthy", selected_server);
                }
            }
        }

        // 调用回调
        if (context->callback) {
            ResolveResult result_ = {
                    .status = status,
                    .hostname = context->hostname,
                    .ip_addresses = ips,
                    .resolution_time = latency.count(),
                    .error = ares_strerror(status),
                    .from_cache = false,
            };
            context->callback(result_);
        }

        // 标记为已完成
        context->completed = true;
    }

    void CaresQueryStrategy::processEvents() {
        if (!initialized_) return;

        fd_set readers, writers;
        int nfds = 0;
        struct timeval tv{};

        FD_ZERO(&readers);
        FD_ZERO(&writers);

        // 获取超时设置
        const struct timeval *tvp = ares_timeout(channel_, nullptr, &tv);

        nfds = ares_fds(channel_, &readers, &writers);

        if (nfds == 0) return;

        int select_result = select(nfds, &readers, &writers, nullptr, tvp);
        if (select_result < 0) {
            DNS_LOGGER_ERROR(logger_, "select() failed: {}", errno);
            return;
        }

        ares_process(channel_, &readers, &writers);

        // 清理已完成的上下文
        cleanupCompletedContexts();
    }

    void CaresQueryStrategy::shutdown() {
        bool expected = true;
        if (!initialized_.compare_exchange_strong(expected, false)) {
            DNS_LOGGER_ERROR(logger_, "C-ares shutdown already in progress");
            return;
        }

        // 取消所有未完成的查询
        ares_cancel(channel_);

        // 清理所有活动上下文
        {
            std::lock_guard<std::mutex> lock(contexts_mutex_);
            for (const auto &context: active_contexts_) {
                if (context->callback) {
                    context->callback({.status = ARES_ECANCELLED});
                }
            }
            active_contexts_.clear();
        }

        ares_destroy(channel_);
        ares_library_cleanup();
        initialized_ = false;
        DNS_LOGGER_INFO(logger_, "C-ares shutdown completed");
    }

    bool CaresQueryStrategy::isInitialized() const {
        return initialized_;
    }

    std::string CaresQueryStrategy::selectServer() {
        if (config_.servers.empty()) {
            return "";
        }

        std::vector<std::pair<std::string, double>> available_servers;
        {
            std::lock_guard<std::mutex> lock(health_mutex_);
            for (const auto &server: config_.servers) {
                if (server.enabled && server_health_.contains(server.address) && server_health_.at(server.address).healthy) {
                    // 计算服务器得分（基于延迟和权重）
                    double score = server.weight / (1.0 + server_health_.at(server.address).avg_latency.count());
                    available_servers.emplace_back(server.address, score);
                }
            }
        }

        if (available_servers.empty()) {
            // 如果没有健康的服务器，重置所有服务器状态
            std::lock_guard<std::mutex> lock(health_mutex_);
            for (auto &health: server_health_ | std::views::values) {
                health.healthy = true;
                health.error_count = 0;
            }
            return config_.servers.front().address;
        }

        // 根据得分选择服务器
        std::ranges::sort(available_servers,
                          [](const auto &a, const auto &b) { return a.second > b.second; });

        return available_servers.front().first;
    }

    bool CaresQueryStrategy::verifyServerHealth(const std::string &server) {
        // 执行健康检查查询
        // 这里可以实现实际的健康检查逻辑
        return true;
    }

    void CaresQueryStrategy::updateServerMetrics(const std::string &server, const std::chrono::milliseconds &latency) {
        uint64_t count = 0;
        std::chrono::milliseconds total{0};

        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            // 更新延迟历史
            auto &history = latency_history_[server];
            history.push(latency);
            if (history.size() > MAX_LATENCY_SAMPLES) {
                history.pop();
            }

            // 计算平均延迟
            count = history.size();
            auto temp = history;
            while (!temp.empty()) {
                total += temp.front();
                temp.pop();
            }
        }

        {
            // 更新服务器健康状态
            std::lock_guard<std::mutex> health_lock(health_mutex_);
            auto &health = server_health_[server];
            health.avg_latency = std::chrono::milliseconds(total.count() / count);
            health.last_check = std::chrono::steady_clock::now();
            health.error_count = 0;// 重置错误计数
            health.healthy = true;
        }
    }

    void CaresQueryStrategy::cleanupCompletedContexts() {
        // QueryContext中持有的callback会引用DNSResolver,
        // DNSResolver持有QueryStrategy，QueryStrategy持有QueryContext，形成循环引用
        // 因此，在清理完成上下文时，需要确保不会在回调中访问QueryStrategy
        std::vector<std::shared_ptr<QueryContext>> completed_contexts;
        {
            std::lock_guard<std::mutex> lock(contexts_mutex_);
            for (auto it = active_contexts_.begin(); it != active_contexts_.end();) {
                if ((*it)->completed.load()) {
                    completed_contexts.push_back(*it);
                    it = active_contexts_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

}// namespace leigod::dns