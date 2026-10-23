#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <typeinfo>
#include <unordered_map>

// ============================================================
// ResourceCache：按 key 缓存共享资源（弱引用 + 线程安全）
//
// 【职责边界】只负责"同一个 key 只创建一次"和生命周期管理，
// 不认识任何文件格式：资源怎么来由调用方给一个工厂回调。
// 因此 runtime 不依赖 io，io 也不需要反向依赖缓存。
//
// 【失败语义】工厂返回空指针表示失败，失败结果不写入缓存：
// 下次调用会重新尝试，避免把一次瞬时失败固化成永久失败。
//
// 用法：
// @code
//   auto texture = sr::ResourceCache::instance().load<Texture>("brick.png", [] {
//       sr::Image image;
//       if (!sr::io::loadImage("brick.png", image))
//           return std::shared_ptr<Texture>{};
//       return std::make_shared<Texture>(std::move(image));
//   });
// @endcode
// ============================================================

namespace sr
{

class ResourceCache
{
   public:
    static ResourceCache &instance()
    {
        static ResourceCache cache;
        return cache;
    }

    // 取得资源：命中且未过期则复用，否则调用 factory 创建。
    // factory 返回 nullptr 表示创建失败——失败不进入缓存。
    template <typename T>
    std::shared_ptr<T> load(const std::string &key, const std::function<std::shared_ptr<T>()> &factory)
    {
        const std::string entryKey = std::string(typeid(T).name()) + ":" + key;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = entries_.find(entryKey);
            if (it != entries_.end())
            {
                auto existing = it->second.lock();
                if (existing)
                    return std::static_pointer_cast<T>(existing);
                entries_.erase(it); // 已被释放，清掉过期条目
            }
        }
        // 工厂在锁外调用：创建资源可能很慢，不能阻塞其它 key 的查询
        std::shared_ptr<T> created = factory ? factory() : nullptr;
        if (!created)
            return nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_[entryKey] = created;
        }
        return created;
    }

    // 清理所有引用计数已归零的条目
    void purge()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end();)
            it = it->second.expired() ? entries_.erase(it) : std::next(it);
    }

    // 清空缓存（强制释放本缓存持有的引用）
    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

   private:
    ResourceCache() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::weak_ptr<void>> entries_;
};

} // namespace sr
