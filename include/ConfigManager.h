#pragma once

#include "interface/IConfigManager.h"
#include "interface/ILogger.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

namespace leigod::dns {

    class ConfigManager : public IConfigManager {
    public:
        ConfigManager(std::shared_ptr<ILogger> logger)
            : logger_(logger), stopHotReload_(false) {}

        ~ConfigManager() override;

        DNSResolverConfig getConfig() const override;

        void updateConfig(const DNSResolverConfig &config) override;

        void registerConfigChangeHandler(
                std::function<void(const DNSResolverConfig &)> handler) override;

        bool loadFromFile(const std::string &filename);

        bool saveToFile(const std::string &filename) const;

        void enableHotReload(const std::string &filename,
                             std::chrono::milliseconds checkInterval = std::chrono::seconds(5));

        void disableHotReload();

    private:
        void checkForConfigChanges();

        void notifyConfigChange() const;

        std::shared_ptr<ILogger> logger_;
        mutable std::mutex mutex_;
        DNSResolverConfig config_;
        std::function<void(const DNSResolverConfig &)> changeHandler_;

        // 热重载相关
        std::string configFile_;
        std::chrono::milliseconds checkInterval_{1000};
        std::atomic<bool> stopHotReload_;
        std::thread hotReloadThread_;
        std::filesystem::file_time_type lastModTime_;
    };

}// namespace leigod::dns
