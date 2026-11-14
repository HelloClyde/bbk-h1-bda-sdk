# 媒体组件来源

通用图片、AAC客户端、绘图和适配代码由本项目作者从H1工程提取，并按用户授权以Apache-2.0另行提供。
历史应用中的GPL声明不覆盖这里保留的第三方许可证；既有MIT PCM实现保持MIT许可。
没有QQ音乐业务、账号凭据、歌曲、封面或BBK固件转储。

| 源码 | 上游 / 许可 |
| --- | --- |
| third_party/ffmpeg-0.6.7 | https://ffmpeg.org/releases/ffmpeg-0.6.7.tar.bz2 ; LGPL-2.1-or-later配置，保留LICENSE/COPYING.*，config/ffmpeg.h未启用GPL/nonfree |
| third_party/musl-1.2.5 | https://musl.libc.org/releases/musl-1.2.5.tar.gz ; MIT及COPYRIGHT所列许可 |
| third_party/tlsf | Matthew Conte TLSF3.1，BSD-3-Clause，tlsf.h保留完整声明 |
| third_party/jpeg-9f | https://ijg.org/files/jpegsrc.v9f.tar.gz ; IJG，见README |
| third_party/libpng-1.6.43 | https://download.sourceforge.net/libpng/libpng-1.6.43.tar.gz ; libpng-2.0，见LICENSE |
| third_party/zlib-1.3.1 | https://zlib.net/fossils/zlib-1.3.1.tar.gz ; Zlib，见LICENSE |
| fonts | GNU Unifont16.0.04，选择SIL OFL-1.1；完整HEX/来源指纹/许可均保留，像素形状不变 |
| fonts/ime_table.h | 原生GB2312映射，历史Noto映射来源；保留Noto-OFL.txt，没有Noto字形像素 |

SDK根LICENSE不取代本表及逐文件许可。静态分发可选FFmpeg时应提供对应源码及可重新链接的构建方式。
