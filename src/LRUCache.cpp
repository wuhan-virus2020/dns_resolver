#include "LRUCache.h"

namespace leigod::dns {
    bool LRUCache::get(const std::string &hostname, std::vector<std::string> &ips) {
        std::lock_guard<std::mutex> lock(mutex_);
        cleanup();

        auto it = cache_.find(hostname);
        if (it == cache_.end()) {
            misses_++;
            return false;
        }

        auto &entry = it->second;
        const auto now = std::chrono::system_clock::now();

        if (now >= entry.expire_time) {
            // 条目已过期
            cache_.erase(it);
            lru_list_.erase(entry.lru_iterator);
            misses_++;
            return false;
        }

        // 更新LRU位置
        lru_list_.erase(entry.lru_iterator);
        lru_list_.push_front(hostname);
        entry.lru_iterator = lru_list_.begin();

        ips = entry.ips;
        hits_++;
        return true;
    }

    void LRUCache::update(const std::string &hostname, const std::vector<std::string> &ips) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cache_.find(hostname);
        if (it != cache_.end()) {
            // 更新现有条目
            auto &entry = it->second;
            lru_list_.erase(entry.lru_iterator);
            lru_list_.push_front(hostname);
            entry.lru_iterator = lru_list_.begin();
            entry.ips = ips;
            entry.expire_time = std::chrono::system_clock::now() + ttl_;
        } else {
            // 添加新条目
            if (cache_.size() >= max_size_) {
                evict();
            }

            CacheEntry entry;
            entry.ips = ips;
            entry.expire_time = std::chrono::system_clock::now() + ttl_;
            lru_list_.push_front(hostname);
            entry.lru_iterator = lru_list_.begin();

            cache_[hostname] = entry;
        }
    }

    void LRUCache::remove(const std::string &hostname) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(hostname);
        if (it != cache_.end()) {
            lru_list_.erase(it->second.lru_iterator);
            cache_.erase(it);
        }
    }

    void LRUCache::clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        lru_list_.clear();
        hits_ = 0;
        misses_ = 0;
    }

    size_t LRUCache::size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }

    double LRUCache::hit_rate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto total = hits_ + misses_;
        return total > 0 ? static_cast<double>(hits_) / total : 0.0;
    }

    void LRUCache::cleanup() {
        auto now = std::chrono::system_clock::now();
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now >= it->second.expire_time) {
                lru_list_.erase(it->second.lru_iterator);
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void LRUCache::evict() {
        if (lru_list_.empty()) return;

        const auto &lru_key = lru_list_.back();
        cache_.erase(lru_key);
        lru_list_.pop_back();
    }

}// namespace leigod::dns