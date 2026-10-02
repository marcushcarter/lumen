#include <core/base/range_allocator.h>
#include <algorithm>
#include <cassert>
#include <cstddef>

namespace lumen {

void RangeAllocator::reset(uint32_t p_capacity)
{
    free_blocks.clear();
    if (p_capacity) free_blocks.push_back({ 0, p_capacity });
    capacity = p_capacity;
    used = 0;
}

void RangeAllocator::grow(uint32_t p_capacity)
{
    if (p_capacity <= capacity) return;
    const uint32_t added = p_capacity - capacity;
    if (!free_blocks.empty() && free_blocks.back().offset + free_blocks.back().size == capacity) free_blocks.back().size += added;
    else free_blocks.push_back({ capacity, added });
    capacity = p_capacity;
}

uint32_t RangeAllocator::allocate(uint32_t p_size)
{
    if (p_size == 0) return INVALID;

    // Best fit: keeps large holes (and the tail) intact, which keeps extent() low.
    size_t best = SIZE_MAX;
    uint32_t best_size = UINT32_MAX;
    for (size_t i = 0; i < free_blocks.size(); i++) {
        const uint32_t s = free_blocks[i].size;
        if (s < p_size || s >= best_size) continue;
        best = i;
        best_size = s;
        if (s == p_size) break;
    }
    if (best == SIZE_MAX) return INVALID;

    Block& b = free_blocks[best];
    const uint32_t offset = b.offset;
    b.offset += p_size;
    b.size -= p_size;
    if (b.size == 0) free_blocks.erase(free_blocks.begin() + (ptrdiff_t)best);
    used += p_size;
    return offset;
}

void RangeAllocator::free(uint32_t p_offset, uint32_t p_size)
{
    if (p_offset == INVALID || p_size == 0) return;
    assert(p_offset + p_size <= capacity && p_size <= used);

    auto it = std::lower_bound(free_blocks.begin(), free_blocks.end(), p_offset, [](const Block& b, uint32_t off) { return b.offset < off; });
    const bool merge_prev = it != free_blocks.begin() && std::prev(it)->offset + std::prev(it)->size == p_offset;
    const bool merge_next = it != free_blocks.end() && p_offset + p_size == it->offset;
    assert(it == free_blocks.begin() || std::prev(it)->offset + std::prev(it)->size <= p_offset);
    assert(it == free_blocks.end() || p_offset + p_size <= it->offset);

    if (merge_prev && merge_next) {
        std::prev(it)->size += p_size + it->size;
        free_blocks.erase(it);
    } else if (merge_prev) {
        std::prev(it)->size += p_size;
    } else if (merge_next) {
        it->offset = p_offset;
        it->size += p_size;
    } else {
        free_blocks.insert(it, { p_offset, p_size });
    }
    used -= p_size;
}

uint32_t RangeAllocator::extent() const
{
    return capacity - tail_free();
}

uint32_t RangeAllocator::tail_free() const
{
    if (free_blocks.empty()) return 0;
    const Block& b = free_blocks.back();
    return b.offset + b.size == capacity ? b.size : 0;
}

uint32_t RangeAllocator::largest_free() const
{
    uint32_t m = 0;
    for (const Block& b : free_blocks) m = std::max(m, b.size);
    return m;
}

}