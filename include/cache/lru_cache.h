#pragma once
#include <unordered_map>
#include <list>
#include <shared_mutex>
#include <optional>
#include <string_view>
#include <mutex>
#include <array>
#include "core/types.h"

namespace hp::cache {

struct Key {
    char pkg[64];
    char scene[32];
    
    bool operator==(const Key& o) const {
        return std::string_view(pkg) == o.pkg && 
               std::string_view(scene) == o.scene;
    }
};

struct Hash {
    size_t operator()(const Key& k) const noexcept {
        size_t h = 14695981039346656037ull;
        for (char c : k.pkg) { h ^= c; h *= 1099511628211ull; }
        for (char c : k.scene) { h ^= c; h *= 1099511628211ull; }
        return h;
    }
};

template<size_t Segs = 16>
class LRUCache {
    struct CacheEntry {
        FreqConfig config;
        std::list<Key>::iterator lru_it;
    };

    struct alignas(64) Segment {
        mutable std::shared_mutex mtx;
        std::list<Key> lru;
        std::unordered_map<Key, CacheEntry, Hash> map;
    };

    std::array<Segment, Segs> segments_;
    size_t max_per_segment_;

    static size_t segment_index(const Key& k) noexcept {
        return Hash{}(k) % Segs;
    }

public:
    explicit LRUCache(size_t total_max = 8192) 
        : max_per_segment_(total_max / Segs) {}

    std::optional<FreqConfig> get(const Key& k) noexcept {
        auto& seg = segments_[segment_index(k)];
        std::shared_lock lk(seg.mtx);
        
        auto it = seg.map.find(k);
        if (it == seg.map.end()) {
            return std::nullopt;
        }

        lk.unlock();
        std::unique_lock ulk(seg.mtx);
        seg.lru.erase(it->second.lru_it);
        seg.lru.push_front(k);
        it->second.lru_it = seg.lru.begin();
        
        return it->second.config;
    }

    void put(const Key& k, FreqConfig cfg) noexcept {
        auto& seg = segments_[segment_index(k)];
        std::unique_lock lk(seg.mtx);

        auto it = seg.map.find(k);
        if (it != seg.map.end()) {
            seg.lru.erase(it->second.lru_it);
            it->second.config = cfg;
        } else {
            if (seg.map.size() >= max_per_segment_) {
                const Key& evict_key = seg.lru.back();
                seg.map.erase(evict_key);
                seg.lru.pop_back();
            }
            seg.map.emplace(k, CacheEntry{cfg, seg.lru.end()});
            seg.lru.push_front(k);
            seg.map[k].lru_it = seg.lru.begin();
        }
    }
};

} // namespace hp::cache