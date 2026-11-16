# ESP32-S3 通用联网扩展

本扩展将 H1/Y100 V1.41 的原生 USB CDC、H1NET/1、ESP32-S3 Wi-Fi/HTTP 固件
和配网样例一起放入 SDK。日常联网由 ESP 直接访问网站，不需要电脑或 NAS 常驻。
H1 是 USB **设备**，ESP 是 USB **主机**；这不是 USB 网卡驱动，也不是 TCP/IP socket API。

入口：[h1_esp32.h](../sdk/include/h1_esp32.h)、
[Wi-Fi 设置样例](../examples/esp32_wifi/README.md)、
[选型与接线](esp32-hardware.md)、[固件构建](../sdk/esp32/README.md)、
[完整协议](../sdk/esp32/PROTOCOL.md)、[验证边界](esp32-verification.md)。

## ESP32 固件源码在哪里

**固件源码已随 SDK 公开**，当前版本 **0.4.5**，不需要新闻或 QQ音乐工程才能构建。
ESP 提供通用网络能力；扫码、网站账号和业务配置由 H1 应用负责。

| 路径 | 内容 |
| --- | --- |
| `sdk/esp32/esp32/main/` | ESP32-S3 的 USB Host、Wi-Fi、HTTP/HTTPS、校时、AAC 和调试隧道实现 |
| `sdk/esp32/common/` | H1NET/1 协议与公共通信代码 |
| `sdk/esp32/esp32/sdkconfig.defaults`、`partitions.csv` | 板型、内存、串口、Hub 和分区配置 |
| `sdk/esp32/esp32/main/idf_component.yml`、`sdk/esp32/esp32/dependencies.lock` | 固定版本的外部组件与依赖锁 |
| `sdk/esp32/tools/build_firmware.py`、`flash_firmware.py` | 构建、产物清单、烧录前检查、写入与读回校验 |
| `.github/workflows/esp32.yml` | 固定 IDF 容器构建和固件 Artifact |

以下所有 `python sdk/...` 命令均在 **H1 SDK 仓库根目录**执行。只构建/烧录 ESP 固件
不需要 H1 的 MIPS 工具链；生成 H1 的 `.bda` 应用才需要 MIPS 工具链。

## 从零安装 ESP32-S3 构建环境

### 1. 核对板型和依赖版本

默认支持已经实测的睿星双 Type-C **ESP32-S3 N16R8**：16 MiB Flash、8 MiB 八线 PSRAM。
默认 Flash DIO/40 MHz，PSRAM OCT/40 MHz，UART0 TX43/RX44。其他 S3 板应先核对
Flash、PSRAM 类型、引脚和 USB1 供电电路，不能仅因为名称含 S3 就直接套用。
选型、J-OTG、软线 OTG 与 CC/Hub 限制见[硬件说明](esp32-hardware.md)。

