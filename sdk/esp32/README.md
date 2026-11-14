# H1NET/1 · ESP32-S3 固件与工具

完整实现位于 `common/`、`esp32/`、`h1/`，SDK 封装在 `h1_esp32.c`。
当前固件版本为 **0.4.5**；本次导入不增加新闻、音乐或账号专用服务。

先读 [板型/接线](../../docs/esp32-hardware.md) 和 [H1 API](../../docs/esp32.md)。
固件提供 HTTP/HTTPS GET/POST、Range、gzip 解压、四任务有界接收、原始响应头、
Wi-Fi 扫描/设置、校时、通用 AAC 解码和 H1Debug UART 隧道。

## 构建

需要 ESP-IDF **5.5.6**，固定提交
`880a203ae603d08a94d06aa01e0d6761958e5e9e`，并进入其 Python/编译器环境。
`esp32/main/idf_component.yml` 和 `esp32/dependencies.lock` 固定 CDC Host **2.4.1**、
`esp_audio_codec` **2.6.2**。IDF 组件管理器负责下载，不能把浮动 master 当成相同版本。

SDK 根目录执行：

```sh
python sdk/esp32/tools/build_firmware.py --stage /tmp/h1-sdk-esp32 --jobs 2
```

Windows 的 stage 可用 `C:/Espressif/h1-sdk-esp32`。路径不能含空格；脚本只复制公开源文件，
不复制旧 `sdkconfig`、凭据、日志、构建目录或设备数据。每次从 defaults 配置。
编译并发默认 2，避免低内存机器上同时启动大量 Xtensa 编译器。

输出 `build/esp32/`：应用、bootloader、分区表、flasher 参数、生成的 sdkconfig 和 SHA256
清单。脚本确认 Hub、PSRAM 和 `MBEDTLS_EXTERNAL_MEM_ALLOC` 真正出现在生成配置中。
这些配置解决软件支持和内存问题，不证明任何 Type-C Hub 都可直接接在板端。

也可以在无空格目录中直接 `cd sdk/esp32/esp32 && idf.py set-target esp32s3 && idf.py build`，
但此方式的版本检查、输出清单与后续烧录验证需自行完成。

## 烧录和串口配网

烧录时断开 H1，仅 ESP 的 **USB2** 接电脑，先重新查询端口：

```sh
python -m serial.tools.list_ports
python sdk/esp32/tools/flash_firmware.py --port COM7 --all
python sdk/esp32/tools/h1_wifi_setup.py --port COM7 status
```

需要 `pyserial` 和 IDF 环境里的 `esptool`。首次 `--all` 写入 bootloader、分区表和应用；
后续省略 `--all` 默认仅更新应用。工具先验证 SHA256，再写入并读回校验，不执行整片擦除，
默认保留 NVS。改分区表之前自行确认原 NVS 布局；不是通用跨产品升级器。

正常用 [Wi-Fi设置 BDA](../../examples/esp32_wifi/README.md) 配网。
电脑也可 `h1_wifi_setup.py --port COM7 set`，通过交互输入名称和隐藏密码，不将密码放在
命令行参数或文件中。`clear` 会删除已保存的 Wi-Fi，调用方应确认操作意图。
端口示例 COM7 不代表固定端口，更不应把 H1 的旧 COM4 当作 ESP 串口。

## 开发参考后端和调试桥

`tools/h1_wifi_pc.py` 是同协议的 PC 参考实现，便于不用实体 ESP 时调试客户端，
不是日常使用所必需的服务器。安装 `pyserial` 后用 `--help` 查看参数；AAC 模拟额外需要
PyAV (`av`)。本目录不携带 PyAV 二进制或测试媒体。

`tools/h1_debug_esp.py` 提供兼容 serial 的 `ESPTransport`，电脑连接 ESP USB2，
H1 保持接在 ESP USB1 上。先在 H1 启动 H1Debug，再打开该 transport；关闭时发送
`DBG OFF`。它只透传 H1Debug 字节，不赋予同时运行两个 H1 USB 所有者的能力。
完整 H1Debug 文件部署客户端属于独立调试工具；这个模块可作为其 transport 插件。

桥协议为 `H1CFG/1 DBG ON/OFF/DATA` 和 `H1DBG/1 DATA/ACK/GONE`，详见模块和固件源码。
0.4.5使用UART0 TX43/RX44、921600 baud，主机工具用只读INFO自动识别921600/115200，兼容旧固件。
旧工具只支持115200，升级固件时应一起使用本版本工具（含h1_esp_serial.py）。普通H1原生USB的115200 CDC字段不限制USB线速。
固件批量读取UART，并通过EventGroup唤醒USB任务，去除桥接命令最多10ms的额外等待；主机批量读串口。
不启用未经验证的多RPC并发，H1Debug的序号、CRC、文件事务、读回和回滚保持原实现。

相同32KiB临时文件的物理对照：新建上传7.969→3.375秒，覆盖上传8.281→5.250秒；
下载两轮6.5→约1.7秒。另测256KiB上传（含提交校验）21.953秒、下载13.0秒。
全部上传/读回SHA256一致，文件已清理，H1调试器未更换。证据和限制见
[物理记录](../../docs/evidence/esp32-debug-speed-20261114.json)。
115200的8N1原始上限11520字节/秒，921600则为92160字节/秒；Base64、RPC往返和原生文件校验仍有开销，
不能把USB Full Speed 12Mbit/s当作桥接文件速度。未做本轮持续音乐播放验收。
不需要调试桥时不要开启它，H1NET 应用自己接管 CDC。

## 许可证与第三方边界

本项目原创 H1NET、USB、固件控制层和工具采用 Apache-2.0，经原著作权持有人另行授权；
不导入 GPL 游戏/模拟器 libc，复用 SDK 已有的 freestanding 内存函数。

- `third_party/zlib`：未修改的 zlib1.3.1 inflate 子集，保留其许可证和来源哈希。
- IDF 与 CDC 组件：由固定版本的组件管理器获取，保留各自许可证。
- `esp_audio_codec`2.6.2：**Espressif Modified MIT**，仅允许与乐鑫产品配合使用，
  部分解码实现以厂商库提供。不是本项目的 Apache 源码，也不能宣传为无条件跨芯片开源。
  [许可证副本](licenses/esp_audio_codec.txt) 与 [AAC API/资源说明](esp32/AUDIO_DEPENDENCY.md)。
- Wi-Fi样例的 H1WiFi16 点阵字体：GNU Unifont 子集，SIL OFL1.1，见样例 fonts目录。

本仓库不分发 BBK 固件、设备 RAM/NAND、用户 SSID/密码、网站 Cookie、商业歌曲或专辑图。
