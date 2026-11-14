# H1Music16

使用GNU Unifont16.0.04原始HEX，Roman Czyborra、Paul Hardy及Unifont贡献者；选择SIL OFL-1.1。
源文件、哈希、OFL及上游双许可保留。ASCII8×16、全角16×16；GB2312、JIS X0208、日文假名和全部11172个现代韩文音节。
22369个字形只选取/打包，不缩放、不修改像素形状。

```powershell
python tools/generate_font.py --japanese --korean --output src/font.h --metadata assets/font.json
```

原生输入另用src/ui/ime_table.h中的Unicode/GB2312映射，保留Noto来源/OFL，但不含该字体的像素数据。
