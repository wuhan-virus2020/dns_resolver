#include "BasicMetrics.h"
#include "ConfigManager.h"
#include "DNSResolver.h"
#include "EventPublisher.h"
#include <chrono>
#include <iostream>
#include <thread>

#include "interface/ILogger.h"

#include <csignal>

#ifdef WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
struct SocketInit {
    SocketInit() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~SocketInit() {
        WSACleanup();
    }

} g_envInit;
#endif


namespace leigod {
    namespace dns {

        class ConsoleLogger : public ILogger {
        public:
            explicit ConsoleLogger(Level minLevel = Level::kINFO) : minLevel_(minLevel) {}

            void log(int level, const char *file, const char *func, int line, const std::string &message) const override {

                if (level < minLevel_) return;

                auto now = std::chrono::system_clock::now();
                auto now_time_t = std::chrono::system_clock::to_time_t(now);
                auto now_tm = std::localtime(&now_time_t);

                std::cout << "[" << std::put_time(now_tm, "%Y-%m-%d %H:%M:%S") << "] "
                          << levelToString(level) << ": " << message;

                if (file != nullptr) {
                    std::cout << " (" << file;
                    if (line > 0) {
                        std::cout << ":" << line;
                    }
                    std::cout << ")";
                }

                std::cout << std::endl;
            }

        private:
            Level minLevel_;

            static std::string levelToString(int level) {
                switch (level) {
                    case Level::kDEBUG:
                        return "DEBUG";
                    case Level::kINFO:
                        return "INFO";
                    case Level::kWARNING:
                        return "WARNING";
                    case Level::kERROR:
                        return "ERROR";
                    default:
                        return "UNKNOWN";
                }
            }
        };

    }// namespace dns
}// namespace leigod

using namespace leigod::dns;

// 事件循环
bool g_running = true;

int main(int argv, char *argc[]) {
    // 捕获ctrl+c信号
    signal(SIGINT, [](int) { g_running = false; });

    // 创建组件
    auto logger = std::make_shared<ConsoleLogger>(ILogger::Level::kDEBUG);
    auto configManager = std::make_shared<ConfigManager>(logger);
    auto eventPublisher = std::make_shared<EventPublisher>();
    auto metrics = std::make_shared<BasicMetrics>(logger);

    // 订阅事件
    eventPublisher->subscribeAddressChange([](const DNSAddressEvent &event) {
        std::cout << "Address changed for " << event.hostname << ": ";
        for (const auto &addr: event.old_addresses) std::cout << addr << " ";
        std::cout << "-> ";
        for (const auto &addr: event.new_addresses) std::cout << addr << " ";
        std::cout << std::endl;
    });

    // 加载配置
    if (!configManager->loadFromFile("dns_config.json")) {
        std::cerr << "Failed to load config, using defaults" << std::endl;

        // 使用默认配置
        DNSResolverConfig defaultConfig;
        defaultConfig.servers.push_back({"114.114.114.114", 53, 1, 2000, true});
        defaultConfig.servers.push_back({"8.8.8.8", 53, 1, 2000, true});
        defaultConfig.servers.push_back({"1.1.1.1", 53, 1, 2000, true});
        configManager->updateConfig(defaultConfig);
    }

    // 启用配置热重载
    configManager->enableHotReload("dns_config.json", std::chrono::seconds(5));

    // 创建DNS解析器
    auto resolver = std::make_shared<DNSResolver>(logger, configManager, metrics, eventPublisher);

    // 初始化
    if (!resolver->initialize()) {
        std::cerr << "Failed to initialize DNS resolver" << std::endl;
        return 1;
    }

    // 解析域名
    std::vector<std::string> domains = {
            "google.com", "github.com", "stackoverflow.com",
            "example.com", "wikipedia.org", "reddit.com"};

    for (const auto &domain: domains) {
        resolver->resolve(domain, [domain](const ResolveResult &result) {
            std::cout << domain << ": ";
            if (result.status == 0) {
                for (const auto &ip: result.ip_addresses) {
                    std::cout << ip << " ";
                }
                std::cout << "(" << result.resolution_time << "ms)";
            } else {
                std::cout << "Failed: " << result.error;
            }
            std::cout << std::endl;
        });
    }

    std::thread thr([&resolver]() {
        while (g_running) {
            resolver->processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    while (g_running) {
        // 每5秒打印一次统计信息
        static auto lastStatsTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - lastStatsTime > std::chrono::seconds(5)) {
            auto stats = metrics->getStats();
            std::cout << "=== Stats ===" << std::endl;
            std::cout << "Total queries: " << stats.total_queries << std::endl;
            std::cout << "Successful: " << stats.successful_queries << std::endl;
            std::cout << "Failed: " << stats.failed_queries << std::endl;
            std::cout << "Cache hits: " << stats.cache_hits << std::endl;
            std::cout << "Cache misses: " << stats.cache_misses << std::endl;
            std::cout << "Cache hit rate: " << (stats.cache_hit_rate * 100) << "%" << std::endl;
            std::cout << "Avg query time: " << stats.avg_query_time_ms << "ms" << std::endl;
            std::cout << "=============" << std::endl;

            lastStatsTime = now;

            for (const auto &domain: domains) {
                resolver->resolve(domain, [domain](const ResolveResult &result) {
                    std::cout << domain << ": ";
                    if (result.status == 0) {
                        for (const auto &ip: result.ip_addresses) {
                            std::cout << ip << " ";
                        }
                        std::cout << "(" << result.resolution_time << "ms)";
                    } else {
                        std::cout << "Failed: " << result.error;
                    }
                    std::cout << std::endl;
                });
            }
        }
    }

    // 关闭
    resolver->shutdown();
    configManager->disableHotReload();

    return 0;
}