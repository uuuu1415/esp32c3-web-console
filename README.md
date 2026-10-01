# esp32c3-web-console

一个跑在 **ESP32-C3** 上的个人主页 + 状态接口，页面和数据全部由板子自己提供，**不依赖任何云服务或后端服务器**。

网页内容（网名、头像、网址列表、联系方式、时间树、标签…）存在板子的 NVS 里，随时通过内置的 `/admin` 后台修改，**不用重新烧固件**。

> 目标场景：想给自己的开发板做一个能公网访问的小站，又不想买服务器、不想备案。
> 板子接入家里的 WiFi，通过 IPv6 + DDNS 拿到一个域名，直接对外提供服务。

---

## 功能

**页面（`/`）**
- 单页个人主页：头像 / 网名 / 一句话 / 个人标签 / 设备信息 / 联系方式
- 实时时钟、天气（浏览器直接请求 [Open-Meteo](https://open-meteo.com)，板子零负担）
- 网页列表（数量不限，自适应排列）、可滚动的时间树
- 亮色 / 暗色主题切换（右下角，记在浏览器里）
- 移动端自适应

**后台（`/admin`）**
- 表单式编辑：侧写 / 站点 / 网页列表 / 联系方式 / 时间树
- 每栏支持 **上移 / 下移 / 反转顺序**，列表顺序就是网页显示顺序
- **开发板状态监控**：芯片、Flash、内存、温度、WiFi 信号、IPv4/IPv6、MAC、在线时长、DDNS 状态
- **请求日志**：最近 40 条请求的路径与来源 IP，排查"请求为什么变多"
- 写入令牌保护（`?k=令牌`，可收藏带令牌的链接）

**网络**
- WiFi 自动重连
- **IPv6**：拿到公网地址（`SLAAC`），同时监听 IPv4
- **DDNS**：把 IPv6 地址推到 DNSPod（只在地址真的变化时才调 API，避免被限流）
- 同时监听 80 与自定义端口（浏览器只认 80/443，这样地址栏里可以不带端口）

---

## 硬件

| | |
|---|---|
| 开发板 | 合宙 CORE-ESP32C3（AirM2M CORE ESP32C3，4MB Flash） |
| 芯片 | ESP32-C3（RISC-V 160MHz，WiFi 4 + BLE 5.0） |
| 其他 ESP32-C3 板子 | 只要能改 `board` 就能用（注意 Flash 容量，程序约 1MB） |

`platformio.ini` 里用的 `board = airm2m_core_esp32c3` 会替你做好三件事：LED 引脚定义、`flash_mode = dio`（GPIO12/13 只有在 DIO 模式下才能当普通 IO）、4MB Flash 与排针命名。

---

## 快速开始

```bash
# 1. 克隆
git clone https://github.com/<you>/esp32c3-web-console.git
cd esp32c3-web-console

# 2. 生成本地配置（这个文件不会被提交）
cp include/wifi_config.example.h include/wifi_config.h     # Windows 用 copy

# 3. 编辑 include/wifi_config.h，填 WiFi 名称/密码，
#    需要 DDNS 的话再填 DNSPod Token 和域名

# 4. 编译并烧录
pio run -t upload
pio device monitor        # 串口日志会打印可访问的地址
```

烧录后串口会打印类似：

```
[http] 后台管理: http://192.168.1.9:8088/admin?k=CHANGE_ME
```

把那条链接收藏起来，以后改内容直接进。

> 依赖只有 [ArduinoJson](https://arduinojson.org/)（`lib_deps` 里已声明，PlatformIO 会自动下载）。

---

## 接口

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | 主页（内嵌在固件里，约 20KB） |
| GET | `/admin` | 后台管理页 |
| GET | `/api/status` | 板子状态 JSON（芯片/内存/温度/WiFi/IP/DDNS/请求数） |
| GET | `/api/content` | 页面内容 JSON（公开读取） |
| POST | `/api/content?k=令牌` | 写入内容（需要令牌） |
| GET | `/api/requests` | 最近 40 条请求日志 |
| GET | `/api/log` | 串口日志环形缓冲 |
| GET | `/api/scan` | 扫描附近 WiFi |
| GET | `/api/led?on=0/1&bright=0-255` | 板载 LED 控制 |
| GET | `/favicon.ico` | 302 跳转到后台配置的站点图标 |

内容 JSON 结构：

```json
{
  "profile": { "name": "...", "avatar": "URL", "tagline": "...", "tags": ["..."], "bg": "URL" },
  "site":    { "title": "...", "favicon": "URL", "city": "Beijing" },
  "urls":    [{ "icon": "🌐", "img": "URL", "name": "...", "url": "...", "desc": "..." }],
  "contacts":[{ "icon": "🐙", "img": "URL", "name": "...", "url": "..." }],
  "timeline":[{ "date": "2026.01", "title": "...", "desc": "..." }]
}
```

---

## 实现要点

- **手写的非阻塞 HTTP 服务**：8 个连接槽 × 2KB 缓冲，`accept()` 后置为非阻塞，按 `\r\n\r\n` + `Content-Length` 判断请求收完，`shutdown(SHUT_WR)` 后 drain 再 `close()`，避免浏览器收到 RST。
- **内容存 NVS**：用 `Preferences` 按 key 存 JSON 片段，写入用 ArduinoJson 序列化，读取时校验类型，损坏则回退默认值。
- **DDNS 只在地址变化时调用**：DNSPod 规定"1 小时内超过 5 次无变化的修改请求"会锁定记录，所以先做字节级比较再决定是否推送。
- **天气在浏览器侧获取**：板子只提供城市名，天气由访客浏览器直接请求 Open-Meteo（免费、无需 key、允许跨域），板子不承担 TLS 与解析开销。
- **图片不经过板子**：头像/背景/图标/网站图标都是浏览器直接去图库取，板子一个字节都不转发。
- **页面切到后台时轮询降频**：前台 1 秒一次 `/api/status`，切到后台变 15 秒，避免多个标签页把请求数拉爆。

---

## 已知限制

- 只有 HTTP，没有 TLS（ESP32-C3 跑 HTTPS 服务压力较大）。公网暴露时请注意：`/admin` 虽然有令牌保护，但**首页是公开的**。
- 没有 OTA：分区表用 `huge_app.csv`（程序区约 3MB），代价是不支持双分区 OTA。
- 数据存在 NVS，**刷固件不会清空**，但 `pio run -t erase`（擦除整片 Flash）会。

---

## License

[MIT](LICENSE)
