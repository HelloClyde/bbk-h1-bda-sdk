# H1NET/1

SDK导入基线为固件0.4.4；下文按扩展引入版本保留协议说明。当前应逐位检查能力，
通常HTTP/Wi-Fi/响应头具备时为59，AAC初始化成功后为63。历史验收边界及本次验证
见[验证记录](../../docs/esp32-verification.md)。

## Wi-Fi设置扩展（0.4.0）

操作 22～27 使用能力位 `H1NET_CAP_WIFI_CONFIG=16`，客户端先 HELLO/CAPABILITIES。
仅实现该后端才发布此位，与 HTTP/AAC 位独立组合；原操作和 512 字节帧负载上限不变。
下述多字节整数均为 LE，SSID/密码按字节传输，不附加 NUL，不回传已保存密码。

| 操作 | 请求 | 成功响应 |
|---|---|---|
| 22 WIFI_SCAN | 空 | scan_id:u32，立即确认，扫描在 worker 中执行 |
| 23 WIFI_PAGE | scan_id:u32, offset:u16 | scan_id:u32, scan_state:u8, count:u8, total:u8, reserved:u8=0；随后 count 个 AP |
| 24 WIFI_STATUS | 空 | 固定 110 字节，布局如下 |
| 25 WIFI_CONNECT | ssid_len:u8, password_len:u8, auth:u8, reuse:u8, SSID, password | operation:u32，立即确认，连接在 worker 中执行 |
| 26 WIFI_FORGET | 空 | 新 operation:u32；异步删除已保存配置并断开 Wi-Fi |
| 27 WIFI_CANCEL | operation:u32 | 空；只取消匹配的连接操作，进入 NVS 提交后返回 BUSY |

AP 固定 36 字节：ssid_len:u8, rssi:i8, auth:u8, channel:u8, SSID[32]（剩余填零）。
最多 24 个去重 AP、每页最多 12 个（最大负载 440 字节）。offset 不能超过 total，
scan_id 必须匹配当前扫描；扫描未完成时返回 count=0。客户端仅在完整收到同代列表后替换旧列表。
扫描失败不覆盖 H1 已显示的旧列表；隐藏且未广播名称的热点通过手动 SSID 添加。

STATUS 前 44 字节为 11 个 u32：schema=1, operation, outcome, error, raw_disconnect_reason,
connected, ip, rssi（按 i32 解释）, scan_id, scan_state, scan_count。
字节 44 为 current_len，45..76 为 current[32]；77 为 saved_len，78..109 为 saved[32]。
ip 与旧 STATUS 一致，按低字节先显示 IPv4。current 在未连接时可能为最近尝试的 SSID，
必须结合 connected 判断。USB 断开后的缓存不是当前无线状态。

- outcome：0 idle, 1 joining, 2 saved, 3 failed, 4 cancelled, 5 forgetting。
- error：0 none, 1 auth, 2 no_ap, 3 timeout, 4 unsupported, 5 storage, 6 driver。
- scan_state：0 idle, 1 scanning, 2 done, 3 failed。
- auth 沿用 ESP-IDF wifi_auth_mode_t：0 open，2 WPA-PSK，3 WPA2-PSK，4 WPA/WPA2，
  6 WPA3-PSK，7 WPA2/WPA3；1 WEP、5 企业及其他值拒绝。
- SSID 长 1..32 字节，无 NUL；密码 8..63 字节，无控制字符。开放网络密码为空且 reuse=0。
  reuse=1 时密码长度必须 0，仅允许与 ESP 已保存且有密码的 SSID 相同；密码留在 ESP 内部。

