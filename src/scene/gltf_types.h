#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "math/mat4.h"
#include "math/vec.h"
#include "scene/texture.h"

// ============================================================
// glTF PBR 材质（metallic-roughness 工作流）
//
// 对应 glTF 2.0 规范中的 material 对象。
// 参考：https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials
// ============================================================

struct GltfMaterial
{
    // PBR metallic-roughness
    Vec3f baseColorFactor{1, 1, 1};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;

    // 纹理索引（-1 = 无纹理）
    int baseColorTextureIndex = -1;
    int metallicRoughnessTextureIndex = -1;
    int normalTextureIndex = -1;
    int emissiveTextureIndex = -1;

    // 自发光
    Vec3f emissiveFactor{0, 0, 0};

    // Alpha 模式
    // 底层类型 uint8_t：cpp_project_rules.md 要求枚举必须有明确的底层类型
    enum class AlphaMode : uint8_t
    {
        Opaque,
        Mask,
        Blend
    };
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;

    // 双面
    bool doubleSided = false;

    // 名称
    std::string name;

    // 解析后的纹理指针（加载后填充）
    Texture* baseColorTexture = nullptr;
    Texture* metallicRoughnessTexture = nullptr;
    Texture* normalTexture = nullptr;
};

// glTF 纹理信息
struct GltfTexture
{
    int imageIndex = -1;    // 指向 images 数组
    int samplerIndex = -1;  // 指向 samplers 数组
    Texture loaded;         // 加载后的纹理数据
};

// glTF 图像信息
struct GltfImage
{
    std::string name;
    std::string uri;  // 外部 URI 或 base64
    std::string mimeType;
    std::vector<uint8_t> data;  // 内嵌 buffer 数据
    int width = 0, height = 0;
};

// glTF 节点（场景图）
struct GltfNode
{
    std::string name;
    int meshIndex = -1;
    int skinIndex = -1;
    std::vector<int> children;
    Vec3f translation{0, 0, 0};
    Vec3f scale{1, 1, 1};
    // 旋转用四元数存储（glTF 原生格式）
    float rotation[4] = {0, 0, 0, 1};  // x,y,z,w
    Mat4 matrix = Mat4::identity();    // 如果有 matrix 则覆盖 TRS
    bool hasMatrix = false;
};

// glTF 皮肤（骨骼蒙皮）
struct GltfSkin
{
    std::string name;
    std::vector<int> joints;       // 关节节点索引
    int skeleton = -1;             // 根骨骼节点
    int inverseBindMatrices = -1;  // accessor 索引
};

// glTF 动画
struct GltfAnimation
{
    std::string name;
    struct Channel
    {
        // 成员声明即初始化（cpp_project_rules.md「初始化规范」）。
        //
        // 【path 的默认值是修一个真实缺陷】gltf_loader.h 只在 target_path 等于
        // "translation" / "rotation" / "scale" 三者之一时才赋值 path；而 glTF 2.0
        // 还允许 "weights"（形变目标）与扩展自定义值。遇到这些值时三个分支
        // 全部不命中，旧代码的 path 就是**不确定值**，读取它属 UB。
        //
        // 【下面四个 int 字段是防御性初始化】gltf_loader.h 当前对它们都做了
        // 无条件赋值（先填满局部对象再 push_back），所以暂无活跃缺陷；
        // 给默认值是为了保护未来的 loader 修改。
        // 选 -1 而非 0：0 是合法的数组下标，用它做「未设置」会静默指向首元素；
        // -1 能被下游校验识别，且与同文件 GltfTexture::imageIndex / samplerIndex
        // 已有的 -1 约定一致。animation_player.h 已有
        // `if (channel.samplerIndex < 0 || channel.samplerIndex >= size) return;` 的校验。
        int samplerIndex = -1;
        int targetNode = -1;
        enum class Path : uint8_t
        {
            Translation,
            Rotation,
            Scale
        } path = Path::Translation;
    };
    struct Sampler
    {
        int input = -1;   // accessor（时间戳）；-1 = 未设置，理由同 Channel
        int output = -1;  // accessor（关键帧值）
        enum class Interpolation : uint8_t
        {
            Linear,
            Step,
            CubicSpline
        } interpolation = Interpolation::Linear;
    };
    std::vector<Channel> channels;
    std::vector<Sampler> samplers;
};
