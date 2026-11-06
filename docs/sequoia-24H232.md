# Sequoia 24H232 适配审查

目标为 Intel macOS **15.8.2 / build 24H232 / Darwin 24.6.0**。本页记录静态审查与 0.5.0 观察版、控制版的单机运行短测，不是通用兼容性或性能承诺。控制版的全核升频、低负载降频及请求读回已验证；尚无该系统的吞吐 A/B 或跑分对照。

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

## 观察版运行短测（2026-11-07）

已从 EFI 启动分发的 0.5.0 观察版，模块 UUID 为 `8AF5FDDF-368C-3902-A320-6534E0D40D48`，二进制 SHA-256 与 manifest 一致。内核、writer、timer、deadline 和规范化 urgency 函数体校验通过；CpuTscSync 原有入口跳转保留，插件未挂接 urgency 或 deadline。

| 检查 | 结果 |
|---|---|
| 负载阶段 | 轻载 6 秒、1 个整数 worker 10 秒、20 个 worker 20 秒、恢复 6 秒 |
| 新增有效样本 | 16,840 个，覆盖逻辑 CPU 0–19 |
| 满载阶段首末快照间每核新增样本 | 391–699 个；20 个逻辑核均持续采样 |
| 全核自检 | `SelfTestReadyMask = 0xFFFFF`，与要求一致 |
| 控制与故障 | `ControlActive = false`；控制写入、写入读回、回退计数均为 0；各快照 `Fault = 0` |
| 原生 class 为零的忙核 | 能持续取得有效样本 |
| 硬件限制识别 | 操作最低 8、Linux 效率最低 12、基频 29、单核睿频 35、十核睿频 32 |

观察版没有改变原生选频；该轮满载的新鲜 APERF/MPERF 样本仍出现约 2.0–3.2 GHz 的核间差异。这是原生策略的观察基线，不是控制版效果或独立性能跑分。随后另一次重启加载控制版，结果如下。短测不覆盖睡眠唤醒或长期稳定性。

## 控制版运行短测（2026-11-07）

加载的控制版模块 UUID 为 `4EAAC467-A1C2-3EE0-B7FF-E1E3DFAC7F4E`，二进制 SHA-256 为 `9db3341b687e687c1380a7e9fc28e579514c7b563587df030f46145759d472a3`，与控制版分发文件一致。`Mode = 2`、`Status = controlling`，全核自检掩码为 `0xFFFFF`；采样中每次 `ControlActive = true` 时均满足至少 15 秒预热及全核资格条件。

测试依次使用轻载 6 秒、20 个 worker 资格负载 10 秒、20 个 worker 满载 20 秒、恢复 10 秒、单 worker 10 秒和最终轻载 10 秒，取得 64 个 IORegistry 快照。另用 Apple powermetrics 以 1 秒间隔采集 36 个样本，与驱动数据按时间对齐。

| 检查 | 结果 |
|---|---|
| 每核持续采样 | 逻辑 CPU 0–19 全覆盖；满载首末快照间每核增加 624–896 个有效样本 |
| 总新增有效样本 | 39,097 |
| 请求覆盖与读回 | 17,374 次覆盖，17,374 次成功读回，回退为 0 |
| 故障及温度状态 | 全部快照 `Fault = 0`；满载驱动热标志为 0；对应 powermetrics 压力为 `Nominal` |
| 每核满载平均频率 | 3.157–3.179 GHz，20 个逻辑核平均约 3.168 GHz |
| powermetrics 满载核对 | 稳定窗口 14 个样本，各逻辑核平均 3.162–3.163 GHz，系统平均约 3.163 GHz |
| 恢复与最终轻载 | 取得新鲜低负载样本，请求最低 12 倍频，有效频率估计最低 1.202 GHz |

驱动频率统计剔除满载阶段最初两个快照，仅使用 `decision_valid`、距离发布时刻不超过 250 ms（允许并发读取造成最多 10 ms 的负差）且 `busy_q8 >= 230` 的样本。每个逻辑核保留 12–17 个合格样本。powermetrics 使用完全落在同一稳定满载时段内的窗口，覆盖全部 20 个逻辑核，避免将过渡或旧样本当成持续频率。数值摘要见 [results JSON](sequoia-24H232-results.json)。

原厂十核睿频表上限为 32 倍频。这里实测约 3.16–3.18 GHz，接近标称全核 3.2 GHz，并非锁定或保证恒定 3.200 GHz。powermetrics 的 P-state 请求分布可能显示 3500 MHz；应与实际 APERF/MPERF 平均频率区分，不能据此宣称全核 3.5 GHz。低负载的低倍频请求和新鲜约 1.2 GHz 样本证明本轮没有锁住最高请求。

两次重启的后台活动未严格控制，没有实施 Sequoia 吞吐 A/B；powermetrics 是另一个采样客户端，并非外部硬件频率计。以上只支持本机该内核的短期调频与读回结论，不证明所有场景都与 Linux 等效，也未验证睡眠唤醒、长期稳定性或空闲功耗。

## 运行验收顺序

1. 只接入观察版，重启检查 Lilu 加载、目标 UUID、四个函数校验和两个挂钩。
2. 检查 20 个逻辑 CPU 在轻载及整数负载下持续采样，原生 class 为零也能采样；`controlled_writes`、`readback_verified_writes`、`fallback_writes` 与 `Fault` 均应为零，`ControlActive` 应为 false。
3. 观察验收通过后再评估控制版，验证至少 15 秒预热及每核样本门控、原请求读回、满载约 3.2 GHz 与负载结束后降频。

不改 Turbo 表、功耗限制、电压、BIOS、ForceBoost 或线程调度器。没有把支持范围扩大到其他 macOS 15 build；系统更新后仍需要审查实际内核。
