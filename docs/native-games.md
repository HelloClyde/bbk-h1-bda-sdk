# 原生 H1 V1.41 游戏接口与移植改进

这批改进来自 H1-GBA/GB/GBC 的开发和真机诊断。基线为上游
`067fe072477861dfc8949d7b1a55279fb92d2548`。固件提供很多游戏功能，旧 SDK
缺少的是公开封装、生命周期和调用约定；不能把应用算法或 QEMU 的错误归为 SDK 缺陷。

## 使用范围

```c
#include "h1_v141.h"
```

这是**明确选择 V1.41 ABI**的入口，未自动加入通用 `h1_sdk.h`，不适用于 V2
或 9588。服务表偏移并不是固件版本检测；开发者必须先确认运行环境。
触摸、音频内部游标另有指令特征检查，不能把它当成完整固件鉴权。

| 改进 | 接口与边界 |
|---|---|
| 文件选择 | `h1_file_pick` 返回 1=选择、0=取消、-1=参数/接口/路径错误。系统没有返回长度参数，不能保证任意未知固件的写入安全。 |
| 组合键 | `h1_game_input_event` + `h1_game_input_sample`，每帧查询一次，同一原生码只查询一次。 |
| 原生屏幕 | `h1_game_open/close`、`h1_game_framebuffer_get`，获取实际缓冲地址和以字节计的行跨度。 |
| RGB565 输出 | `h1_game_blit_rgb565` 在缓存内转换整行，再顺序写 LCD，避免读回无缓存 framebuffer。未提供新的插值缩放器。 |
| 触摸 | `h1_touch_position` 返回校准后的 480×272 LCD 坐标，失败不改输出；事件常量补入 `h1_input.h`。 |
| PCM | `h1_pcm_api_get`、完整 32 字节描述符和 36 字节配置类型。未知模式、route/flags 不编造枚举。 |
| RTC | `h1_rtc_read_local_seconds`、`h1_rtc_read_calendar`，只读有效 2000–2099 日历，不修改设备时钟。 |
| JIT | `h1_code_cache_sync` 同步 KSEG0 可执行 RAM 的 D/I cache，不能代替核心 ABI 适配。 |

### 文件与 GUI 模式

路径是 GBK 字节串，扩展名使用 `gba;gb;gbc`，不带点，每项最多 10 字节，
最多 10 项；`*` 为全部文件，空字符串不是全部文件。`h1_path_directory_gbk`
不会把 GBK 尾字节 `0x5c` 当作目录分隔符。调用方预留 264 字节；包装器的
512 字节内部缓冲和长度检查是防御措施，原系统 API 仍没有容量参数。

系统选择器返回后，用 GUI+0x84C 创建原生游戏窗口，以 GUI+0x850 关闭。
原生游戏模式下 GUI+0x070 会跳过刷新；这不是游戏核心卡住。GUI+0x8F4 返回
xRGB8888 (`0x00RRGGBB`) framebuffer，V1.41 为 480×272、1920 字节行跨度。
进入/离开系统 UI 后重新查询，不能缓存一个硬编码 LCD 地址。

### 按键与触摸

事件键码和原生游戏码不一样：D=事件 10/游戏 32，K=事件 32/游戏 37。
Enter/Confirm、左 Alt/Left、右 Alt/Right 共享游戏码。状态包装器利用事件身份
选择最近一个实体别名；固件不能通过原生查询独立区分同时按住的同码别名。
`h1_game_input_sample_selected` 只查询映射到游戏/快捷操作的键，减少每帧固件调用；
未选择的键状态清零，选择数组的长度为 45。`h1_alloc_aligned(size,32)` 可分配
PCM/JIT 对齐缓冲，必须用 `h1_free_aligned` 释放，不可用普通 free。
Fn/Power 没有确认的原生转换，使用事件处理。`h1_input_poll_key` 会丢弃触摸和
其他事件，完整游戏应直接收取 `h1_event_fetch` 并把键事件送给状态包装器。