扫描/连接/忘记共用一个有界控制 worker；已有管理操作时返回 BUSY。
HTTP 与状态查询不在 USB 任务上等待扫描、认证或 NVS；切换 Wi-Fi 可能使在途 HTTP 失败，
H1 应先退出原联网应用。HELLO/USB 会话重建不清除 NVS，也不自动重放管理操作。
连接最长等待约 20 秒取得 IP；成功检查后提交 NVS，失败或取消保留原凭据并尝试恢复旧网络。
忘记操作先删除 NVS，成功后才断开并清空 RAM；保存失败保留原配置。
请求被接收后即使 USB 拔下也可能继续，重连后查询结果。ESP 擦除待处理凭据和接收解析器中的
CONNECT 负载，H1 清空编辑和 RPC 暂存；不通过文件或日志保存密码，未新增 NVS 加密。

以上扩展的实体无线/USB 联合验收尚未完成；MIPS 模型和 SDK 边界模拟证据见设置应用 README。

## 原有传输与 HTTP

H1 是 USB CDC 设备，ESP32-S3 是 USB Host。**H1主动发请求**，ESP32回复；
USB仍由主机调度，不改变USB角色。H1先发送HELLO，ESP32不向未知H1Debug会话发送命令。

每帧：12字节 `<4sBBHHH` 头、0..512字节payload、LE32 IEEE CRC32。
magic=`H1NW`，版本1，opcode、sequence、payload长度、status。
CRC覆盖头及payload；应答opcode=request|0x80、sequence相同。
H1发送给主机的帧若恰好是64字节倍数，须以ZLP结束，让CDC Host的IN传输完成。
ESP32发往H1的OUT传输由Host指定长度；H1按包读入解析器，不依赖OUT ZLP。
一次仅一个未完成RPC；请求超时不自动重放。
HELLO和STATUS不发起HTTP操作，应用可显式重新握手。

状态码：0 OK、1 INVALID、2 BUSY、3 NOT_CONNECTED、4 BAD_JOB、5 INTERNAL。

| opcode | 请求 | 应答payload |
|---|---|---|
| 1 HELLO | 空 | ASCII `H1NET/1 ESP32 HTTP` |
| 2 STATUS | 空 | 6个LE32：版本1、Wi-Fi已连接、IPv4原始4字节、RSSI有符号、空闲堆、最大下载字节数 |
| 3 HTTP_OPEN | max_bytes:u32、timeout_ms:u32、UTF-8 URL，无NUL | 非零job:u32；仅表示已接受 |
| 4 HTTP_INFO | job:u32 | 8个LE32：job、state、HTTP状态码、Content-Length或0xffffffff、received、delivered、buffered、error |
| 5 HTTP_READ | job:u32、wanted:u32 (1..500) | job:u32、offset:u32、count:u32、count字节正文 |
| 6 HTTP_CANCEL | job:u32 | 空；仅表示取消已请求 |
| 7 HTTP_META | job:u32 | job:u32、最终URL长度:u16、Content-Type长度:u16、URL字节、类型字节 |
| 8 URL_CHUNK | offset:u16、total:u16、URL分片（1..508字节） | 下一offset:u16 |
| 9 HTTP_RANGE | offset:u32、length:u32、total_size:u32、timeout_ms:u32、可选Referer UTF-8 | 非零job:u32 |
| 10 CAPABILITIES | 空 | 4个LE32：能力版本1、HTTP任务槽4、每HTTP槽缓冲8192字节、feature_bits；普通HTTP为3，AAC可用时为7 |
| 11 HTTP_RELEASE | job:u32 | 空；取消并释放此job的客户端所有权，ID立即失效 |
| 12 HTTP_OPEN_BACKGROUND | 与3相同 | 非零job:u32；最多占用三个槽，给前台保留容量 |
| 13 AUDIO_OPEN | version:u32=1、out_rate:u32=32768、out_channels:u32=1、asc_length:u32、ASC字节（1..64） | 非零音频job:u32；仅表示已接受 |
| 14 AUDIO_PUSH | job:u32、frame_seq:u32、total_bytes:u32、offset:u32、pts_ms:u32、AAC分片（1..492字节） | 本帧下一offset:u32 |
| 15 AUDIO_READ | job:u32、wanted:u32（1..500字节） | job:u32、byte_offset:u32、count:u32、count字节PCM |
| 16 AUDIO_INFO | job:u32 | 8个LE32：job、audio_state、out_rate、out_channels、input_bytes、total_output_bytes、buffered、error |
| 17 AUDIO_RESET | job:u32 | 空；保留job和ASC，清空解码状态与数据 |
| 18 AUDIO_CLOSE | job:u32 | 空；当前音频job立即失效 |
| 19 AUDIO_FINISH | job:u32 | 空；输入结束，待解码与输出仍须排空 |
| 20 REQUEST_CHUNK | offset:u16、total:u16、请求分片（1..508字节） | 下一offset:u16 |
| 21 HTTP_REQUEST | method:u32、url_length:u32、headers_length:u32、body_length:u32、max_bytes:u32、timeout_ms:u32 | 非零HTTP job:u32 |

