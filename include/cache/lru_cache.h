#pragma once
#include <unordered_map>
#include <list>
#include <shared_mutex>
#include <optional>
#include "core/types.h"

namespace hp::cache {
struct Key {
    char pkg[64], scene[32];
    bool operator==(const Key& o) const {
        return std::string_view(pkg) == o.pkg && std::string_view(scene) == o.scene;
    }
};
struct Hash {
    size_t operator()(const Key& k) const noexcept {
        size_t h = 14695981039346656037ull;
        for(char c : k.pkg) { h ^= c; h *= 1099511628211ull; }
        for(char c : k.scene) { h ^= c; h *= 1099511628211ull; }
        return h;
    }
};

template<size_t Segs = 16>
class LRUCache {
    struct alignas(64) Seg {
        mutable std::shared_mutex mtx;
        std::list<Key> lru;
        std::unordered_map<Key, FreqConfig, Hash> map;
    };
    std::array<Seg, Segs> segs_;
    size_t max_per_;
    static size_t h(const Key& k) noexcept { return Hash{}(k) % Segs; }

public:
    explicit LRUCache(size_t total_max = 8192) : max_per_(total_max / Segs) {}
    
    std::optional<FreqConfig> get(const Key& k) noexcept {
        auto& s = segs_[h(k)];
        std::shared_lock lk(s.mtx);
        auto it = s.map.find(k);
        if(it == s.map.end()) return std::nullopt;
        lk.unlock();
        std::unique_lock ul(s.mtx);
        s.lru.erase(it->second.second);
        s.lru.push_front(k);
        it->second.second = s.lru.begin();
        return it->second;
    }

    void put(const Key& k, FreqConfig v) noexcept {
        auto& s = segs_[h(k)];
        std::unique_lock lk(s.mtx);
        auto it = s.map.find(k);
        if(it != s.map.end()) {
            s.lru.erase(it->second.second);
            it->second = v;
        } else {
            if(s.map.size() >= max_per_) {
                s.map.erase(s.lru.back());
                s.lru.pop_back();
            }
            s.lru.push_front(k);
            s.map.emplace(k, std::pair{v, s.lru.begin()});
        }
    }
};
} // namespace hp::cache