触摸事件 11=按下、12=移动、8=释放，事件值不提供可直接使用的屏幕坐标。
触摸可能合成 ESC，应用必须自己定义暂停/退出策略。GUI+0x6C0 读取 ADC 并
应用系统校准；原生模式缓存坐标可能过期，不应自行解算未校准的原始 ADC。

## PCM 与音频节奏

原版《使命》的音效用 **SYS** 表，不是 GUI 播放接口：
`+0x50/+0x54` 初始化/销毁描述符，`+0x58` 初始化设备，`+0x5C` 提交，
`+0x60/+0x64/+0x68` 开始/停止/关闭。《使命》配置前三字为
`{11025,1,4096}`，H1-GBA 使用 `{32000,1,4096}`；mode=1 符合已验证的
16 位单声道路径，不承诺其他枚举含义。描述符和 PCM 地址须 32 字节对齐，
后 24 字节归系统管理。调用 init 后不能再清零描述符，数据须活到取消引用后。

```c
#include "experimental/h1_pcm_stream.h"
static h1_pcm_stream stream; /* 零初始化，设备只允许一个拥有者 */
static short ring[8192] __attribute__((aligned(32)));
/* 已确定是 V1.41；调用之前不要让系统音乐播放器同时占用设备 */
/* h1_pcm_stream_open(&stream, ring, 8192, 4096, 32000); */
```

实验流接口提供 `open/write/poll/pause/resume/close` 和 `queued` 待播样本数。
输入已是输出采样率的 s16 单声道，SDK 不隐式下混或重采样；write 返回接收量。
不反复追加短块，使用持久环避免 V1.41 内部追加计数增长。只清除已经消费的
区域，不覆盖未播放数据。暂停停止并取消引用、清空环；继续复用设备/描述符，
重新积累预缓冲；仅最终退出完整关闭。队列异常或欠载会取消并重新预缓冲。

固件没有已经确认的正式播放位置函数。流接口从实际 submit 实现的三条指令
解析队列地址，仅只读节点/游标，不写固件结构；签名不匹配不开启。这仍然是
**版本敏感的实验实现**，不能据此声称创造了稳定的厂商 API。调用方应频繁 poll，
间隔必须小于一圈（8192/32000=256 ms），完整错过一圈无法从模数游标恢复。
禁止两个实例同时占用音频服务；应用应对设备停滞设置超时并回退静音，不能
无限忙等 `queued`。约 4096 样本音频领先量可用于节奏控制。

## 时钟、JIT 与构建 ABI

PCM 开启后，软件毫秒计时可能落后于实际声音，80 Hz 时钟只有 12.5 ms 精度。
JZ4740 **没有 CP0 Count**。实验 `h1_profile_clock` 借用空闲非 PWM 的 TCU5，
RTC 输入 32768 Hz，约 30.52 µs 一 tick；严格检查占用，保存/恢复寄存器、
时钟停止状态和本通道 IRQ mask。必须零初始化并确保单拥有者，定期 read（<1 s），
否则无法恢复漏掉的周期；32 位累计 tick 自身也会回绕，用无符号差计算区间。
跨域读不稳定或无法解释的倒退会报告 fault，FULL 平台期不提前增加周期。
所有路径（包括失败和退出）都必须 stop；它不是通用系统资源分配器。

RTC 秒值为从 1970 起算的**本地日历**，不可再次加时区。无效时返回失败；
离线时间追赶和卡带虚拟时钟写入仍由模拟器负责。

固件 IRQ 可恢复固件自己的 `$gp`，JIT 后端不能把持久游戏状态放到 GP 中。
缓存同步仅能保证生成代码可见，不能解决寄存器约定冲突。SDK 采用 O32、
MIPS32 little-endian、soft-float、`-G0 -mno-abicalls -fno-pic`；代码用 KSEG0，
MMIO 用 KSEG1。MXU1 的指令支持与 JIT 后端仍需独立移植。

