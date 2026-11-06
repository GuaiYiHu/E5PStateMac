# E5PStateMac

Experimental frequency control for **Intel Xeon E5-2666 v3 / macOS**, implemented as a Lilu plugin.

针对 E5-2666 v3 的实验性 macOS 调频驱动。目标是保留低负载降频，在普通整数满载时恢复处理器原有的全核睿频。当前源码为 **0.5.0 实验版**，增加 macOS 15 的独立内核适配；已有运行短测的数据仍来自 macOS 26 上的 **0.4.0**。

> **仅在一台机器的 macOS 26 固定内核上做过短测，不是通用 X99 驱动。macOS 15 适配尚待重启运行验收。** 使用私有 XCPM 接口，错误适配可能导致冻结或内核恐慌。长期稳定性、睡眠唤醒和空闲功耗尚未验证。

## 已审查的目标

| 项目 | 当前目标 |
|---|---|
| CPU | 单路 Intel Xeon E5-2666 v3，10 核 20 线程，真实 CPUID `0x306F2` |
| 平台 | X99，未解锁全核最高睿频 |
| 依赖 | Lilu 1.7.2；测试机器保留 CpuTscSync 1.1.2 |

| 构建目标 | 系统与内核 | 证据与状态 |
|---|---|---|
| `tahoe-25G332` | macOS 26.7.2 / `25G332` / Darwin `25.6.0`；UUID `28267FD5-3E1E-3686-8A1C-E73E2466FCFB` | [固定指纹](kernel-profile.json)；0.4.0 控制版有单机短测；0.5.0 重新构建未做运行验收 |
| `sequoia-24H232` | macOS 15.8.2 / `24H232` / Darwin `24.6.0`；UUID `747934E9-FD01-36AB-8E97-06A578AB22F9` | [独立指纹](profiles/sequoia-24H232/boot.json)、[审查说明](docs/sequoia-24H232.md)；观察版静态校验通过，运行与调频效果待验证 |

每个二进制只编入一个目标；内核 UUID 和函数指纹不匹配时拒绝安装钩子。系统更新改变内核后，需要重新审查适配，不能仅替换散列值。Darwin 版本号相同也不代表内核相同。

## 做了什么

- 使用参考 Linux 6.8 `intel_pstate` 非 HWP 路径实现的整数选频核心，按有效负载选择频率请求。
- 通过原生 XCPM writer 写入，沿用已有本核 timer 回调；额外本地写入使用原生 package ticket lock。
- 修复早期观察版把原生状态字段为零当成“空闲”、导致满载漏采样的问题。该字段现在只作诊断。
- 保留 CpuTscSync，不再挂接共享的 urgency 入口，不改变原生 deadline，不增加周期性跨核 rendezvous/IPI。
- 保留 Apple 与硬件上下限、Turbo Disable 和热状态检查。不改电压、功耗上限、BIOS、睿频表或 OpenCore ForceBoost。
- 请求覆盖后读取 IA32_PERF_CTL 验证。失败时尝试恢复本次原请求，并停止进一步覆盖。

这是 CPU **调频**策略适配，线程调度仍由 macOS 负责。没有完整复现 Linux 调度事件或 I/O-wait 语义，也不宣称所有应用性能、响应时间及功耗与 Linux 相同。

## 自检与模式

控制版启动或唤醒后先等待至少 **15 秒**，并要求 20 个逻辑 CPU 各取得连续 8 个、间隔不超过 100 ms 的有效样本；收齐后才允许接管。15 秒不是保证开启的固定时间。

每次实际覆盖还要求本核决策新鲜、无相关热状态标志，且 XCPM 已配置、未暂停、非 HWP、非断言模式。空闲核心不会为了收样而被本驱动周期唤醒。缺少有效反馈时沿用原请求。

| 构建／启动参数 | 行为 |
|---|---|
| Observer 构建，无参数 | 默认 mode 1；频率控制编译禁用 |
| Control 构建，无参数 | 默认 mode 2；运行自检通过后可覆盖频率请求 |
| `e5pm=1` | 观察模式 |
| `e5pm=2` | Control 构建使用带门控的动态控制；Observer 构建仍只观察 |
| `e5pm=0` 或 `-e5pmoff` | 禁用本插件 |

高负载时可能请求 35 倍频，由处理器原有 N-core 睿频表限制实际频率；测试机器的十核上限为 32。不会解锁全核 35 倍频，也不是固定最高频率。驱动不支持热卸载。

## 下载与编译

[dist/v0.4.0](dist/v0.4.0) 提供控制版与观察版的 `.kext`、ZIP、散列及依赖许可。**控制版会实际改变频率请求。** 当前控制版二进制对应下述单机短测；观察版完成编译和静态检查，未单独进行 0.4.0 运行验收。

