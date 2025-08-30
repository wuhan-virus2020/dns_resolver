#pragma once

#include <chrono>
#include <list>
#include <mutex>
#include <unordered_map>

#include "interface/ICache.h"

namespace leigod::dns {

    class LRUCache : public ICache {
    public:
        LRUCache(size_t max_size, int64_t ttl)
            : max_size_(max_size), ttl_(ttl), hits_(0), misses_(0) {}

        bool get(const std::string &hostname, std::vector<std::string> &ips) override;

        void update(const std::string &hostname, const std::vector<std::string> &ips) override;

        void remove(const std::string &hostname) override;

        void clear() override;

        size_t size() const override;

        double hit_rate() const override;

    private:
        struct CacheEntry {
            std::vector<std::string> ips;
            std::chrono::system_clock::time_point expire_time;
            std::list<std::string>::iterator lru_iterator;
        };

        void cleanup();

        void evict();

        size_t max_size_;
        std::chrono::milliseconds ttl_;
        mutable std::mutex mutex_;

        std::unordered_map<std::string, CacheEntry> cache_;
        std::list<std::string> lru_list_;// MRU at front, LRU at back

        // 统计信息
        size_t hits_;
        size_t misses_;
    };

}// namespace leigod::dns
