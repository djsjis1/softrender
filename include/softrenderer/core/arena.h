/**
 * @file arena.h
 * @brief 按帧复用的线性分配器；reset 使旧指针失效，但保留内存块供下一帧使用。
 *
 * alloc / allocArray 返回未构造的存储；无效参数或长度溢出返回 nullptr。
 * 真正的内存分配失败沿用标准库的 std::bad_alloc；不执行对象析构。
 * 每个线程独立持有一个实例，或使用 frameArena() 后显式调用 reset()。
 */
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace sr
{
class ArenaAllocator
{
   public:
    explicit ArenaAllocator(size_t initialCapacity = 4 * 1024 * 1024)
    {
        // 零容量仍能前进，避免后续翻倍扩容陷入 0 * 2 的死循环。
        addBlock(std::max(size_t{1}, initialCapacity));
    }

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;
    ArenaAllocator(ArenaAllocator&&) = delete;
    ArenaAllocator& operator=(ArenaAllocator&&) = delete;

    /** @brief 分配原始存储；align 必须为非零的 2 次幂，无效请求不改变分配状态。 */
    void* alloc(size_t size, size_t align = 16)
    {
        constexpr auto limit = std::numeric_limits<size_t>::max();
        if (align == 0 || (align & (align - 1)) != 0 || size > limit - (align - 1))
            return nullptr;
        // 最坏对齐填充也必须能记入 used_；先检查再操作游标。
        const size_t required = size + align - 1;
        if (required > limit - used_)
            return nullptr;

        // reset 后顺序复用已有块。当前块用完才向后走，既有指针一直有效到 reset。
        for (size_t index = blockIndex_; index < blocks_.size(); ++index)
        {
            const size_t offset = index == blockIndex_ ? offset_ : 0;
            auto& block = blocks_[index];
            void* ptr = block.data.get() + offset;
            size_t space = block.capacity - offset;
            // std::align 在边界检查后才调整指针，避免先构造越界指针的 UB。
            if (void* aligned = std::align(align, size, ptr, space))
            {
                const size_t padding = block.capacity - offset - space;
                blockIndex_ = index;
                offset_ = offset + padding + size;
                used_ += padding + size;
                return aligned;
            }
        }

        const size_t previous = blocks_.back().capacity;
        const size_t growth = previous <= limit / 2 ? previous * 2 : previous;
        size_t newCapacity = std::max(growth, required);
        if (newCapacity > limit - capacity_)
            newCapacity = required;
        if (newCapacity > limit - capacity_)
            return nullptr;

        // unique_ptr 在 vector 扩容失败时也会释放新块，保持强异常保证。
        addBlock(newCapacity);
        blockIndex_ = blocks_.size() - 1;
        offset_ = 0;
        void* ptr = blocks_.back().data.get();
        size_t space = newCapacity;
        void* aligned = std::align(align, size, ptr, space);
        const size_t padding = newCapacity - space;
        offset_ = padding + size;
        used_ += offset_;
        return aligned;
    }

    /** @brief 分配数组存储，不调用构造函数；元素数乘法溢出时返回 nullptr。 */
    template <typename T>
    T* allocArray(size_t count)
    {
        if (count > std::numeric_limits<size_t>::max() / sizeof(T))
            return nullptr;
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    /** @brief O(1) 重置；所有旧指针失效，保留扩展块直到分配器析构。 */
    void reset() noexcept
    {
        blockIndex_ = 0;
        offset_ = 0;
        used_ = 0;
    }

    /** @brief 本帧分配字节数，包含实际消耗的对齐填充。 */
    size_t used() const noexcept { return used_; }

    /** @brief 已保留的总字节数，用于观测峰值内存占用。 */
    size_t capacity() const noexcept { return capacity_; }

   private:
    struct Block
    {
        std::unique_ptr<uint8_t[]> data;
        size_t capacity;
    };

    void addBlock(size_t capacity)
    {
        // 原始存储无需清零；渲染热路径由使用者自行初始化对象。
        Block block{std::unique_ptr<uint8_t[]>(new uint8_t[capacity]), capacity};
        blocks_.push_back(std::move(block));
        capacity_ += capacity;
    }

    std::vector<Block> blocks_;
    size_t blockIndex_ = 0;
    size_t offset_ = 0;
    size_t used_ = 0;
    size_t capacity_ = 0;
};

/** @brief 获取当前线程的帧分配器；调用方必须在帧末显式 reset。 */
inline ArenaAllocator& frameArena()
{
    thread_local ArenaAllocator arena(2 * 1024 * 1024);
    return arena;
}
} // namespace sr
