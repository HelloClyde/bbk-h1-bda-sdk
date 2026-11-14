# 可选媒体接口

这些模块从 H1 应用中提取，属于 SDK 的可选依赖。它们不包含 QQ音乐、新闻、账号或站点逻辑。
ESP32 仍仅负责通用网络和计算，不保存应用账号。

| 接口 | 用途 |
| --- | --- |
| `h1_image.h` | 有界 JPEG/PNG → RGB565，输入最多256KiB，输出最多480×272；成功返回0，输出由调用者用h1_image_free释放 |
| `h1_aac.h` | 调用者持有的AAC会话，通过RPC/时钟/取消/idle回调接入H1NET；支持分片、BUSY退避、PCM进展回调、计数和EOF |
| `h1_draw.h` | 调用方画布、stride、裁剪和字体；点阵字、矩形、圆角、圆点和线；没有隐藏帧缓冲 |
| `h1_font16.h` | 可选Unifont像素字库，含中文、日文和韩文；可自行传入更小的字体 |
| `h1_text_input.h` | V1.41原生文本输入，UTF-8正文、GBK标题；调用前释放原生绘图上下文/定时器 |
| `h1_media_audio.h` | V1.41 PCM环和严格已消费样本时钟；保留既有gba_audio/h1ns兼容符号 |
| `h1_media_runtime.h` | 可选musl/TLSF适配，堆、stdio边界及诊断；调用者提供h1ns_time_ms与h1ns_panic |

```c
#include "h1_draw.h"
#include "h1_font16.h"
uint16_t pixels[480*272];
h1_canvas screen={pixels,480,272,480,0,272,&h1_font16_cjk};
/* 在应用运行时： */
/* h1_draw_text(&screen,"你好 / 日本語 / 한국어",12,12,468,0xffff); */
```

`h1_aac_init`必须先于其他AAC调用。诊断可用会话内置存储或调用者提供的16个word。
open返回1成功/0失败，其他操作0或负错误；eof为1完成/0未完成/负错误。
READ输出S16LE、32768Hz单声道，缓冲容量为偶数；PUSH接受原始AAC access unit，不能传ADTS/M4A整个文件。
BUSY(-102)表示当前分片未消费，可以重试；进展回调只允许READ，不能重入PUSH/reset/close。
H1NET固件当前仅有一个解码器；两个C对象不会共享状态，但不意味着硬件能同时解两首歌。

构建从完整Git检出使用 `from h1_bda import media`：`media.libraries()`构建可重现的归档；
`media.sources()`列出通用适配和原生USB源，`media.compile_group(name,sources,flags)`编译调用方及SDK代码。
`media.ROOT/'runtime/h1.ld'`是对应的可选BDA链接布局；它和基础`--runtime`入口互斥。
设置`H1_GNU_BIN`为GNU15.2工具链目录，`H1_MEDIA_BUILD`为调用方构建缓存目录（导入前设置）。
完整可执行构建示例见`scripts/build_media_probe.py`。轻量应用可只编译draw.c或images.c，不必链接FFmpeg/libc全集。

库和模块都按需求参与链接，未使用的段会被剔除；标准SDK基本构建不自动引入媒体依赖。
FFmpeg配置包含MOV/M4A解复用、AAC和H264解码/解析；未引用的段在链接时剔除。QQ音乐在H1端仅解复用，AAC由ESP处理。源码保留上游原始许可，使用此可选模块需遵守LGPL等第三方条款。
来源和许可证见`sdk/media/THIRD_PARTY.md`。字体再生：
`python sdk/media/generate_font.py --japanese --korean`。字库原始像素保留，不依赖商业字体。

`h1_mkdir`创建单级GBK目录，父目录必须存在；只调用识别的V1.41原生入口。
mkdir返回0成功；目录已存在也可能返回非零，写入后仍应读回验证，不能据此判断文件已保存。
本次新增媒体接口经过主机协议/图片/绘图检查和应用MIPS模型检查；不将模型结果称作实体试听通过。
