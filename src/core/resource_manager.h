#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>

// ============================================================
// 资源管理器：引用计数 + 缓存 + 线程安全
//
// 管理纹理、网格等资源的生命周期：
//   - 同一资源只加载一次（缓存）
//   - 引用计数归零时自动释放
//   - 线程安全（多线程渲染器必须）
//
// 用法：
//   auto tex = ResourceManager::instance().load<Texture>("brick.png");
//   // tex 是 shared_ptr<Texture>，离开作用域自动减引用
// ============================================================

namespace sr {
// C++17 探测习语：判断 T 是否有 loadFromFile(const std::string&)
template <typename T, typename = void>
struct has_loadFromFile : std::false_type
{
};
template <typename T>
struct has_loadFromFile<T, std::void_t<decltype(std::declval<T&>().loadFromFile(std::declval<const std::string&>()))>>
    : std::true_type
{
};
}  // namespace sr

class ResourceManager
{
   public:
    static ResourceManager& instance()
    {
        static ResourceManager rm;
        return rm;
    }

    // 加载资源（缓存命中则直接返回）
    template <typename T>
    std::shared_ptr<T> load(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = std::string(typeid(T).name()) + ":" + path;
        auto it = cache_.find(key);
        if (it != cache_.end())
        {
            auto sp = it->second.lock();
            if (sp)
                return std::static_pointer_cast<T>(sp);
            // 已过期，移除
            cache_.erase(it);
        }
        // 创建新资源（若类型支持 loadFromFile 则自动加载，加载失败返回 nullptr）
        auto resource = std::make_shared<T>();
        if constexpr (sr::has_loadFromFile<T>::value)
        {
            if (!resource->loadFromFile(path))
                return nullptr;
        }
        cache_[key] = resource;
        return resource;
    }

    // 手动注册已创建的资源
    template <typename T>
    std::shared_ptr<T> registerResource(const std::string& name, std::shared_ptr<T> resource)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = std::string(typeid(T).name()) + ":" + name;
        cache_[key] = resource;
        return resource;
    }

    // 清除所有已过期（引用计数=0）的缓存条目
    void purge()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = cache_.begin(); it != cache_.end();)
        {
            if (it->second.expired())
                it = cache_.erase(it);
            else
                ++it;
        }
    }

    // 清除所有缓存（强制释放）
    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }

    size_t cacheSize() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }

   private:
    ResourceManager() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::weak_ptr<void>> cache_;
};
