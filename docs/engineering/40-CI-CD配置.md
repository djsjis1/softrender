# 40 · CI/CD 配置

> GitHub Actions 三平台（Windows/Linux/macOS）自动构建 + 测试 + 冒烟运行。

## 一、工作流

`.github/workflows/ci.yml` 在 push/PR 时触发：

1. **Configure**：`cmake -S . -B build`
2. **Build**：`cmake --build build --parallel`
3. **Unit Tests**：`ctest --output-on-failure`
4. **Smoke Tests**：运行所有离屏 stage 示例
5. **Upload Artifacts**：上传 `output/` 目录

## 二、矩阵

| OS | 编译器 | 说明 |
|---|---|---|
| windows-latest | MSVC | 主力开发平台 |
| ubuntu-latest | GCC/Clang | Linux 兼容性 |
| macos-latest | Clang | macOS 兼容性 |

## 三、本地运行测试

```bash
cmake --build build --target unit_tests
ctest --test-dir build --output-on-failure
```

## 四、对应文件

| 文件 | 作用 |
|---|---|
| `.github/workflows/ci.yml` | GitHub Actions 工作流 |
| `CMakeLists.txt` | `enable_testing()` + `gtest_discover_tests` |

---

上一篇：[39 · 命令缓冲与线程池](39-命令缓冲与线程池.md)
