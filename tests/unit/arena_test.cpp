/** @file arena_test.cpp
 *  @brief Arena 的边界、跨块数据完整性与重复帧复用测试。
 */
#include "softrenderer/core/arena.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <limits>

using namespace sr;

namespace sr::tests
{
TEST(ArenaSafety, alloc_zeroInitialCapacity_canGrow)
{
    ArenaAllocator arena(0);
    ASSERT_NE(arena.alloc(64), nullptr);
    EXPECT_GE(arena.used(), 64u);
}

TEST(ArenaSafety, alloc_invalidAlignment_preservesState)
{
    ArenaAllocator arena(64);
    arena.alloc(4);
    const auto before = arena.used();
    EXPECT_EQ(arena.alloc(1, 0), nullptr);
    EXPECT_EQ(arena.alloc(1, 3), nullptr);
    EXPECT_EQ(arena.used(), before);
}

TEST(ArenaSafety, alloc_overflowingSize_preservesState)
{
    ArenaAllocator arena(64);
    EXPECT_EQ(arena.alloc(std::numeric_limits<size_t>::max(), 16), nullptr);
    EXPECT_EQ(arena.used(), 0u);
    ASSERT_NE(arena.alloc(8), nullptr);
}

TEST(ArenaSafety, allocArray_overflowingCount_returnsNull)
{
    ArenaAllocator arena(64);
    EXPECT_EQ(arena.allocArray<uint64_t>(std::numeric_limits<size_t>::max() / sizeof(uint64_t) + 1), nullptr);
    EXPECT_EQ(arena.used(), 0u);
}

TEST(ArenaSafety, alloc_growthAndOverAlignment_preservesPreviousData)
{
    ArenaAllocator arena(32);
    auto* first = arena.allocArray<uint8_t>(24);
    ASSERT_NE(first, nullptr);
    std::fill_n(first, 24, uint8_t{0x5a});
    auto* second = static_cast<uint8_t*>(arena.alloc(512, 256));
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(second) % 256, 0u);
    std::fill_n(second, 512, uint8_t{0xa5});
    for (size_t i = 0; i < 24; ++i)
        EXPECT_EQ(first[i], 0x5a);
}

TEST(ArenaSafety, reset_repeatedFrames_reusesAllBlocks)
{
    ArenaAllocator arena(32);
    void* first = arena.alloc(24);
    void* second = arena.alloc(512, 64);
    ASSERT_NE(second, nullptr);
    const auto capacity = arena.capacity();
    for (int frame = 0; frame < 100; ++frame)
    {
        arena.reset();
        EXPECT_EQ(arena.used(), 0u);
        EXPECT_EQ(arena.capacity(), capacity);
        EXPECT_EQ(arena.alloc(24), first);
        EXPECT_EQ(arena.alloc(512, 64), second);
        EXPECT_EQ(arena.capacity(), capacity);
    }
}
} // namespace sr::tests
