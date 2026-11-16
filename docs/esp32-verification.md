# ESP32 扩展验证记录

## 0.4.5 构建与烧录教程补全

从公开主分支 `0b1e907a64f84f4f964b4dc6f5dc61e1dc60c05c` 重新检出源码，使用
本地 ESP-IDF 5.5.6 固定提交和空白 staging，按[教程](esp32.md#构建固件及检查产物)
执行 `build_firmware.py --jobs 2`，842 步完整构建成功。应用 1292752 字节，
3 MiB 应用分区余量约59%；esptool 4.13.0 对应用与 bootloader 的 checksum / hash 均验证有效。
源码、IDF、产物哈希与生成配置摘要见[构建证据](evidence/esp32-firmware-build.json)。

新增烧录工具 `--check`：不接设备，校验所有清单文件并输出 app-only / 首次安装的
烧录方案。对本轮真实产物的两种方案检查通过；5项主机测试验证默认仅应用、首次三镜像、
写后 verify、保留 NVS、不接受损坏镜像/未校验烧录配置、端口与 ROM 波特率参数。
测试中的 esptool 设备操作为模拟，不是新镜像的实体烧录。
CI 在固定 IDF 构建后也检查这两种方案，再上传完整固件 Artifact。

本轮只读 INFO 另确认现有实体 ESP 为 `h1-wifi/0.4.5`、UART921600，Wi-Fi 和 H1 USB
均已连接。未复位、未再次烧录、未改 NVS；**新构建镜像仍标记 hardware_verified=false**。
同版本此前实际 AppOnly 写入/校验及桥接文件传输的证据独立保留在
[物理测速记录](evidence/esp32-debug-speed-20261114.json)，不能把它改称本轮新镜像已刷入。
没有重复运行新闻/音乐的持续播放验收。

## 原始导入基线

日期：2026-11-14。SDK 基线：HelloClyde/bbk-h1-bda-sdk
`b34218548ecfa3dbc652e735352f76bff41b56c8`。

## 本次检查

| 范围 | 结果与边界 |
| --- | --- |
| SDK 回归 | `python -m pytest tests -q -p no:faulthandler`：44通过，5跳过。5项需要独立的外部模拟器工作区，不是本扩展失败或已验收 |
| H1NET 与新封装 | 实际 C wire/client/server/facade 联合执行11组场景；USB和网络边界为模拟，不是实体USB |
| 配网 UI | 原始UI/editor/点阵字形及SDK RGB565转换实际执行，模拟输入/时钟/framebuffer：触屏和按键编辑、密码清空、隐藏SSID、忘记操作二次确认，3帧输出，ASCII字形索引校验 |
| MIPS 构建 | 配网样例通过真实 little-endian MIPS32 编译/链接，使用SDK私有栈运行时；V2入口被拒绝 |
| ESP 固件 | IDF5.5.6固定提交、CDC2.4.1、codec2.6.2，完整构建成功；生成配置确认Hub、PSRAM、mbedTLS外部内存 |
| 烧录工具 | 镜像哈希错误时禁止调用esptool；默认只写应用，首次模式只写引导/分区/应用，均不执行NVS擦除；设备边界模拟 |
| 新包实机 | 未烧录新镜像，未把新SDK配网样例安装到H1；此范围尚未验收 |

11组C场景包括：自动检测和Wi-Fi状态变化；分片传输与idle重入保护；四任务交错与释放；
重复Set-Cookie完整保留及小缓冲失败清空；2048字节URL/16KiB POST/Range；旧代次任务
失效；HTTP403、网络失败及截断长度；超时POST不重放；CRC故障和缺失能力；离线配网；
同一对象重新初始化后的旧句柄隔离；RELEASE失败后的任务回收。

首次固件全量构建遇到IDF闲置LCD组件中的Xtensa GCC内部崩溃。降低并发后该错误仍存在，
没有把重试写成成功。最终启用IDF `MINIMAL_BUILD`，只构建main及明确依赖，822步构建完成。
SDK源码中的此配置和两进程默认值已提交。没有修改IDF或编译器源码。

构建应用镜像：1292448字节（`0x13b8a0`），3MiB应用分区余量约59%。
SHA256：`2aab495597315c8d026c3de40be6f5dd16de3523e334c45ac9b5107008ee2e30`。
完整构建在本地生成 `build/esp32/build.json`，含相对源码指纹及镜像哈希；不提交机器构建日志。

## 复用的历史实机事实

原项目在同一H1/Y100 V1.41、睿星S3 N16R8、USB1软线OTG直连、USB2另行供电的条件下：

- 新闻IT之家列表约204.8KB在用户观察的1～2秒内收到；不是所有网站/正文/图片的速度保证。
- 已验证附线启动的软件USB重枚举、ESP串口调试隧道、文件CRC回读。
- 固件0.4.4支持HTTP响应头，扫码/凭据仍由H1应用处理；微信登录恢复已观察到。
- QQ音乐0.1.9，用户对原先卡顿的曲目持续播放并返回列表翻页后确认“没卡”，韩文正常。
  该应用BDA SHA256：`5b8a4b5c81339d5ade1b2b78197d4870698548ccec31b861b0bb8200df326883`。

这些是导入通信核心的历史证据，不代表新封装、新最小依赖固件镜像或新配网样例已上机。
不为这次SDK整理重复运行旧的新闻/音乐矩阵，也不向SDK复制商业媒体、账号、RAM/NAND或原始串口记录。
导入来源指纹见 [provenance.json](../sdk/esp32/provenance.json)；除许可证头和明确的构建整理外，
通信核心保持原实现。上层封装及配网网络调度是本次新增代码，单独做上述检查。

## 可复现命令

```sh
python -m pip install -r requirements-dev.txt
# 配置H1_GNU_BIN；本机另设HOST_CC指向GCC，Linux默认cc。
python -m pytest tests -q -p no:faulthandler
python -m h1_bda.build examples/esp32_wifi/main.c examples/esp32_wifi/core.c \
  --runtime --esp32 --title WiFiSetup --icon-png examples/esp32_wifi/assets/wifi-settings.png \
  -o build/H1WiFiSetup.bda
# 进入固定IDF环境后：
python sdk/esp32/tools/build_firmware.py --stage /tmp/h1-sdk-esp32 --jobs 2
```

CI分别运行Windows GNU/MIPS回归、Linux主机协议/UI检查和固定IDF固件构建，上传完整BDA
及固件清单。CI不能证明实际接线、Hub CC、电源、射频、TLS持续并发或音频实时性能。
