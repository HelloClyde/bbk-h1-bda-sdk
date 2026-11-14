# ESP32-S3 通用联网扩展

本扩展将 H1/Y100 V1.41 的原生 USB CDC、H1NET/1、ESP32-S3 Wi-Fi/HTTP 固件
和配网样例一起放入 SDK。日常联网由 ESP 直接访问网站，不需要电脑或 NAS 常驻。
H1 是 USB **设备**，ESP 是 USB **主机**；这不是 USB 网卡驱动，也不是 TCP/IP socket API。

入口：[h1_esp32.h](../sdk/include/h1_esp32.h)、
[Wi-Fi 设置样例](../examples/esp32_wifi/README.md)、
[选型与接线](esp32-hardware.md)、[固件构建](../sdk/esp32/README.md)、
[完整协议](../sdk/esp32/PROTOCOL.md)、[验证边界](esp32-verification.md)。

## 构建和最小用法

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