HTTP job state：0 IDLE、1 CONNECTING、2 RECEIVING、3 DONE、4 FAILED、5 CANCELLED。
HTTP error：0 NONE、1 NETWORK、2 LIMIT、3 TIMEOUT、4 CANCELLED、5 WIFI、6 HTTP_PROTOCOL。
HTTP非2xx状态仍保留状态码和正文，由H1应用决定是否保存。
HTTP_OPEN限制 URL≤400字节、max_bytes=1..8388608、timeout_ms=1000..60000。
timeout_ms约束无进展等待：收到正文或成功向H1交付数据后刷新计时；
不以整个下载的累计耗时中断持续前进的慢速USB传输。单次RPC仍有独立时限。
只接受http://或https:// URL，拒绝内嵌NUL/CR/LF、空主机和URL用户信息。
HTTPS必须校验证书。允许有限重定向，响应不作HTML/字符集解析。
联网端发送 `Accept-Encoding: gzip, identity`，收到gzip（含x-gzip）时先流式解压，
HTTP_READ始终返回解码后的正文。无需修改H1NET/1协议或旧BDA。
gzip模式的received、delivered、buffered及max_bytes均按**解压后字节数**计；
接收期间Content-Length为0xffffffff，成功结束后改为解压后长度。
服务器的压缩Content-Length另行核对；每段gzip的CRC32/ISIZE及完整结束必须有效，支持连续成员。
损坏、截断、不支持的Content-Encoding（如br/deflate或叠加编码）返回HTTP_PROTOCOL；
解压后超过max_bytes返回LIMIT。即使部分正文已传送，最终FAILED也不可视作成功。
HTTP_META在最终响应头就绪后可读，否则BUSY。最终URL最多400字节，Content-Type最多96字节。
浏览器用它解析跳转后的相对链接、区分HTML/纯文字并选择UTF-8或GB2312解码。
这是向后兼容的新增命令；旧客户端仍可用1～6，新浏览器需配套含命令7的固件。

0.1.2固件加入8～9，供网络MP4按需读取。8允许URL总长1..2048字节；offset=0开始新组装，
后续片必须连续且total一致。HELLO清除组装状态，9成功接收任务后消耗该URL；
仅序号正确且URL完整、无控制字符/用户信息的请求可开始。普通3仍限制400字节。
9的length为1..8388608，offset<total_size且length<=total_size-offset，timeout同3；
可选Referer最多400字节，须为合法HTTP(S) URL，无CR/LF。联网端发送精确Range与Referer，
要求HTTP206和精确匹配的Content-Range（含总长），拒绝200整文件、错误/重复范围、截断和压缩范围响应。
H1仍用4/5/6读取/取消，游标以本次范围正文起点0计算；不把文件绝对offset写入READ游标。
旧META不能承载长URL，此类任务不调用7。传输帧仍最多512字节，旧BDA/普通HTTP不变。

