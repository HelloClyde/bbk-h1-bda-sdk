# Wi-Fi 设置 · ESP32 SDK 样例

保留原配网应用的浅灰/白卡片、蓝色按钮、Wi-Fi 桌面图标、触屏键盘和实体按键；
网络代码改用 `h1_esp32_*`，不再复制握手、能力协商或手工拼 Wi-Fi 消息。
这是本扩展的完整示例程序，支持已插线启动与自动连接检测。

```sh
python -m h1_bda.build examples/esp32_wifi/main.c examples/esp32_wifi/core.c \
  --esp32 --runtime --title WiFiSetup --icon-png examples/esp32_wifi/assets/wifi-settings.png \
  -o build/H1WiFiSetup.bda
```

中文桌面标题可将 `WiFiSetup` 换为 `Wi-Fi设置`。将完整 BDA 安装到 H1 的应用目录。
支持 H1/Y100 V1.41，配套固件0.4.0及以上提供配网能力，本包固件为0.4.4。
新 SDK 封装和此移植样例尚需单独真机验收；不把历史配网模型测试写成它的实机结果。

| 页面 | 操作 |
| --- | --- |
| 热点列表 | R扫描；上下/Enter选择；点击热点；右上上页/下页 |
| 隐藏网络 | H手动输入名称；开放/加密按钮选择认证 |
| 密码输入 | 实体键盘或触屏；Shift大小写；Fn数字；删除；显示/隐藏；Enter连接 |
| 网络状态 | S进入；C取消连接；F忘记网络（再次确认）；Esc返回 |
| 列表页 | Esc退出，释放 USB 所有权 |

自动扫描24个以内的2.4GHz热点，采用同代分页完成后整体更新列表。
输错密码、找不到热点、获取IP超时或保存失败不会直接替换原配置。
断线不自动重发连接/忘记操作；重连后读取状态，核实实际结果。
密码只保存在 ESP 的 NVS；H1 的编辑器和临时副本会擦除，不生成密码文件。

`main.c`：输入/状态机和SDK调用；`core.c`：密码编辑规则；`screen.h`/`style.h`：界面。
`compat.h` 只提供本样例所需的有限格式化和字符串操作，不是标准 C 库替代品。
触屏、原生绘图窗口和 framebuffer 均复用 SDK 公共接口的检查。

`font.h` 是 H1WiFi16 点阵字体：ASCII、GB2312和界面字符，8×16/16×16，无缩放。
未包含的 SSID 字符以方框显示，不改变真实 SSID 字节。
源文件 `fonts/H1WiFi16.hex`、OFL、上游声明和来源哈希一并保留，
运行 `python examples/esp32_wifi/generate_font.py` 可无损再生头文件。
图标由原项目代码生成，PNG/SVG 均包含；不依赖系统字体或在线图片服务。

更多：[通用 API](../../docs/esp32.md)、[板卡接线](../../docs/esp32-hardware.md)。