[dist/v0.5.0-sequoia-preview](dist/v0.5.0-sequoia-preview) 提供 `24H232` 专用观察版；控制路径已编译禁用。它尚未完成重启运行验收，不代表 macOS 15 调频已修复。

需要 Xcode、macOS Kernel.framework 头文件与 x86_64 交叉编译支持。0.5.0 使用 macOS 26.2 SDK，Sequoia 对象文件的部署目标为 15.0；本机已核对观察版全部外部符号可由固定 Sequoia 内核和 Lilu 提供。SDK 版本号本身不能证明运行兼容。固定版本的公开 Lilu SDK 和 MacKernelSDK 已放在 `vendor/`，许可随源码保留。

```sh
python3 build.py --target sequoia-24H232 --shadow-only
# 仅在观察模式验收后评估控制版：
python3 build.py --target sequoia-24H232 --guarded-control
```

输出位于 `build-<target>-shadow/E5PStateMac.kext` 和 `build-<target>-control/E5PStateMac.kext`。省略 `--target` 时仅按当前准确 build 选择已审查目标；不支持的 build 会报错。编译脚本不会安装、加载驱动或修改 EFI。原 0.4.0 分发文件保留不变。

## 安装边界

此仓库不包含完整 EFI、个人 `config.plist` 或绑定某台机器的安装脚本。[fragments](fragments) 只提供 OpenCore `Kernel -> Add` 条目供审查，需要置于 Lilu 之后。

Sequoia 观察版使用独立目录 `E5PStateMacSequoia.kext`，其 Add 条目的 `MinKernel`、`MaxKernel` 均为 `24.6.0`。它与原 26 版的注入范围必须互斥；相同 CFBundleIdentifier 的两份驱动不能在同一内核同时注入。内核范围检查之后仍有驱动内 UUID、指纹与 CPU 自检。

自行测试前应先核对固定内核、CPU 和依赖，保留完整 EFI 备份及可用的恢复启动路径。优先验证观察模式，再评估控制模式。不要将本仓库的条目直接并入不匹配的系统，也不要覆盖整套 EFI。

回退可在下一次启动使用 `-e5pmoff`，或禁用自己的 OpenCore Add 条目并重启；不要尝试热卸载。仓库中没有可在其他机器上直接执行的自动 EFI 写入命令。

## 测试

Linux/macOS 上的 C++17 主机测试：

```sh
python3 tests/run_tests.py
```

在上表的固定 macOS 内核上，验证本地 KC、函数重定位与原生函数体，并检查构建：

```sh
python3 tests/run_tests.py --target sequoia-24H232 --kernel-profile \
  --binary build-sequoia-24H232-shadow/E5PStateMac.kext
```

原生函数体测试从本机 KC 临时提取数据，不分发 Apple 内核或其二进制片段。可额外传入自己取得的 Linux 6.8 源文件进行 20,000 组差分：

```sh
python3 tests/run_tests.py --linux-reference /path/to/linux-6.8/drivers/cpufreq/intel_pstate.c
```

这些测试验证算法、门控和静态目标，不等于运行安全或长期稳定性验证。

## macOS 26 / 0.4.0 单机短测结果

| 项目 | 结果 |
|---|---|
| 20 线程非 AVX 整数满载 | 各逻辑核平均约 3.191–3.195 GHz，整体约 3.193 GHz |
| 负载结束后的有效频率样本 | 最低约 1.236 GHz；请求可降至 12 倍频 |
| 66 秒分阶段测试 | 20 核持续采样，11,225 次请求覆盖全部读回成功，故障／回退为 0 |
| 7-Zip 压缩 | 46,434 → 50,146 KiB/s，约 +8.0% |
| 7-Zip 解压 | 436,531 → 477,513 KiB/s，约 +9.4% |
| Geekbench 7.1.0 单核／多核 | 868 / 5872 → 1038 / 7114 |

频率来自 APERF/MPERF 测算。两次吞吐／Geekbench 测试在不同时间运行，未严格控制后台负载，不是重复重启的严格 A/B 实验；Geekbench 运行期间没有同步采集逐核频率。见 [验证记录](docs/validation.md)。

## 参考与许可

- [Linux 6.8 intel_pstate](https://github.com/torvalds/linux/blob/v6.8/drivers/cpufreq/intel_pstate.c)：非 HWP 选频规则的参考；差分测试在本地提取用户提供的源函数。
- [Lilu 1.7.2](https://github.com/acidanthera/Lilu/tree/1.7.2)：插件与 short route SDK。
- [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK/tree/3f750085caa17ec3a7880f11c11bf4f48cd6a164)：固定头文件版本。
- [XNU PM 接口](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/i386/pmCPU.h)：公开回调声明；私有入口与锁约定来自固定目标内核的本地审查。

项目源码保留 [BSD-2-Clause 许可](LICENSE)。第三方文件按各自文件头与 `vendor/` 内许可分发，见 [第三方说明](docs/third-party.md)。