固定大小环形缓冲流式传送；缓冲满时HTTP任务施加背压，不把整份下载装进内存。
HTTP_READ返回count=0只表示暂时没有数据；只有INFO为DONE且buffered=0才表示完整结束。
FAILED/CANCELLED即使已收到部分正文，也不是下载成功。Content-Length已知时必须核对完整长度。
H1客户端校验job、连续offset和count，不自动重发推进游标的READ。
0.1.4新增10～12。成功查询CAPABILITIES后，本会话切换为显式多任务模式：
最多四个独立HTTP/TLS worker，每个任务有独立8KiB环形缓冲、状态、元数据及读取游标。
3和9可使用所有空闲槽；12最多占用三个槽（计入已释放但尚未退出的worker），给前台保留一个槽。
任务可任意交错INFO／READ，单条USB链路仍一次一个RPC，不表示HTTP只能发起一个请求。
所有HTTP job ID在会话重置和槽复用后继续递增，跳过0及尚占用的ID。

显式模式的DONE／FAILED／CANCELLED任务一直保留，直到客户端发送11；不能因已读完而自动淘汰，
否则另一个请求可能复用仍等待最后一次INFO的任务。6只取消指定job，仍允许读取其终态；
11使该job立即不可读（BAD_JOB），清除未读正文并请求取消。若worker正阻塞于网络调用，
其存储和物理槽保留至I/O返回，ACK不表示worker已经结束；其他任务继续运行。
普通切页／取消不发送HELLO，避免重置其他任务。feature bit0=RELEASE，bit1=后台槽预留。
新客户端重新HELLO后应再次查询10，并在每个任务成功或失败后显式RELEASE。

未查询10的旧客户端保持旧的单任务行为：旧任务未结束／正文未读完时拒绝新OPEN，
终态且buffered=0可隐式复用；命令1～9及META在EOF后的用法保持兼容。
HELLO清理前一会话全部任务，worker未退出时返回BUSY；USB拔线请求取消全部HTTP任务。
任务确认退出前不复用其缓冲／句柄。

通用AAC远程计算使用feature bit2（`H1NET_CAP_AUDIO_AAC=4`）。客户端先检查10的能力位；
仅支持HTTP的服务仍返回3，已启用且初始化成功的AAC服务返回7。它不改变前3个能力字段：
四个槽和8192字节描述HTTP，音频另有一个独立job及有界输入／输出存储，不占用HTTP槽。
PC参考服务须显式给`Server`传入可用的`AudioService`；默认HTTP服务不宣告此能力。

13～19只接收AAC配置和已经解复用的音频访问单元，不获取URL、解析MP4或转换H.264。
ASC是AudioSpecificConfig；OPEN的版本、输出采样率和声道数目前只允许表中固定值。
服务按支持的AAC配置子集校验ASC；ESP32限制见[依赖与验证说明](esp32/AUDIO_DEPENDENCY.md)。
同一时刻最多存在一个音频job；它即使已到EOF，也须CLOSE后才能再次OPEN，否则返回BUSY。
音频ID与HTTP ID属于不同操作命名空间，数值可以相同；音频RESET/CLOSE不会取消或释放HTTP任务。
全部RPC仍共用一条连续的16位sequence，不能为音频另起RPC序列；`frame_seq`是独立的32位AAC帧序号。

每次OPEN/RESET后，首个`frame_seq=0`，一整帧成功接收后递增，按u32回绕。
`total_bytes=1..16384`表示这一帧的总压缩字节数；`offset`从0起，后续片必须紧接上次成功回复的offset。
同帧所有分片的total与pts_ms必须相同。PUSH回复是**本帧内**下一偏移，末片成功时等于total_bytes，
并非全部输入的累计计数。pts_ms单位为毫秒；当前后端只核对分片一致性，输出连续PCM，不按PTS插入静音。

