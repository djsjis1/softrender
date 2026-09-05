# 第三方依赖清单

> 本目录以**源码内嵌**方式集成全部第三方库，不使用包管理器、不依赖网络下载。
> 集成策略与理由见 `docs/architecture.md`。
>
> 版本号均为**从源码中实测提取**，非人工填写。证据列给出具体文件与行号，
> 便于后续升级时核对。

## 依赖表

| 库 | 版本 | 用途 | 许可证 | 版本证据 |
|---|---|---|---|---|
| GoogleTest | 1.18.0 | 单元测试框架 | BSD-3-Clause | `googletest-main/CMakeLists.txt:7` `set(GOOGLETEST_VERSION 1.18.0)` |
| spdlog | 1.17.0 | 日志后端（可选，`SR_ENABLE_SPDLOG`） | MIT | `log/spdlog-1.17.0/include/spdlog/version.h:6-8` |
| stb_image | 2.30 | 图像加载（PNG/JPG/BMP/TGA/HDR 等） | Public Domain / MIT | `stb/stb_image.h:1` |
| stb_image_write | 1.16 | 图像写出（PNG/JPG/BMP/TGA） | Public Domain / MIT | `stb/stb_image_write.h:1` |
| EnTT | 3.14.0 | ECS 场景管理 | MIT | `entt/src/entt/config/version.h:8-10` |
| tinyobjloader | 2.0.0 ⚠️ | Wavefront OBJ 加载 | MIT | `tiny_obj_loader.h:26` |
| tinygltf | v2.9.* ⚠️ | glTF 2.0 / glb 加载与序列化 | MIT | `tinygltf/tiny_gltf.h:28` |
| nlohmann/json | 3.11.3 | JSON 解析（tinygltf 依赖） | MIT | `nlohmann/json.hpp:62,68` |

⚠️ = **版本未完全锁定**，见下方「已知版本锁定缺口」。

## 非第三方内容

| 路径 | 说明 |
|---|---|
| `log/Logger.h`、`log/Logger.cpp`、`log/CMakeLists.txt`、`log/example.cpp` | **本项目自研**的 spdlog 轻量封装库（CMake 目标名 `mylog`），设计为可独立 `add_subdirectory` 复用。放在 `third/` 是因为它以「可复用子库」形态存在，而非本项目 `src/` 的一部分。 |
| `tiny_obj_loader_impl.cpp`、`tinygltf/tiny_gltf_impl.cpp` | **本项目自研**的实现单元（`#define XXX_IMPLEMENTATION` 的单一 .cpp），用于把 header-only 库的符号收敛到静态库，避免多重定义。⚠ `tiny_gltf_impl.cpp` 的文件名有误导性，它**不含 tinygltf**，详见下表 |

## 集成方式

全部依赖由根 `CMakeLists.txt` 统一管理，共三个 CMake 目标：

| CMake 目标 | 类型 | 内容 |
|---|---|---|
| `third_party_impl` | STATIC | 下面三个实现单元，合起来提供 stb / tinygltf / tinyobjloader 的全部符号 |
| `entt` | INTERFACE | 头文件搜索路径指向 `entt/src` |
| `mylog` | STATIC | 由 `add_subdirectory(third/log)` 引入，仅在 `SR_ENABLE_SPDLOG=ON` 时存在 |

### 三个实现单元的真实分工

stb / tinygltf / tinyobjloader 都靠「在**恰好一个** .cpp 里 `#define XXX_IMPLEMENTATION`」
来产出符号：多处定义会符号重定义，一处不定义则链接报未解析符号。

| 实现单元 | `#define` | 提供的符号 |
|---|---|---|
| `tinygltf/tiny_gltf_impl.cpp` | `STB_IMAGE_IMPLEMENTATION`、`STB_IMAGE_WRITE_IMPLEMENTATION` | **stb_image + stb_image_write** |
| `../src/scene/gltf_loader_impl.cpp` | `TINYGLTF_IMPLEMENTATION` | **tinygltf** |
| `tiny_obj_loader_impl.cpp` | `TINYOBJLOADER_IMPLEMENTATION` | **tinyobjloader** |

> ⚠️ **文件名陷阱**：`tinygltf/tiny_gltf_impl.cpp` 位于 `tinygltf/` 目录、
> 名字也叫 `tiny_gltf_impl`，但它**完全不含 tinygltf 的实现**，只提供 stb。
> tinygltf 的实现在 `src/scene/gltf_loader_impl.cpp`。
>
> 本表由直接阅读三个 `.cpp` 源文件得出。**不要依赖文件名或旧注释推断**：
> 根 `CMakeLists.txt` 原本的注释就声称「tinygltf 内部会 define
> STB_IMAGE_IMPLEMENTATION，所以一个 .cpp 同时提供 stb + tinygltf 的所有符号」，
> 与实际不符（已修正）。

## 已知版本锁定缺口

`.qoder/rules/cpp_project_rules.md`「依赖管理」要求**锁定具体版本号，禁止使用 latest**。
当前有两项不满足，需在下次升级依赖时补齐：

1. **`googletest-main/` 目录名以 `-main` 结尾**，表明是 GitHub main 分支的 zip 快照而非
   release tag。虽然 `CMakeLists.txt` 内部标注 1.18.0，但快照来源无法追溯到具体 commit。
   建议：重新以 `v1.18.0` tag 获取，并把目录重命名为 `googletest-1.18.0/`。
2. **tinygltf 头文件只标注 `v2.9.*`**，未给出修订号；**tinyobjloader 只标注 `2.0.0`**，
   上游实际发布形态是 `v2.0.0-rcN`。两者都无法从源码确定精确修订版本。
   建议：记录获取时的上游 commit hash（本目录当前未保留该信息）。

## EnTT 双份源码警告

`entt/` 下同时存在两份 EnTT 源码，且**版本不同**：

| 路径 | 版本 | 是否被构建使用 |
|---|---|---|
| `entt/src/entt/` | **3.14.0** | ✅ 是（根 `CMakeLists.txt` 的 `entt` INTERFACE 目标指向此路径） |
| `entt/single_include/entt/entt.hpp` | 3.13.2 | ❌ 否（陈旧残留） |

**风险**：若有人误改 include 路径指向 `single_include/`，会静默降级到 3.13.2，
而两个版本的 API 存在差异。升级 EnTT 时必须同步处理这两份，或直接删除 `single_include/`。

## 升级流程

1. 从上游获取指定 **tag**（禁止 main 分支快照）的源码，替换对应目录
2. 更新本文件的「版本」与「版本证据」两列
3. 全量重新配置 + 构建：`cmake -S . -B build` → `cmake --build build --config Release`
4. 跑全量测试：`ctest --test-dir build --output-on-failure -j`
5. 在 `docs/changelog.md` 记录升级项与影响范围
