---
trigger: model_decision
description: 任务涉及项目初始化、新建模块/目录、文件命名、目录结构调整或 .gitignore 配置时加载本规范
---
# 项目目录结构规范

## 核心原则

目录混乱是项目腐化的第一迹象。

模块边界 = 目录边界，禁止跨模块文件散落。

---

# 标准目录结构

```
project/
├── src/                        # 源代码
│   ├── core/                   # 核心库（无平台依赖）
│   │   ├── data_structures/    # 通用数据结构
│   │   ├── algorithms/         # 通用算法
│   │   └── utils/              # 工具函数（仅允许存在于 core/ 下）
│   ├── module_a/               # 业务模块 A
│   │   ├── ModuleA.cpp
│   │   ├── ModuleA.h
│   │   ├── ModuleAProtocol.cpp
│   │   └── ModuleAProtocol.h
│   ├── module_b/               # 业务模块 B
│   │   ├── ModuleB.cpp
│   │   ├── ModuleB.h
│   │   ├── ModuleBProtocol.cpp
│   │   └── ModuleBProtocol.h
│   ├── ui/                     # UI 模块
│   │   ├── MainWindow.h
│   │   ├── MainWindow.cpp
│   │   ├── widgets/            # 自定义控件
│   │   └── views/              # 视图组件
│   ├── network/                # 网络模块
│   │   ├── NetworkClient.h
│   │   ├── NetworkClient.cpp
│   │   ├── protocols/          # 协议实现
│   │   └── serializers/        # 序列化/反序列化
│   └── main.cpp                # 入口

├── include/                    # 公共头文件（对外暴露）
│   └── project_name/           # 命名空间对应目录
│       ├── core/
│       ├── module_a/
│       └── module_b/

├── tests/                      # 测试
│   ├── unit/                   # 单元测试
│   │   ├── core/
│   │   ├── module_a/
│   │   └── module_b/
│   ├── integration/            # 集成测试
│   └── fixtures/               # 测试数据

├── docs/                       # 文档
│   ├── architecture.md
│   ├── modules.md
│   ├── api.md
│   ├── protocol.md
│   ├── deployment.md
│   ├── troubleshooting.md
│   ├── changelog.md
│   └── adr/

├── scripts/                    # 构建/部署/工具脚本
│   ├── build.sh
│   ├── deploy.sh
│   └── format.sh

├── tools/                      # 项目相关工具
│   └── codegen/

├── third/                      # 第三方库（源码 / header-only 直接集成）
│   └── README.md               # 记录每个库的版本、来源、许可证

├── CMakeLists.txt              # 顶层构建文件
├── .clang-format               # 代码格式化配置
├── .clang-tidy                 # 静态分析配置
├── .gitignore
└── README.md
```

---

# 模块目录规范

每个模块 = 一个目录，目录名与命名空间对应。

```
module_a/                  # 目录名 = 模块名（snake_case）
├── ModuleA.h              # 模块主类（PascalCase）
├── ModuleA.cpp
├── ModuleAProtocol.h      # 协议/数据结构（PascalCase）
├── ModuleAProtocol.cpp
├── ModuleATypes.h          # 类型定义（枚举、结构体）
└── ModuleAConfig.h         # 配置类
```

禁止：

* 模块文件散落到 src/ 根目录
* 一个目录包含多个不相关的模块
* 模块内部的头文件和 .cpp 文件放在不同目录（例外：对外公共头统一放 `include/`）

---

# 文件命名规范

| 类型 | 命名 | 示例 |
|------|------|------|
| 类文件 | PascalCase | `MotionController.h` |
| 工具函数文件 | snake_case | `string_utils.h` |
| 测试文件 | 类名_test | `MotionController_test.cpp` |
| 协议文件 | 模块名 + Protocol | `MotionProtocol.h` |
| 类型文件 | 模块名 + Types | `MotionTypes.h` |
| 配置类 | 模块名 + Config | `MotionConfig.h` |

---

# 禁止的目录结构

以下目录结构视为不合格：

```
// 禁止：文件全部散落在 src/
src/
├── aaa.cpp
├── bbb.cpp
├── ccc.h
├── new.cpp          // 不知道是什么
├── new2.cpp         // 更不知道是什么
├── test.cpp         // 不知道测试谁
├── test_new.cpp     // 彻底混乱
├── temp.cpp         // 临时文件没删
└── old_code.cpp     // 废弃代码没删
```

```txt
// 禁止：业务层级出现无意义杂物箱目录（utils 仅允许在 core/ 下）
src/
├── utils/           // 垃圾桶目录（业务层禁止）
├── helpers/         // 另一个垃圾桶
├── common/          // 第三个垃圾桶
└── misc/            // 第四个垃圾桶
```

---

# 目录扩展原则

新增模块时：

1. 判断是否属于已有模块的扩展 → 放入已有目录
2. 判断是否是新功能模块 → 创建新目录
3. 保持命名一致 → 目录名反映模块职责

禁止在已有模块目录下创建不相关的新文件。

---

# Git 忽略规范

`.gitignore` 必须覆盖五类：构建产物（`build/`、`cmake-build-*/`、`*.o/obj/exe/dll/so`）、IDE 临时文件（`.vs/`、`*.user`、`*.suo`）、测试输出（`Testing/`、`coverage/`）、包管理器与第三方构建产物（`vcpkg_installed/`、`third/` 构建输出）、临时文件（`*.tmp`、`*.bak`、`*~`）。

> 具体清单由工具模板生成，不在规则中手工维护。

