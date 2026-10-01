#pragma once
#include <cstdint>
#include <vector>
#include <cstring>

namespace lumen {

struct IdMap
{
    static constexpr uint32_t NONE = UINT32_MAX;
    static constexpr uint32_t MIN_CAPACITY = 64;

    std::vector<uint64_t> keys;
    std::vector<uint32_t> values;
    uint32_t count = 0;
    uint32_t mask = 0;

    void clear() {
        if (count) std::memset(values.data(), 0xFF, values.size() * sizeof(uint32_t));
        count = 0;
    }

    uint32_t get(uint64_t p_key) const {
        if (!count) return NONE;
        const uint64_t* k = keys.data();
        const uint32_t* v = values.data();
        for (uint32_t i = _slot(p_key); ; i = (i + 1) & mask) {
            if (v[i] == NONE) return NONE;
            if (k[i] == p_key) return v[i];
        }
    }

    bool contains(uint64_t p_key) const { return get(p_key) != NONE; }
    bool insert(uint64_t p_key, uint32_t p_value) { return _put(p_key, p_value, false); }
    void set(uint64_t p_key, uint32_t p_value) { _put(p_key, p_value, true); }

    void erase(uint64_t p_key) {
        if (!count) return;
        uint32_t i = _slot(p_key);
        for (;; i = (i + 1) & mask) {
            if (values[i] == NONE) return;
            if (keys[i] == p_key) break;
        }
        values[i] = NONE;
        count--;
        for (uint32_t j = (i + 1) & mask; values[j] != NONE; j = (j + 1) & mask) {
            const uint32_t home = _slot(keys[j]);
            const bool stays = (i <= j) ? (i < home && home <= j) : (i < home || home <= j);
            if (stays) continue;
            keys[i] = keys[j];
            values[i] = values[j];
            values[j] = NONE;
            i = j;
        }
    }

    uint32_t _slot(uint64_t p_key) const { return (uint32_t)((p_key * 0x9E3779B97F4A7C15ull) >> 32) & mask; }

    bool _put(uint64_t p_key, uint32_t p_value, bool p_overwrite) {
        if ((count + 1) * 2 > mask + 1) _grow();
        uint64_t* k = keys.data();
        uint32_t* v = values.data();
        for (uint32_t i = _slot(p_key); ; i = (i + 1) & mask) {
            if (v[i] == NONE) {
                k[i] = p_key;
                v[i] = p_value;
                count++;
                return true;
            }
            if (k[i] == p_key) {
                if (p_overwrite) v[i] = p_value;
                return false;
            }
        }
    }

    void _grow() {
        const uint32_t cap = values.empty() ? MIN_CAPACITY : (uint32_t)values.size() * 2;
        std::vector<uint64_t> old_keys = std::move(keys);
        std::vector<uint32_t> old_values = std::move(values);
        keys.assign(cap, 0);
        values.assign(cap, NONE);
        mask = cap - 1;
        count = 0;
        for (size_t i = 0; i < old_values.size(); i++) if (old_values[i] != NONE) _put(old_keys[i], old_values[i], true);
    }
};

}