| 依赖 | 本工程要求 |
| --- | --- |
| ESP-IDF | **v5.5.6**，提交 `880a203ae603d08a94d06aa01e0d6761958e5e9e` |
| 目标芯片 | `esp32s3`，不支持用传统 ESP32/C3 的镜像替代 |
| USB CDC Host 组件 | `espressif/usb_host_cdc_acm` **2.4.1** |
| 音频组件 | `espressif/esp_audio_codec` **2.6.2**，许可证见[固件说明](../sdk/esp32/README.md#许可证与第三方边界) |
| Python、Xtensa、CMake、Ninja、esptool | 使用上述 IDF 安装/激活的环境；本地验证 Python 3.11、esptool 4.13.0 |

保留已提交的组件锁文件，让 IDF 组件管理器下载依赖。首次构建需要访问 GitHub、
乐鑫工具下载站和组件仓库，源码仓库没有把这些工具和厂商二进制重新打包进去。

### 2. Windows 安装和激活

1. 安装 Git。到[乐鑫 EIM 官方下载/安装说明](https://docs.espressif.com/projects/idf-im-ui/en/latest/)
   安装 ESP-IDF Installation Manager，**显式选择 ESP-IDF 5.5.6 和 ESP32-S3**。
   不要使用安装器默认的最新 IDF 替代本工程锁定版本。
2. IDF、工具及 staging 路径使用不含空格/中文的短路径，例如 `C:\Espressif`。
   SDK 仓库可以在带空格的工作目录中；构建脚本会复制公开源文件到独立 staging 再编译。
3. 打开安装器为 **5.5.6** 生成的 ESP-IDF PowerShell 终端。普通 PowerShell 没有自动激活
   Xtensa 和 Python 环境。本项目实测安装器生成的激活文件如下；你的路径以安装结果为准：

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.6.PowerShell_profile.ps1'
python --version
idf.py --version
python -c "import sys, esptool, serial; print(sys.executable); print(esptool.__version__)"
git -C "$env:IDF_PATH" rev-parse HEAD
```

最后两项应指向 IDF 的 Python 环境并输出上表提交。若提示 `IDF_PATH` 不存在、找不到
`idf.py`、`esptool` 或 `serial`，先重新激活/修复这套 IDF 环境，不要改用系统的另一个 Python。
此处的 profile 路径是实测示例，并非仓库附带的文件。

在同一终端检出 SDK（已经有仓库的可直接进入）：

```powershell
New-Item -ItemType Directory -Force C:\src | Out-Null
git clone https://github.com/HelloClyde/bbk-h1-bda-sdk.git C:\src\bbk-h1-bda-sdk
Set-Location C:\src\bbk-h1-bda-sdk
```

### 3. Linux / macOS 环境

按[乐鑫 5.5.6 安装指南](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/get-started/linux-macos-setup.html)
安装系统依赖，再在无空格目录执行：

```sh
git clone --branch v5.5.6 --recursive https://github.com/espressif/esp-idf.git "$HOME/esp-idf-v5.5.6"
cd "$HOME/esp-idf-v5.5.6"
./install.sh esp32s3
. ./export.sh
git rev-parse HEAD
# 应为 880a203ae603d08a94d06aa01e0d6761958e5e9e
# 然后 cd 到另行检出的 bbk-h1-bda-sdk 仓库根目录
```

每个新终端都需要激活 IDF 环境。若 v5.5.6 检出的提交与上述值不同，应先核对来源；
构建脚本会拒绝不匹配的 IDF，而不会继续编出被误标为同一版本的固件。

## 构建固件及检查产物

Windows（已激活 IDF 的 PowerShell、SDK 根目录）：

```powershell
python sdk/esp32/tools/build_firmware.py --stage "$env:TEMP/h1-sdk-esp32" --jobs 2
```

如果 Windows 用户名/临时目录包含空格，改用可写的无空格目录，例如
`--stage C:/Espressif/h1-sdk-esp32`。Linux/macOS：

```sh
python sdk/esp32/tools/build_firmware.py --stage /tmp/h1-sdk-esp32 --jobs 2
```

脚本核对 IDF 提交、设置 `esp32s3`，从 `sdkconfig.defaults` 生成配置，下载锁定组件并构建。
默认两进程限制内存使用。它不导入旧工程的 `sdkconfig`、密码或日志；修改构建配置应改
仓库里的 defaults，再运行脚本，不要只改 staging 的 menuconfig。

成功时结尾显示 `Built and hashed firmware:`，SDK 根目录产生 `build/esp32/`：

| 文件 | 默认烧录地址 / 用途 |
| --- | --- |
| `bootloader/bootloader.bin` | `0x0`，首次安装的引导程序 |
| `partition_table/partition-table.bin` | `0x8000`，首次安装的分区表 |
| `h1_wifi.bin` | `0x10000`，应用程序 |
| `flasher_args.json` | 构建器生成的地址、镜像名和 Flash 参数 |
| `config/sdkconfig.json`、`config/sdkconfig.h` | 实际生效的配置 |
| `build.json` | 版本、目标、IDF 提交、公开源码指纹及产物 SHA256 |

地址表仅解释本工程当前分区；烧录工具实际读取并校验生成的 `flasher_args.json`。
工具检查 Hub、PSRAM、TLS 外部内存和 UART0 TX43/RX44/921600 的生成配置，缺少任一项
不会发布产物。`build.json` 中 `hardware_verified=false` 是正常的：编译成功不能自动变成实机验收。

先做不需要接板、不调用 esptool 的离线检查：

```sh
python sdk/esp32/tools/flash_firmware.py --check
python sdk/esp32/tools/flash_firmware.py --check --all
```

两条命令都须成功，分别列出 app-only 的一个镜像和首次安装的三个镜像。哈希不符、
烧录配置不在哈希清单内或缺少目标镜像会直接失败。

### 不自己编译时：使用 CI 固件

进入 [GitHub Actions → ESP32 SDK](https://github.com/HelloClyde/bbk-h1-bda-sdk/actions/workflows/esp32.yml)，
选择目标提交且 `firmware` 成功的运行，下载 **ESP32-S3-firmware** Artifact
（GitHub 可能要求登录）。解压并找到含 `build.json` 的目录，保留全部子目录。
使用同一提交的工具，给下面所有检查/烧录命令追加
`--images C:/path/to/extracted-firmware`；不要只拿 `h1_wifi.bin` 也不要混用其他构建的引导/分区文件。
CI 每次实际编译，并对两种烧录方案执行 `--check`；没有连接实体 ESP，不能代替硬件验收。

## 烧录到 ESP32-S3

### 1. 只连接右侧 USB2，重新确认串口

先把 H1 从 ESP 的 USB1 断开。正面朝你、天线向上、USB 向下时：**右 USB2** 通过
有数据通路的线接电脑，左 USB1 留空。充电宝/仅供电线无法提供电脑烧录通道。
关闭占用 ESP 串口的日志终端、配网工具和 H1Debug 桥接客户端。

```sh
python -m serial.tools.list_ports -v
```

Windows 在设备管理器「端口 (COM 和 LPT)」中核对 CH340K；没有端口时检查数据线，
并按板商说明安装 USB-UART 驱动。记下实际端口，下文 `COM7` 全部换成你的值；
它可能变化，不要照抄旧 H1 的 COM4。Linux 常见 `/dev/ttyUSB0`，macOS 常见
`/dev/cu.usbserial-*`，同样以列举结果为准。

### 2. 首次安装 / 从其他固件切换

确认是上述 N16R8 板和当前分区布局后，在 IDF 终端、SDK 根目录执行：

```sh
python sdk/esp32/tools/flash_firmware.py --port COM7 --all
```

写入 bootloader、分区表和应用，然后执行 **verify_flash 读回校验**。脚本默认 ROM 下载
波特率为 **115200**，可用 `--baud 460800` 提高烧录速度；遇到线材/串口稳定性问题使用默认值。
ROM 烧录速率与运行中 **921600** 的日志/配网速率是两回事。

当前分区中 NVS 从 `0x9000` 开始；脚本不执行整片擦除，也不写入 NVS 镜像。
但从其他产品切换时，旧 NVS 布局/格式未必兼容，不能保证保留其配置。

### 3. 已安装本工程后的常规升级

相同硬件且 bootloader/分区表不变时，默认只更新应用，保留已配 Wi-Fi：

```sh
python sdk/esp32/tools/flash_firmware.py --port COM7
```

工具不会自动判断旧板上的分区是否相同。首次装机不能省略 `--all`；变更分区表时
需要重新评估原有数据，再按首次流程处理。常规升级不需要 `erase_flash`。

### 4. 卡在 Connecting 时：BOOT + RST

大多数板可由 USB-UART 的 DTR/RTS 自动进入下载模式；失败时：

1. 保持右 USB2 接电脑，**按住右侧 BOOT**。
2. 短按一次左侧 **RST**，再松开 RST。
3. 松开 BOOT，重新执行上面的烧录命令；若自动复位仍打断下载模式，按住 BOOT
   启动命令，等终端已识别芯片/开始上传 stub 后再松开。
4. 写入和读回校验都成功后，确保 BOOT 已松开；未自动运行就短按一次 RST。

BOOT 是复位瞬间把 GPIO0 拉低以选择 ROM 下载模式，不是开启 OTG 的按钮，也不修复
CC 或 Hub 兼容性。原理见[乐鑫启动模式说明](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html)。

## 刷完后的验收与配网

先不接 H1，等 ESP 启动后运行：

```sh
python sdk/esp32/tools/h1_wifi_setup.py --port COM7 status
```

工具只读 INFO，自动探测 **921600 / 115200**，不切换复位引脚。应看到
`target: esp32s3`、`firmware: h1-wifi/0.4.5`、`ok: true`。
此时 `usb_connected: false` 正常，因为 H1 尚未接入；新装机未配 Wi-Fi 时
`wifi_connected: false` 也正常，这不代表刷入失败。

随后可在电脑执行交互配网（密码隐藏输入，不写进命令行）：

```sh
python sdk/esp32/tools/h1_wifi_setup.py --port COM7 set
# 按提示输入 2.4GHz Wi-Fi 名称和密码；等待几秒后再查询
python sdk/esp32/tools/h1_wifi_setup.py --port COM7 status
```

也可直接在 H1 的 **Wi-Fi设置**（`H1WiFiSetup.bda`）扫描、输入密码和连接：

1. 保持 USB2 由电脑或独立 5V 电源供电。
2. 左 USB1 接软线 OTG，OTG 接 H1 的 USB-A→Mini-USB 数据线。
3. 打开 H1「Wi-Fi设置」，如果系统问「是否连接USB」选「否」，不进入磁盘模式。
4. 观察外设连接，再连接 2.4GHz Wi-Fi；取得 IP 后才能认定联网成功。
5. 再打开新闻等应用做一次真实网络请求，区分「镜像写入成功」「USB 已枚举」和「网站访问成功」。

如果要看原始启动日志，关闭上述工具后可用
`python -m serial.tools.miniterm COM7 921600`（Ctrl+] 退出）。不能同时用两个程序占串口。
`status` 含当前网络信息，分享故障记录前应遮去个人 SSID/IP，密码和网站凭据不要放入仓库。

## 常见故障

| 现象 | 检查 / 处理 |
| --- | --- |
| IDF 版本拒绝 / `IDF_PATH` 不存在 | 激活 5.5.6 环境，核对精确提交；不要删掉脚本的版本校验 |
| staging 路径含空格 | 换无空格路径，源码仓库不用搬迁 |
| 组件下载失败 | 恢复 GitHub/乐鑫下载和组件站访问；保留锁文件，不用随机改版本解决 |
| 编译器内存不足 | 用 `--jobs 1`；保留项目 `MINIMAL_BUILD`，不把无关 LCD/相机栈加进来 |
| 找不到串口 / Access denied | 核对 USB2、数据线、CH340 驱动、实际 COM 号；退出所有串口占用程序 |
| Connecting 超时 | 使用 BOOT+RST；确认接的是 ESP USB2，不是 H1 串口 |
| 写入或 verify 失败 | 先解决供电/数据线/端口，使用默认 `--baud 115200`；未校验成功不要当成已刷好 |
| 启动日志乱码 / INFO 无响应 | 使用同一提交的新工具；0.4.5 的 console 为 921600，旧工具仅支持 115200 |
| `usb_connected=false` | H1 未接或未被枚举；检查软 OTG、J-OTG/VBUS和单一 USB 所有者，充电图标不是枚举证据 |
| Wi-Fi 已连但 HTTPS 失败 | 核对校时、DNS、站点状态和 PSRAM/TLS 配置；固件写入校验不证明网站可访问 |

当前构建/烧录工具的验证及实机证据见[验证边界](esp32-verification.md)。

## H1 应用构建和最小用法

在 SDK 根目录配置好原有 MIPS 工具链后：

```sh
python -m h1_bda.build examples/esp32_wifi/main.c examples/esp32_wifi/core.c \
  --esp32 --runtime --title WiFiSetup \
  --icon-png examples/esp32_wifi/assets/wifi-settings.png -o build/H1WiFiSetup.bda
```

`--esp32` 自动加入通信源文件；`--runtime` 提供私有栈和标准应用入口，SDK已有内存复制/填充回退；不需要复制应用的网络代码。
普通应用不用该选项就不会链接本扩展。V2 的链接地址被明确拒绝，不能把 V1.41 的
固定地址 USB 驱动用于未验证的其他固件。

```c
#include "h1_sdk.h"
static h1_esp32 esp;
static unsigned char body[8192];
static void idle(void *context) {
    /* 处理本应用的按键、绘制，以及本地音频补给；不能重入同一个 esp 会话。 */
    (void)context;
}
/* 应用初始化： */
/* int error = h1_esp32_open(&esp, idle, 0); */
/* 每帧： */
/* enum h1_esp32_state state = h1_esp32_poll(&esp); */
/* 当 state == H1_ESP32_READY，用户请求后： */
/* unsigned length; */
/* error = h1_esp32_get(&esp, "https://example.com/", body, sizeof body, &length); */
/* body 是 length 字节二进制数据，SDK 不额外写入 NUL。 */
/* 退出应用：h1_esp32_close(&esp); */
```

`open` 默认支持已插线启动：校验固件，向可识别的 H1Debug 请求停止，等待其主动释放，
然后软件断开并重新枚举。正常场景不用再次手动拔插。未知后台任务或所有权争用会
返回 `OWNER_BUSY`，不会强杀任务。应用仍需要独占 USB，不能同时保持磁盘模式。
主机未枚举或未设置 DTR 时保持 `WAIT_USB`，不会仅凭有 5V 就报告已联网。

## 调度与状态

`poll` 每次只执行一步有限的字节传输/协议解析，不等待、不调用 `idle`。
它自动完成 HELLO、能力协商和 STATUS：

| 状态 | 含义 |
| --- | --- |
| WAIT_USB | CDC 尚未连接；包括未枚举或 DTR 未设置 |
| CHECKING | 正在握手，尚未判断 Wi-Fi |
| WIFI_OFFLINE | ESP 已连接，可以配网，但尚未联网 |
| READY | ESP 报告已取得 Wi-Fi 连接 |
| DEVICE_ERROR | 握手或通信失败，空闲时自动退避后重新检测 |

连接状态每秒更新；握手失败退避三秒。应用每帧调用 `poll`，不需要用户按 H/R 才更新。
除 `poll` 外的 API 会等待单次有超时的 RPC，等待期间调用 `idle`，长请求分片也如此。
`get` 是同步便捷函数。对帧时间有严格要求时，可在现有 `h1net_rpc_begin/poll`
基础上调度，或按流式 API 分段处理，不能把 `get` 当作完全异步调用。

一个会话只允许一个正在收发的 RPC。后台状态探测期间调用其他 API，返回 `AGAIN`；
下一帧继续 `poll` 后重试即可。`idle` 只负责本地 UI/音频，不得再次对同一个会话发 RPC，
也不得关闭它。多个 HTTP job 可交错读取，RPC 本身依次执行。需要更复杂的 HTTP/AAC
交错调度时，调用在上一次 API 返回之后进行。

## HTTP 流、响应头和错误

```c
h1_esp32_stream stream = {0};
h1_esp32_request request = {
    .url = "https://example.com/api", .method = "POST",
    .headers = "Content-Type: application/json", .body = "{}", .body_length = 2,
    .max_bytes = 256*1024, .timeout_ms = 30000
};
/* open 返回 AGAIN：尚未提交，继续 poll 后再调用。
 * open 成功后，保存 stream 到后续帧。 */
int error = h1_esp32_http_open(&esp, &stream, &request);
/* 每次读取最多返回 500 字节。正数=数据，0=已验证EOF，AGAIN=暂时没数据。 */
/* int n = h1_esp32_http_read(&esp, &stream, body, sizeof body); */
/* 获取 HTTP 状态：stream.info.http_status。退出/取消/错误后都要 close。 */
/* h1_esp32_http_close(&esp, &stream); */
```

流 API 返回原始 HTTP 状态；403/404 也是有效的 HTTP 响应，由应用决定如何显示。
便捷 `get` 则要求 2xx。已知长度必须在 EOF 匹配；读到部分内容后网络失败不能冒充成功。
`close` 释放任务，未读完也能释放；失败时保留 handle 供调用方处理，`AGAIN` 后稍后再关闭。
便捷 `get` 无法返回活动句柄；若其清理失败，会使当前会话代次失效，下次握手回收残留任务，
其他旧流也需丢弃。错误原因仍保留，不重发原请求。
不要直接把含有效 job 的 stream 清零或拿它再次 open。SDK 不为流暗中分配大缓冲。

`h1_esp32_http_range` 支持校验过的 206 Range。URL 最长 2048 字节；请求头最长 1024，
请求体最长 16 KiB，单任务正文上限 8 MiB，四个 HTTP 任务各使用 ESP 8 KiB 环形缓冲。
长音频应分段请求并预取，不能靠把单次响应上限无限增大。

`h1_esp32_http_headers` 返回完整 CRLF 字段，保留多个 `Set-Cookie`；含 NUL 最多需 8193
字节缓冲。空间不足返回 LIMIT，并清掉输出，不返回被截断的授权字段。
请求模式不自动重定向；应用决定目标及是否附带认证。Host、长度、连接和编码等协议头
由后端管理，不能用自定义字段覆盖。不要把原始请求头、响应头或密码写入调试日志。

| 字段/返回值 | 应用处理 |
| --- | --- |
| AGAIN | 本地忙或暂未收到数据，继续事件循环 |
| STALE | USB 代次已变化，旧 stream 丢弃；不发送旧 job 的 RELEASE |
| `error.code` | USB I/O、RPC 超时、帧校验、参数/缓冲等本地结果 |
| `error.remote_status` | ESP 的 INVALID/BUSY/NOT_CONNECTED 等 RPC 拒绝原因 |
| `error.backend_error` | 网络、响应大小、网络超时等 HTTP worker 错误 |
| `error.http_status` | 站点返回的 HTTP 状态 |

USB 重连或不可恢复的 RPC 故障使旧代次任务失效。SDK 会重新握手，**不会自动重放 POST、
连接 Wi-Fi、忘记网络或其他结果不确定的操作**。重新显示状态后，由应用明确发起新请求。
HTTP_CANCEL/RELEASE 和底层 H1NET 接口仍可用于高级调用；不要绕过上层会话的忙保护。

## 扫描、连接和凭据

`wifi_scan` 返回 scan_id 后立即结束，扫描在 ESP worker 中执行；用 `wifi_status`
查询扫描结果，再用 `wifi_page` 分页读取，同一次列表最多 24 个热点。JOIN 同样异步，
`wifi_connect` 返回 operation，再观察其 outcome。密码正确且取得 IP 后才替换并保存 NVS；
失败保留旧凭据。`reuse_saved=1` 时传空密码，不能同时传新密码。

调用 `wifi_forget` 前的确认界面由应用提供，配网样例已经实现。
密码参数由应用保管并在不用时擦除；SDK 会擦除自己的请求临时副本，ESP 不回传旧密码。
支持 2.4GHz、开放和 Personal 系列认证；不支持企业认证、WEP、网页门户、WPS。
NVS 中的 Wi-Fi 凭据并未因此自动加密，需要相应安全需求的产品自行配置 NVS 加密。

QQ/微信扫码、Cookie、OAuth、站点签名、登录刷新及账号保存属于 **H1 应用**。
ESP 固件只提供通用网络、时间、Wi-Fi、可选 AAC 计算及调试隧道，不保存网站账号。

## 从实测得到的约束

- 5V/充电不等于 USB 枚举成功；Hub 软件支持也不代表板端具备正确 Type-C CC 主机角色。
- TLS 内存使用 PSRAM；仅启用 PSRAM 而让 mbedTLS 继续使用内部 RAM，会在并行请求时失败。
- HTTP job 释放后，尚在网络 I/O 中的 worker 仍须持有自己的内存，退出后才能复用槽位。
- 每个完整 RPC 后处理 UI/播放任务；不要在解析器、传输回调内重入 RPC。
- 读取环为空时让出控制；重用 Range 流并预取，避免每几百字节重建 TLS。
- 音频欠载不能靠墙钟伪造播放进度；恢复本地 PCM 队列，不应每次欠载都重新下载或重置 AAC。
- 调试串口桥0.4.5使用UART921600（旧版115200）+ Base64 + ACK，主机自动识别速率，与 USB Full Speed 的 12 Mbit/s 原始线速不同。

接口和新配网样例的当前验证范围见[验证记录](esp32-verification.md)，不要将模型测试
写成新版本的实机验收。

0.4.5桥接文件提速的实体对照和验证范围见[记录](evidence/esp32-debug-speed-20261114.json)。