## 可复现构建与示例

Windows 的 GNU 15.2 工具链安装器固定 URL 和 SHA-256（下载源是第三方工具链
分发站，不是步步高官方 SDK）。LLVM 路径继续支持 `H1_LLVM_BIN`。

```powershell
python -m pip install -r requirements-dev.txt
python scripts/install_gnu_toolchain.py
$env:H1_GNU_BIN = (Resolve-Path .tools/toolchain/bin).Path
$env:SOURCE_DATE_EPOCH = '1791244800'
python -m h1_bda.build examples/native_game.c --runtime --title H1NativeDemo -o build/H1NativeDemo.bda
python -m pytest tests -q -p no:faulthandler
```

示例只显示 D/K 的两个状态块、播放原创方波，选文件只是演示选择器，并不读取或
运行游戏 ROM。触摸/ESC/返回/电源退出，正常路径关闭音频和原生窗口。

构建器识别 C/C++/汇编，GNU 链接正确 ABI 的 libgcc，并提供可替换的 weak
memcpy/memset/memmove/memcmp。`--runtime` 启用 128 KiB NOLOAD 私有栈，保存/恢复
调用方 GP/SP/S0/RA，运行 `.preinit_array/.init_array/.fini_array` 和 GNU
`.ctors/.dtors`。定义 `extern "C" int h1_app_main(void)`；不要另定义
`h1_bda_main`。入口独立成节并断言地址，避免后续 64 字节汇编对齐移动
`0x83C00020`。不提供完整 libc、异常、RTTI、线程局部变量或 C++ 标准库。
默认构建仍允许自定义入口；此时调用方负责启动和构造函数。

NAND 部署依赖外部 `h1_ftl.py/build_h1_system_nand.py`，可通过
`H1_EMULATOR_TOOLS` 指定目录；不再在导入 SDK 测试时强行读取作者相邻目录。
前端集成测试用 `H1_EMULATOR_FRONTEND` 指定 `h1_emulator.py`。缺少外部材料时
明确跳过相应集成测试，不计为已通过的 NAND/固件验证。

## 证据与验证层次

本次新增测试执行交叉编译后的 MIPS 二进制和模拟服务表，覆盖组合键/别名、
路径/GBK/取消、触摸成功与拒绝、RTC 闰日、行跨度/像素格式、PCM init/destroy
配对、环复用/欠载/失败清理、暂停不重开设备、TCU 占用/配置顺序/FULL/倒退/
恢复，以及混合 C++/汇编、libgcc 和构造函数。**这不是新的真机 SDK 验证。**
生成代码测试覆盖地址边界和修改后执行；Unicorn 不建模真机 D/I cache 一致性，
该测试不能单独证明物理缓存刷新正确。

既有 H1-GBA 测试、日志与实现来源：

- [原生游戏输入](https://github.com/HelloClyde/BBKH1-GBA/blob/v0.12.3/docs/h1-game-input.md)
- [选择器与图形模式](https://github.com/HelloClyde/BBKH1-GBA/blob/v0.12.3/docs/h1-file-selector.md)
- [PCM 音频](https://github.com/HelloClyde/BBKH1-GBA/blob/v0.12.3/docs/h1-audio.md)
- [性能计时与真机边界](https://github.com/HelloClyde/BBKH1-GBA/blob/v0.12.3/docs/h1-performance.md)
- [RTC](https://github.com/HelloClyde/BBKH1-GBA/blob/v0.12.3/docs/h1-gba-rtc.md)
- SDK 原有 [音频逆向记录](../reverse/docs/audio_api.md)

原版 BDA、固件镜像、商业 ROM 和个人真机日志不复制进 SDK。固件地址只是分析
证据，应用实际通过运行时服务表调用。本次不会把缩放卡顿、gpSP GP 使用错误
或 QEMU TCU 模型差异冒充 SDK 修复。
