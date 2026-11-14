# ESP32-S3 选型、接线与兼容性

## 已用板型

本项目使用睿星双 Type-C **ESP32-S3 N16R8** 核心板：16 MiB Flash、8 MiB 八线 PSRAM。
它提供 S3 原生 USB OTG 和独立 CH340K USB-UART。需要 USB Host 的 S3，不应直接购买
没有原生 USB Host 的传统 ESP32 板来替代；不同 S3 板的供电电路也不一定兼容本配置。

板卡资料：[厂家资料仓库](https://gitee.com/jia5761193/ruixing_esp32s3_core_board)，
核对版本 `383a09bfc56b3909318074415da18460f7b2a8fe`。本 SDK 不重新发布厂家的原理图或固件。

正面朝向你、天线向上、USB 向下时：

| 位置 | 用途 |
| --- | --- |
| 左 USB1 | S3 原生 OTG，D+ GPIO20、D− GPIO19，运行 Host 固件后连接 H1 |
| 右 USB2 | CH340K，电脑烧录/诊断或独立 5V 供电 |
| 左按钮 RST | 复位 |
| 右按钮 BOOT | GPIO0，复位/上电时拉低用于 ROM 下载模式；不是运行时开启 Host 的按钮 |

固件默认设置针对 N16R8：Flash DIO/40MHz、PSRAM OCT/40MHz、CPU240MHz。
其他容量/PSRAM 类型先修改 defaults 和分区表并重新验证；不能保证其他板通用烧录。
USB Host、Wi-Fi、TLS 和音频一起运行时内部 RAM 紧张，PSRAM 对本方案很重要。

## 正常接线

```text
独立5V电源或电脑 ─ USB-A/USB-C数据或供电线 ─ ESP右USB2
                                                    │
H1 Mini-USB ─ 原H1数据线(USB-A公) ─ 软线OTG(A母/C公) ─ ESP左USB1
                                                    │
                                              2.4GHz路由器 ─ 网站
```

量产/长期使用可以用充电头或充电宝供电；电脑只在开发和读取日志时使用。
选择有数据通路的 Type-C 公→USB-A 母 **带软线 OTG**，再接 H1 的 USB-A→Mini-USB 数据线。
硬质 OTG 外壳可能与右侧供电插头互顶；“能充电”的线不一定有 USB 数据。
不需要为 H1 USB2 速度购买 USB3 高速线。

这块板的 **J-OTG 默认可能断开**；请按实际板版号确认，由懂硬件的人完成所需供电配置。
本项目的商家已短接，要求是“USB2 供电时 USB1 有 5V 输出”。排针朝下与这个跳线无关。
H1 可能同时充电，电源须能承担两端负载，项目采用有足够余量的 5V/2A 电源配置。
不要在未确认隔离电路的情况下再给同一链路并接另一路 VBUS，尤其是带供电的 Hub。

## 为什么 Hub 支持仍可能连不上

固件已启用 `CONFIG_USB_HOST_HUBS_SUPPORTED=y`，负责 USB Hub 的协议处理。
Type-C **CC1/CC2** 用来判定连接方向与供电/数据角色；这个板级问题不由 Hub 软件选项解决。
传统 Type-C→USB-A OTG 与原生 Type-C Hub 上行的角色检测方式不同，前者可用不能证明后者可用。

实测：所用软线 OTG 直连已完成 H1 新闻下载及音乐持续播放；早先的 Type-C Hub 上行
直插未完成枚举。翻转插头、独立电源、空载 Hub 均没有把该 Hub 方案验收通过。
这证明特定组合存在兼容性问题，**没有证明 Hub 损坏，也没有测定唯一根因**。

资料 PDF 把两 USB 口的 CC 标为 No Connect，但 PCB/实物有版本和元件编号差异；
实物 R1/R2 等元件已焊接且无法从照片读清阻值，当时没有万用表。不能把 PDF 的 NC
直接当作实物测量结论，也不能凭空断言实物装了 Rd/Rp。优先使用已通过的软线 OTG
直连；要用 Hub，应取得准确版原理图、核实 CC 主机角色、VBUS、供电预算及 USB2 Hub兼容性。

参考：[乐鑫 Type-C 硬件指南](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_typec_hardware_guide.html)。

## 诊断顺序

1. 确认 USB2 电源、PSRAM 启动日志和 Wi-Fi IP；串口号每次重新枚举，不硬编码历史 COM 号。
2. 确认 USB1 Host 和 H1 CDC 枚举日志。没有连接事件时先检查线材/接头/CC/VBUS，
   不能用新闻下载超时推断网站故障。
3. CDC 连接后确认 DTR、HELLO、能力位、STATUS，然后才请求 HTTP。
4. 记录 USB epoch、RPC 错误、HTTP状态和 worker 错误；不要只显示同一个“网络错误”。
5. 软件重连和单一所有者适用于正常已插线启动；所有权保护失败不能通过忽略检查来绕过。

烧录只接 USB2、断开 H1；需要 ROM 下载时使用 BOOT+RST。BOOT 不会修复 CC 电路。
