# Sequoia 24H232 适配审查

目标为 Intel macOS **15.8.2 / build 24H232 / Darwin 24.6.0**。本页记录静态审查，不是运行兼容性或性能承诺。0.5.0 观察版尚待重启验证；尚无该目标的调频或跑分结果。

## 与 Tahoe 的差异

| 项目 | Sequoia 24H232 | Tahoe 25G332 |
|---|---|---|
| 内核 UUID | `747934E9-FD01-36AB-8E97-06A578AB22F9` | `28267FD5-3E1E-3686-8A1C-E73E2466FCFB` |
| writer 相对 `_xcpm_enable_pstate_ctrs` | `+0x1140` | `+0x1190` |
| writer / timer 长度 | 736 / 928 | 736 / 928 |
| urgency / deadline 长度 | 5696 / 48 | 5728 / 48 |
| 部署目标 | macOS 15.0 | macOS 26.0 |

每个目标使用独立头文件、独立指纹和编译宏。遗漏或同时指定两个宏会编译失败，不能通过更改 OpenCore 的版本范围把 Tahoe 二进制用于 Sequoia。

## 静态证据

- 独立 kernel、Boot KC 内嵌 kernel 与正在运行内核的 UUID 一致，完整文件 SHA-256 记录于 [配置](../profiles/sequoia-24H232/boot.json)。
- `LC_FUNCTION_STARTS` 分别验证四个函数在独立 kernel 与 Boot KC 中的边界。反汇编与分段地址转换解释了所有差异：writer 19 处重定位、timer 20 处、urgency 106 处、deadline 1 处；短跳板所用序言不变。
- 原生 writer 仍以第一个整数参数接收倍频，非 HWP 路径经左移 8 位写入 MSR `0x199`。入口返回值继续原样返回。额外控制写入仍交给原生 writer，不在插件中直接执行 `wrmsr`。
- 原生 lcpu 索引位于 `+0x44`，每核状态步长 `0x380`，诊断 class 位于 `+0xCC`；package 最大／最小请求字段为 `+0x14` / `+0x18`。package ticket lock/unlock 仍接收单个锁地址参数。
- 保留 CpuTscSync；对 urgency 已有入口跳转只校验规范化函数体，不新增 urgency 钩子。原生 deadline 路径不变。
- 观察版外部符号共 315 个，其中 306 个由当前 Sequoia 内核提供、9 个由 EFI 中 Lilu 1.7.2 提供，无缺失符号。26.2 SDK 的 libkmod 包装代码仅使用模块自身字段及入口指针；该检查不能替代重启加载测试。

Apple 内核、提取的原生函数体和反汇编均不分发。配置仅含地址、长度、UUID、摘要和重定位统计。

## 运行验收顺序

1. 只接入观察版，重启检查 Lilu 加载、目标 UUID、四个函数校验和两个挂钩。
2. 检查 20 个逻辑 CPU 在轻载及整数负载下持续采样，原生 class 为零也能采样；`controlled_writes`、`readback_verified_writes`、`fallback_writes` 与 `Fault` 均应为零，`ControlActive` 应为 false。
3. 观察验收通过后再评估控制版，验证至少 15 秒预热及每核样本门控、原请求读回、满载约 3.2 GHz 与负载结束后降频。

不改 Turbo 表、功耗限制、电压、BIOS、ForceBoost 或线程调度器。没有把支持范围扩大到其他 macOS 15 build；系统更新后仍需要审查实际内核。
