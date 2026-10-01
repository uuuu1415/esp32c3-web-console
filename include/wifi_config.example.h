#pragma once

// ============================================================
//  复制这个文件为 wifi_config.h，然后填上你自己的信息。
//
//  wifi_config.h 已经写进 .gitignore —— 你的 WiFi 密码、
//  API Token 不会被提交到仓库里。
//
//  Windows:  copy include\wifi_config.example.h include\wifi_config.h
//  Linux/macOS:  cp include/wifi_config.example.h include/wifi_config.h
// ============================================================

// ============ WiFi ============
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// 是否允许 WiFi modem sleep（省电模式），实测差别很大：
//   1 = 省电（芯片在 beacon 间隙睡觉）：ping 平均约 369 ms，电流小
//   0 = 不省电（一直守着）：ping 平均几毫秒，响应快，但更费电
// 做实时控制/交互（手机点一下要立刻响应）用 0；做电池供电的传感器用 1。
#define WIFI_MODEM_SLEEP 0

// ============ HTTP 服务 ============
// 运营商家宽经常封 80/8080/443 的入站，所以主用 8088。
#define HTTP_PORT 8088

// "备用端口"用来试 80：浏览器只认 80(http)/443(https)，
// 监听了 80 才能用 http://esp.你的域名/ 这种不带端口的地址。
// 如果运营商封了 80，它连不上，但主端口不受任何影响。填 0 表示不监听。
#define HTTP_PORT_ALT 80

// ============ DNSPod 动态域名（DDNS）============
// 不用 DDNS 就填 0
#define DDNS_ENABLE 1

// DNSPod Token：控制台 → 用户中心 → 安全设置 → API Token，格式是 "ID,Token"
// 例如 "123456,abcdef1234567890abcdef1234567890"
#define DNSPOD_TOKEN "YOUR_ID,YOUR_TOKEN"

#define DDNS_DOMAIN    "example.com"   // 你的域名
#define DDNS_SUBDOMAIN "esp"           // 主机记录（子域名）
#define DDNS_TTL       600             // DNSPod 免费版最小 600 秒

// ============ 写入令牌 ============
// 网页列表 / 联系方式 / 时间树存在板子 flash 里，可以随时改、不用重烧固件。
// 因为首页域名是公开的，/admin 谁都能打开，所以写操作默认需要一个令牌。
//
// 用法：后台链接带上 ?k=令牌 即可，例如
//      http://esp.example.com/admin?k=YOUR_TOKEN
// 收藏这条链接就行，不用每次手输。
//
// 留空 = 不校验（任何人都能改内容），只在完全内网使用时才建议这么做。
#define WRITE_TOKEN "CHANGE_ME"
