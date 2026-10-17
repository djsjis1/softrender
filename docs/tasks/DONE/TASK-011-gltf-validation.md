# TASK-011：glTF 数据读取与动画采样校验

- 日期：2026-10-05 19:30
- 状态：本地实现与验证完成；远端 CI 由 TASK-004 跟进
- 范围：scene accessor/mesh/animation、蒙皮示例、单测、加载与动画文档

## 设计与边界

采用共享非拥有 AccessorView，统一检查 accessor/bufferView/buffer 下标、偏移、长度、stride、类型与归一化。
逐分量按 glTF 小端编码读取，避免将字节缓冲强转为 float/uint 指针。支持标量、向量和 FLOAT MAT4；
稀疏 accessor 与需特殊列填充的矩阵在本轮明确返回不支持，不能静默按普通数据读取。

方案 A 是在旧 loader/player 中分别补判断；方案 B 是共享读取层（采用），避免相同错误在多个路径重复出现。
网格转换失败返回清晰错误，不暴露半成品；动画验证失败不修改输出。补齐 CubicSpline 的三元组读取与 Hermite 插值。
unsafe accessorFloats 裸指针助手改由 AccessorView 替代，并记录迁移；常用 load/sampleAnimation 调用保留。

## 验收

- [x] 原实现的 CubicSpline 错误复现
- [x] 数据读取层、网格/动画消费者与示例接线
- [x] 坏下标、截断、溢出、交错顶点、归一化、非法动画时间/输出等测试
- [x] Release 全量、Debug ASAN、下游安装和相关 demo 验证
- [x] 同步架构、模块、教程、分析报告、状态与变更日志

## 验证记录

Release 155 单元测试与两个消费工程通过；Debug ASAN 153 通过、2 跳过；3 像素场景通过。详见 ../../project_review.md。线程创建失败路径已审查、未故障注入；TSAN/UBSAN CI 尚未远端执行。
