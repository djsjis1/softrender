#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

// ============================================================
// 线性内存池（Arena Allocator）
//
// 渲染器每帧大量临时分配（顶点缓冲、片元队列等），用 new/delete
// 会产生碎片和系统调用开销。线性池的思路：预分配一大块内存，
// 分配时只移动指针，帧末一次性 reset——O(1) 分配、O(1) 释放。
//
// 适用场景：每帧生命周期相同的临时数据。
// 不适用：跨帧存活的对象（用 std::allocator 或资源管理器）。
//
// 参考：Mesa 的 ralloc、Unity 的 TempAlloc、游戏引擎的 FrameAllocator。
// ============================================================
namespace sr {

class ArenaAllocator
{
   public:
    explicit ArenaAllocator(size_t initialCapacity = 4 * 1024 * 1024)  // 默认 4MB
        : capacity_(initialCapacity)
    {
        blocks_.push_back(new uint8_t[capacity_]);
        current_ = blocks_.back();
        end_ = current_ + capacity_;
    }

    ~ArenaAllocator()
    {
        for (auto* b : blocks_)
            delete[] b;
    }

    // 不可拷贝
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // 分配 size 字节，align 对齐（默认 16 字节，适合 SIMD）
    void* alloc(size_t size, size_t align = 16)
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(current_);
        uintptr_t aligned = (addr + align - 1) & ~(align - 1);
        size_t padding = aligned - addr;
        if (current_ + padding + size > end_)
        {
            // 当前块不够，分配新块（至少能容纳本次请求）
            size_t newCap = capacity_;
            while (newCap < size + align)
                newCap *= 2;
            blocks_.push_back(new uint8_t[newCap]);
            current_ = blocks_.back();
            end_ = current_ + newCap;
            addr = reinterpret_cast<uintptr_t>(current_);
            aligned = (addr + align - 1) & ~(align - 1);
            padding = aligned - addr;
        }
        current_ = reinterpret_cast<uint8_t*>(aligned + size);
        used_ += padding + size;
        return reinterpret_cast<void*>(aligned);
    }

    // 模板便捷版
    template <typename T>
    T* allocArray(size_t count)
    {
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    // 帧末重置：所有分配一次性释放
    void reset()
    {
        // 保留第一块，释放多余的
        while (blocks_.size() > 1)
        {
            delete[] blocks_.back();
            blocks_.pop_back();
        }
        current_ = blocks_[0];
        end_ = current_ + capacity_;
        used_ = 0;
    }

    // 已用字节数（含对齐填充，调试用）
    size_t used() const { return used_; }

   private:
    size_t capacity_;
    size_t used_ = 0;
    std::vector<uint8_t*> blocks_;
    uint8_t* current_ = nullptr;
    uint8_t* end_ = nullptr;
};

// 线程局部帧分配器（每线程一个，无锁）
inline ArenaAllocator& frameArena()
{
    thread_local ArenaAllocator arena(2 * 1024 * 1024);
    return arena;
}

}  // namespace sr
