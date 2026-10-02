#pragma once
#include <cstdint>
#include <vector>

namespace lumen {

struct RangeAllocator
{
    static constexpr uint32_t INVALID = UINT32_MAX;

    struct Block
    {
        uint32_t offset;
        uint32_t size;
    };

    std::vector<Block> free_blocks;
    uint32_t capacity = 0;
    uint32_t used = 0;

    void reset(uint32_t p_capacity);
    void grow(uint32_t p_capacity);
    uint32_t allocate(uint32_t p_size);
    void free(uint32_t p_offset, uint32_t p_size);

    uint32_t extent() const;
    uint32_t tail_free() const;
    uint32_t largest_free() const;
};

}