输入槽或PCM输出背压可使PUSH返回BUSY；**该次请求的分片未被消费，包括末片**。
客户端保持相同job、frame_seq、total、offset、PTS和分片内容，先READ释放输出空间或等待工作线程前进，
再用下一个RPC sequence重试。BUSY不改变输入计数、offset或帧号；只用成功回复的下一offset继续分片。
其他错误可能伴随解码失败，不能据此假定分片未被消费，应查询INFO并通过RESET或CLOSE恢复。
PC后端可在末片解码前拒绝消费；ESP32也可因压缩帧队列已满拒绝当前片。
明确收到BUSY的重试与RPC超时不同：超时意味着接收结果未知，不能自动重放推进状态的PUSH或READ。

AUDIO_READ返回小端有符号16位、32768Hz、单声道PCM。byte_offset从OPEN/RESET后的0开始连续递增，
count和wanted均按字节计；需要完整采样的客户端应请求偶数wanted，并校验job、偏移、count与应答长度。
`count=0`成功回复只表示当前无PCM，READY时也可发生；它不表示流结束。
AUDIO_INFO的audio_state独立于HTTP状态：1 READY、2 EOF、3 FAILED。
input_bytes为成功接收的AAC分片累计字节；total_output_bytes为已产出的PCM累计字节；buffered为尚未READ的PCM字节。
EOF时仍可能有buffered数据；只有`audio_state=2 && buffered=0`才表示全部输出已排空。
FAILED及非零error必须视为失败，即使之前已读到部分PCM。error正常为0；当前后端解码失败记录INTERNAL=5。

RESET用于同一AAC配置下的跳转／重播：保留job与ASC，丢弃部分AAC帧、待解码帧和未读PCM，
清零帧序号、输入／输出／读取字节游标及EOF／错误状态，重新进入READY。
ESP工作线程通过代次检查丢弃RESET前仍在处理的结果。RESET不重新HELLO，也不改变任何HTTP读取游标。
FINISH只在没有未完成AAC分片时发送；成功后不再PUSH，继续READ/INFO等待EOF并排空PCM。
FINISH回复不代表解码和输出已经完成；PC后端若因PCM背压返回BUSY，先排空输出再重试。
客户端应发送一次成功的FINISH，不依赖EOF后的重复FINISH在所有后端都返回相同结果。
CLOSE丢弃未读数据并立即使ID无效，之后针对旧ID的请求返回BAD_JOB；ESP工作线程异步关闭其私有解码器。
成功HELLO和USB会话断开也会清理音频job。正常音频跳转／关闭使用17／18，避免重置并行HTTP请求。

ESP32-S3实现使用192KiB PSRAM工作区：两个16KiB压缩帧槽、16KiB私有输入、16KiB解码暂存及128KiB PCM环。
另有24KiB任务栈及随AAC配置变化的解码器堆分配；缺少PSRAM或初始化失败时不宣告能力4。
关闭job后常驻工作区和任务继续保留供后续OPEN复用。解码任务优先级2，每帧让出一个tick，USB任务不执行AAC解码。
现有离线协议、PCM及MIPS集成证据不等于ESP真机性能；AAC/HE-AAC实时能力、USB/Wi-Fi并行负载和持续播放仍待真机验证。

USB VID/PID 首版沿用项目已有实验值07C4:A4A6，以独立H1NET/1握手区分用途。
H1前台联网程序与H1Debug不能同时接管USB。必须先在H1上启动网络BDA，再接USB线。

通用请求扩展使用feature bit3（`H1NET_CAP_HTTP_REQUEST=8`），与RELEASE、后台槽和AAC位独立组合。
HTTP服务具备此扩展时普通能力为11，AAC同时可用时为15；只检查需要的位，不以整个feature值判断。
旧1～19、GET/Range/gzip/AAC行为和帧上限保持兼容。客户端先查询10，再使用20～21。

