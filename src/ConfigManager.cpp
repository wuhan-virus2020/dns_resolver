#include "ConfigManager.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace leigod::dns {

    ConfigManager::~ConfigManager() {
        disableHotReload();
    }

    DNSResolverConfig ConfigManager::getConfig() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    void ConfigManager::updateConfig(const DNSResolverConfig &config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        notifyConfigChange();
    }

    void ConfigManager::registerConfigChangeHandler(std::function<void(const DNSResolverConfig &)> handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        changeHandler_ = handler;
    }

    bool ConfigManager::loadFromFile(const std::string &filename) {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                DNS_LOGGER_ERROR(logger_, "Could not open config file: {}", filename);
                return false;
            }

            nlohmann::json configJson;
            file >> configJson;

            DNSResolverConfig newConfig;

            // 解析DNS服务器配置
            if (configJson.contains("servers")) {
                for (const auto &serverJson: configJson["servers"]) {
                    DNSServerConfig server;
                    server.address = serverJson["address"];
                    server.port = serverJson.value("port", 53);
                    server.weight = serverJson.value("weight", 1);
                    server.timeout_ms = serverJson.value("timeout_ms", 2000);
                    server.enabled = serverJson.value("enabled", true);
                    newConfig.servers.push_back(server);
                }
            }

            // 解析缓存配置
            if (configJson.contains("cache")) {
                const auto &cacheJson = configJson["cache"];
                newConfig.cache.enabled = cacheJson.value("enabled", true);
                newConfig.cache.ttl = cacheJson.value("ttl_seconds", 300 * 1000);
                newConfig.cache.max_size = cacheJson.value("max_size", 10000);
                newConfig.cache.persistent = cacheJson.value("persistent", false);
                newConfig.cache.cache_file = cacheJson.value("cache_file", "");
            }

            // 解析重试配置
            if (configJson.contains("retry")) {
                const auto &retryJson = configJson["retry"];
                newConfig.retry.max_attempts = retryJson.value("max_attempts", 3);
                newConfig.retry.base_delay_ms = retryJson.value("base_delay_ms", 100);
                newConfig.retry.max_delay_ms = retryJson.value("max_delay_ms", 1000);
            }

            // 解析监控配置
            if (configJson.contains("metrics")) {
                const auto &metricsJson = configJson["metrics"];
                newConfig.metrics.enabled = metricsJson.value("enabled", true);
                newConfig.metrics.metrics_file = metricsJson.value("file", "");
                newConfig.metrics.report_interval_sec = metricsJson.value("report_interval_sec", 60);
            }

            // 解析全局配置
            if (configJson.contains("global")) {
                const auto &globalJson = configJson["global"];
                newConfig.query_timeout_ms = globalJson.value("query_timeout_ms", 5000);
                newConfig.max_concurrent_queries = globalJson.value("max_concurrent_queries", 100);
                newConfig.ipv6_enabled = globalJson.value("ipv6_enabled", true);
            }

            std::lock_guard<std::mutex> lock(mutex_);
            config_ = newConfig;
            configFile_ = filename;
            lastModTime_ = std::filesystem::last_write_time(filename);

            DNS_LOGGER_INFO(logger_, "Configuration loaded from: {}", filename);
            return true;

        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error loading configuration: {}", std::string(e.what()));
            return false;
        }
    }

    bool ConfigManager::saveToFile(const std::string &filename) const {
        try {
            std::lock_guard<std::mutex> lock(mutex_);

            nlohmann::json configJson;

            // 保存DNS服务器配置
            nlohmann::json serversJson = nlohmann::json::array();
            for (const auto &server: config_.servers) {
                nlohmann::json serverJson;
                serverJson["address"] = server.address;
                serverJson["port"] = server.port;
                serverJson["weight"] = server.weight;
                serverJson["timeout_ms"] = server.timeout_ms;
                serverJson["enabled"] = server.enabled;
                serversJson.push_back(serverJson);
            }
            configJson["servers"] = serversJson;

            // 保存缓存配置
            nlohmann::json cacheJson;
            cacheJson["enabled"] = config_.cache.enabled;
            cacheJson["ttl_seconds"] = config_.cache.ttl;
            cacheJson["max_size"] = config_.cache.max_size;
            cacheJson["persistent"] = config_.cache.persistent;
            cacheJson["cache_file"] = config_.cache.cache_file;
            configJson["cache"] = cacheJson;

            // 保存重试配置
            nlohmann::json retryJson;
            retryJson["max_attempts"] = config_.retry.max_attempts;
            retryJson["base_delay_ms"] = config_.retry.base_delay_ms;
            retryJson["max_delay_ms"] = config_.retry.max_delay_ms;
            configJson["retry"] = retryJson;

            // 保存监控配置
            nlohmann::json metricsJson;
            metricsJson["enabled"] = config_.metrics.enabled;
            metricsJson["file"] = config_.metrics.metrics_file;
            metricsJson["report_interval_sec"] = config_.metrics.report_interval_sec;
            configJson["metrics"] = metricsJson;

            // 保存全局配置
            nlohmann::json globalJson;
            globalJson["query_timeout_ms"] = config_.query_timeout_ms;
            globalJson["max_concurrent_queries"] = config_.max_concurrent_queries;
            globalJson["ipv6_enabled"] = config_.ipv6_enabled;
            configJson["global"] = globalJson;

            // 添加元数据
            configJson["metadata"]["version"] = "1.0";

            // 写入文件
            std::ofstream file(filename);
            file << configJson.dump(4);

            DNS_LOGGER_INFO(logger_, "Configuration saved to: {}", filename);
            return true;

        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error saving configuration: {}", std::string(e.what()));
            return false;
        }
    }

    void ConfigManager::enableHotReload(const std::string &filename, std::chrono::milliseconds checkInterval) {
        disableHotReload();

        if (!loadFromFile(filename)) {
            DNS_LOGGER_ERROR(logger_, "Failed to load config file for hot reload: {}", filename);
            return;
        }

        configFile_ = filename;
        checkInterval_ = checkInterval;
        stopHotReload_ = false;

        hotReloadThread_ = std::thread([this]() {
            while (!stopHotReload_) {
                checkForConfigChanges();
                std::this_thread::sleep_for(checkInterval_);
            }
        });

        DNS_LOGGER_INFO(logger_, "Hot reload enabled for config file: {}", filename);
    }

    void ConfigManager::disableHotReload() {
        stopHotReload_ = true;
        if (hotReloadThread_.joinable()) {
            hotReloadThread_.join();
        }
        DNS_LOGGER_INFO(logger_, "Hot reload disabled");
    }

    void ConfigManager::checkForConfigChanges() {
        try {
            auto currentModTime = std::filesystem::last_write_time(configFile_);

            if (currentModTime != lastModTime_) {
                DNS_LOGGER_INFO(logger_, "Config file changed, reloading...");
                if (loadFromFile(configFile_)) {
                    notifyConfigChange();
                }
            }
        } catch (const std::exception &e) {
            DNS_LOGGER_ERROR(logger_, "Error checking for config changes: {}", std::string(e.what()));
        }
    }

    void ConfigManager::notifyConfigChange() const {
        if (changeHandler_) {
            try {
                changeHandler_(config_);
            } catch (const std::exception &e) {
                DNS_LOGGER_ERROR(logger_, "Error in config change handler: {}", std::string(e.what()));
            }
        }
    }

}// namespace leigod::dns