20组装 `URL字节 || headers字节 || body字节`，无分隔符/NUL；三个长度在21指定。
URL为合法HTTP(S) UTF-8，1..400字节；headers为0..1024字节；body为0..16384字节；
因此total为1..17808，按LE16传送。offset=0先安全擦除旧暂存并开始新上传；后续片必须连续、
total一致且片尾不越界。非法分片会擦除整个暂存，不推进游标。与8的长URL暂存互相独立。
21固定24字节，method仅1=GET或2=POST；GET的body_length必须0，POST允许空正文及二进制正文。
三个长度必须分别满足上限且总和精确等于已完整上传的total；max_bytes、timeout_ms与3一致。
仅完整校验后才启动worker。21每次处理（含失败/BUSY）都擦除暂存；重试需完整重新上传。
HELLO也先擦除暂存，即使后端因旧worker未退出而返回BUSY。USB会话重新初始化会擦除暂存。
服务端暂存为固定有界存储，ESP32 USB实例位于静态区；后台worker在接受前深拷贝headers/body到私有堆，
任务退出时擦除并释放。RELEASE立即使ID失效，但旧worker存储直到退出才允许复用。

headers为ASCII `Name: value`，字段间严格CRLF，可省略末尾CRLF。拒绝空行、折行、重复字段、
空字段名、非HTTP token字段名、NUL、裸CR/LF、控制字符（含TAB）及非ASCII值。
支持Authorization、Content-Type、Accept及其他普通端到端字段。以下字段大小写不敏感地禁止：
Host、Content-Length、Transfer-Encoding、Connection、Proxy-Connection、Keep-Alive、TE、Trailer、
Upgrade、Expect、Accept-Encoding、Proxy-Authorization、Proxy-Authenticate。框架负责正文长度和传输编码。
扩展请求使用 `Accept-Encoding: identity`；不遵守此协商而返回压缩正文时报告HTTP_PROTOCOL。
普通3/9保留既有压缩策略。扩展请求完全禁用自动重定向，避免Authorization或POST正文转送其他URL；
3xx与其他非2xx响应一样保留原状态、响应正文和META，由应用决定如何提示。

21返回的任务与普通HTTP共享四个槽，沿用4/5/6/7/11及连续READ游标、8KiB背压和状态语义。
短SSE片段可在EOF之前读取；不解析SSE、JSON或UTF-8，跨READ的UTF-8序列由应用合并。
PC服务使用HTTPResponse.read1，ESP扩展请求在HTTP_EVENT_ON_DATA中即时将网络分片写入环，
不等esp_http_client_read填满512字节；read返回的副本不再重复入环。
实现依据[ESP-IDF v5.5.1 HTTP客户端源码](https://github.com/espressif/esp-idf/blob/v5.5.1/components/esp_http_client/esp_http_client.c)：
http_on_body先派发ON_DATA，esp_http_client_read随后才继续等待剩余缓冲。此源码审计不等于真机时延验证。


## 通用响应头与长请求URL（0.4.4）

`H1NET_CAP_HTTP_HEADERS=32`，配合 `HTTP_HEADERS=28`。同一版本的HTTP_REQUEST支持最长2048字节URL，20暂存总量上限19456字节；旧1..400字节请求保持兼容。其他旧GET/META的URL上限不变。

28请求固定12字节：job、offset、wanted三个LE32；wanted为1..500。响应头为job、offset、total（LE32），后接0..wanted字节原始字段，响应正文长度减12即本次字节数。total上限8192，offset<=total；重复头分别保留，字段以CRLF结束。头未就绪返回BUSY，非法offset返回INVALID，RELEASE后的job返回BAD_JOB。只有通用HTTP_REQUEST任务保存头；普通旧GET/Range不额外保存。头超限或控制字符非法时整个任务失败。RELEASE/重置/worker回收时擦除存储。

此接口不解释Cookie、OAuth、登录状态或站点规则。应用自行解析Set-Cookie、处理Location并决定下个请求；ESP不持久化应用凭据，也不自动发送某个站点的账号。自动重定向仍关闭，以免私有headers/body被转送其他URL。
