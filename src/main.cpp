#include <Arduino.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <lwip/sockets.h>
#include <lwip/ip_addr.h>
#include "esp_netif.h"
#include "wifi_config.h"
#include "web_page.h"
#include "admin_page.h"

/*
 * CORE-ESP32C3：开机自动连 WiFi + 双栈 HTTP 服务（不含蓝牙）
 *
 *   - 上电后自动连 wifi_config.h 里配置的 AP，连不上就每 10 秒重试
 *   - 连上后打印 IP / IPv6 / 网关 / 信号强度，之后每 30 秒报一次心跳
 *   - 掉线自动重连（WiFi.setAutoReconnect + 自己兜底）
 *   - 不反复写 flash（WiFi.persistent(false)）
 *   - 跑一个极简 HTTP 服务，同时监听 IPv6(::) 和 IPv4(0.0.0.0)：
 *       /              状态页面
 *       /json          状态 JSON
 *       /led/on|/led/off   远程控制 D5(GPIO13)
 *     地址：http://[板子的IPv6]:8088/  或  http://192.168.1.x:8088/
 *
 * LED 指示：
 *   D4 (GPIO12) 正在连接 = 100ms 快闪；已连上 = 常亮；掉线 = 灭
 *   D5 (GPIO13) 网络状态灯之外的"可远程控制"灯，默认灭
 *
 * 开销：连接成功那一刻写一次 GPIO（实测 537 ns），之后电平由硬件保持，
 *       CPU 占用 0；真正消耗的是 LED 本身的持续电流（约 1~3 mA）。
 *
 * 修改 WiFi / 端口只改 include/wifi_config.h
 * 备份：backup/main_blink_backup.cpp、backup/main_radiotest.cpp、backup/main_radio_cost.cpp
 */

#ifndef LED_BUILTIN
#define LED_BUILTIN 12
#endif
#ifndef LED_BUILTIN_AUX
#define LED_BUILTIN_AUX 13
#endif

static const char *PLACEHOLDER_SSID = "YOUR_WIFI_SSID";
static const uint32_t RETRY_MS = 10000;   // 连接重试间隔
static const uint32_t RSSI_MS  = 30000;   // 心跳打印间隔

static bool gConfigured = false;
static bool gConnected = false;
static bool gIpv6Tried = false;
static uint32_t gLastBlink = 0, gLastRetry = 0, gLastRssi = 0, gConnAt = 0;
static uint32_t gAttempt = 0;

// D5 用硬件 LEDC PWM 调光（CPU 开销 0）
static const int LEDC_CH = 0;
static const int LEDC_BITS = 8;
static const int DEFAULT_BRIGHT = 70;
static bool gLedOn = false;
static int gLedPct = 0;  // 0 = 灭

// 日志环形缓冲，供网页的"设备日志"面板读取
static const int LOG_LINES = 14;
static const int LOG_LEN = 96;
static char gLogRing[LOG_LINES][LOG_LEN];
static int gLogIdx = 0;

static void logf(const char *fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  Serial.println(buf);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.println(buf);
#endif

  snprintf(gLogRing[gLogIdx], LOG_LEN, "%s", buf);
  gLogIdx = (gLogIdx + 1) % LOG_LINES;
}

static const char *statusName(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (找不到这个 WiFi 名字?)";
    case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED (密码不对?)";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "UNKNOWN";
  }
}

// 打印 IPv6 状态：link-local 一定有；全局地址要看路由器/运营商肯不肯下发前缀
static void printIPv6Status() {
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!netif) {
    logf("       IPv6    : netif 未就绪");
    return;
  }

  esp_ip6_addr_t a;
  if (esp_netif_get_ip6_linklocal(netif, &a) == ESP_OK) {
    logf("       IPv6 LL : " IPV6STR, IPV62STR(a));
  }
  if (esp_netif_get_ip6_global(netif, &a) == ESP_OK) {
    logf("       IPv6    : " IPV6STR "   <- 全局地址，可公网直连", IPV62STR(a));
  } else {
    logf("       IPv6    : 无全局地址（路由器或运营商没下发 IPv6 前缀）");
  }
}

// ==================== 极简 HTTP 服务（IPv6 + IPv4 双栈） ====================
// 不用 WiFiServer：它在这个核心版本里只绑 IPv4。这里直接用 lwIP socket，
// IPv6 用 :: 监听、IPv4 另开一个 0.0.0.0 监听，互不抢端口，行为确定。

static const int MAX_LISTENERS = 4;
static int gListeners[MAX_LISTENERS] = {-1, -1, -1, -1};
static uint32_t gReqCount = 0;
static char gLastPeer[64] = "-";

// DNSPod DDNS 状态（网页上会显示）
static char gDdnsRecordId[24] = "";
static char gDdnsLineId[16] = "0";     // 从记录里读出来的线路 ID（默认线路是 0）
static char gDdnsLastV6[48] = "";      // 目前 DNS 上的地址
static uint32_t gDdnsLastOkMs = 0;     // 上次成功时间（millis）
static int gDdnsResult = 0;            // 0=未跑 1=成功 -1=失败
static char gDdnsMsg[72] = "未配置";

static void fmtPeer(const struct sockaddr_storage *ss, char *out, size_t n) {
  if (ss->ss_family == AF_INET6) {
    const struct sockaddr_in6 *a6 = (const struct sockaddr_in6 *)ss;
    snprintf(out, n, "[%s]", ip6addr_ntoa((const ip6_addr_t *)&a6->sin6_addr));
  } else {
    const struct sockaddr_in *a4 = (const struct sockaddr_in *)ss;
    snprintf(out, n, "%s", ip4addr_ntoa((const ip4_addr_t *)&a4->sin_addr));
  }
}

static int startListener(int family, uint16_t port) {
  int fd = socket(family, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    return -1;
  }
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  if (family == AF_INET6) {
    int only = 1;  // 只管 IPv6，IPv4 由另一个 socket 负责
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &only, sizeof(only));
    struct sockaddr_in6 a = {};
    a.sin6_family = AF_INET6;
    a.sin6_addr = in6addr_any;
    a.sin6_port = htons(port);
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
      close(fd);
      return -1;
    }
  } else {
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
      close(fd);
      return -1;
    }
  }

  if (listen(fd, 4) < 0) {
    close(fd);
    return -1;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);  // 非阻塞，不卡住 loop
  return fd;
}

static void startServer() {
  uint16_t ports[2] = {HTTP_PORT, HTTP_PORT_ALT};
  int n = 0;
  for (int p = 0; p < 2; p++) {
    if (ports[p] == 0) {
      continue;  // 0 = 不监听这个端口
    }
    if (p == 1 && ports[1] == ports[0]) {
      continue;  // 和主端口重复就跳过
    }
    for (int fam = 0; fam < 2 && n < MAX_LISTENERS; fam++) {
      gListeners[n] = startListener(fam == 0 ? AF_INET6 : AF_INET, ports[p]);
      n++;
    }
    logf("[http] 端口 %u: IPv6 %s / IPv4 %s", (unsigned)ports[p],
         gListeners[n - 2] >= 0 ? "OK" : "失败", gListeners[n - 1] >= 0 ? "OK" : "失败");
  }

  logf("[http] 局域网: http://%s:%d/", WiFi.localIP().toString().c_str(), HTTP_PORT);

  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_ip6_addr_t g6;
  if (netif && esp_netif_get_ip6_global(netif, &g6) == ESP_OK) {
    logf("[http] 外网  : http://[" IPV6STR "]:%d/", IPV62STR(g6), HTTP_PORT);
  }
}

// ===================== HTTP 响应小工具 =====================

// 一次 send() 可能只发出去一部分（lwIP 发送缓冲区有限），必须循环发完
static void sendAll(int fd, const char *data, size_t len) {
  size_t off = 0;
  while (off < len) {
    int r = send(fd, data + off, len - off, 0);
    if (r <= 0) {
      return;
    }
    off += (size_t)r;
  }
}

static const char *httpReason(int code) {
  switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    default:  return "OK";
  }
}

static void sendHeader(int fd, int code, const char *ctype, size_t len) {
  char hdr[192];
  int hl = snprintf(hdr, sizeof(hdr),
                    "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nConnection: close\r\n"
                    "Cache-Control: no-store\r\nContent-Length: %u\r\n\r\n",
                    code, httpReason(code), ctype, (unsigned)len);
  sendAll(fd, hdr, hl);
}

static void sendText(int fd, int code, const char *ctype, const char *body) {
  size_t len = strlen(body);
  sendHeader(fd, code, ctype, len);
  sendAll(fd, body, len);
}

static void getV6(char *out, size_t n) {
  snprintf(out, n, "-");
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_ip6_addr_t g6;
  if (netif && esp_netif_get_ip6_global(netif, &g6) == ESP_OK) {
    snprintf(out, n, IPV6STR, IPV62STR(g6));
  }
}

// ===================== 可配置内容：侧写 / 网页列表 / 联系方式 / 时间树 =====================
// 四块内容以 JSON 文本存放在 NVS(flash)，可在 /admin 或 POST /api/content 修改，
// 无需重新烧录固件。JSON 的解析与生成统一交给 ArduinoJson，不手写字符串处理。
// 读取公开（首页要用），写入需要 WRITE_TOKEN。

static String gProfileJson;
static String gSiteJson;
static String gUrlsJson;
static String gContactsJson;
static String gTimelineJson;


// 首次上电（NVS 里还没有内容）时使用的默认值，全部是通用示例，
// 真实内容请在 /admin 里改，会存在板子的 NVS 里。
static const char *DEF_PROFILE =
    "{\"name\":\"ESP32\",\"avatar\":\"\",\"tagline\":\"ESP32-C3 · 自建小站\","
    "\"tags\":[\"ESP32\",\"IPv6\",\"DIY\"],\"bg\":\"\"}";
static const char *DEF_SITE =
    "{\"title\":\"ESP32-C3 小站\",\"favicon\":\"\",\"city\":\"Beijing\"}";
static const char *DEF_URLS =
    "[{\"icon\":\"🌐\",\"name\":\"示例链接\",\"url\":\"https://example.com\",\"desc\":\"在后台改成你自己的\"}]";
static const char *DEF_CONTACTS =
    "[{\"icon\":\"🐙\",\"name\":\"GitHub\",\"url\":\"\"},"
    "{\"icon\":\"✉️\",\"name\":\"邮箱\",\"url\":\"\"}]";
static const char *DEF_TIMELINE =
    "[{\"date\":\"2026.01\",\"title\":\"网站上线\",\"desc\":\"板子跑起来了\"},"
    "{\"date\":\"2026.01\",\"title\":\"接入 IPv6\",\"desc\":\"拿到公网地址\"},"
    "{\"date\":\"2026.01\",\"title\":\"DDNS 生效\",\"desc\":\"用域名访问\"}]";

// 读取 NVS 里的文本；只有能解析成期望类型才采用，否则回退到默认值
static String contentLoad(const char *key, const char *fallback, bool wantObject) {
  Preferences prefs;
  prefs.begin("site", true);
  String saved = prefs.getString(key, "");
  prefs.end();
  if (saved.length() == 0) {
    return String(fallback);
  }
  JsonDocument doc;
  if (deserializeJson(doc, saved) != DeserializationError::Ok) {
    logf("[content] %s 内容损坏，已回退默认值", key);
    return String(fallback);
  }
  bool ok = wantObject ? doc.is<JsonObject>() : doc.is<JsonArray>();
  return ok ? saved : String(fallback);
}

// 站点信息（标题 / favicon）单独缓存一份，供 /favicon.ico 直接跳转使用
static char gFaviconUrl[256] = "";

static void refreshSiteCache() {
  gFaviconUrl[0] = 0;
  JsonDocument doc;
  if (deserializeJson(doc, gSiteJson) == DeserializationError::Ok) {
    snprintf(gFaviconUrl, sizeof(gFaviconUrl), "%s", (const char *)(doc["favicon"] | ""));
  }
}

static void contentBegin() {
  gProfileJson = contentLoad("profile", DEF_PROFILE, true);
  gSiteJson = contentLoad("site", DEF_SITE, true);
  gUrlsJson = contentLoad("urls", DEF_URLS, false);
  gContactsJson = contentLoad("contacts", DEF_CONTACTS, false);
  gTimelineJson = contentLoad("timeline", DEF_TIMELINE, false);
  refreshSiteCache();
  logf("[content] 侧写 %u / 站点 %u / 网页 %u / 联系 %u / 时间树 %u 字节",
       (unsigned)gProfileJson.length(), (unsigned)gSiteJson.length(),
       (unsigned)gUrlsJson.length(), (unsigned)gContactsJson.length(),
       (unsigned)gTimelineJson.length());
}

static String buildContentJson() {
  String out;
  out.reserve(gProfileJson.length() + gSiteJson.length() + gUrlsJson.length() +
              gContactsJson.length() + gTimelineJson.length() + 80);
  out = "{\"profile\":" + gProfileJson + ",\"site\":" + gSiteJson + ",\"urls\":" + gUrlsJson +
        ",\"contacts\":" + gContactsJson + ",\"timeline\":" + gTimelineJson + "}";
  return out;
}

static bool contentSave(const char *key, const String &value) {
  Preferences prefs;
  if (!prefs.begin("site", false)) {
    return false;
  }
  bool ok = prefs.putString(key, value) > 0;
  prefs.end();
  return ok;
}

static void sendContentJson(int fd) {
  String out = buildContentJson();
  sendHeader(fd, 200, "application/json", out.length());
  sendAll(fd, out.c_str(), out.length());
}

// 把数组长度数出来（仅用于回执，顺便验证存进去的内容是合法 JSON）
static size_t jsonArraySize(const String &json) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return 0;
  }
  return doc.size();
}

// 写权限校验：WRITE_TOKEN 留空 = 不校验；否则要求 ?k=<令牌>
static bool writeAllowed(const char *query) {
  size_t tokenLen = strlen(WRITE_TOKEN);
  if (tokenLen == 0) {
    return true;
  }
  const char *given = strstr(query, "k=");
  return given != NULL && strncmp(given + 2, WRITE_TOKEN, tokenLen) == 0;
}

static void handleContentPost(int fd, const char *body, const char *query) {
  if (!writeAllowed(query)) {
    logf("[content] 拒绝写入：令牌不正确");
    sendText(fd, 403, "application/json",
             "{\"ok\":0,\"msg\":\"写入令牌不正确（后台链接要带 ?k=令牌）\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    String msg = String("{\"ok\":0,\"msg\":\"JSON 解析失败: ") + err.c_str() + "\"}";
    sendText(fd, 400, "application/json", msg.c_str());
    return;
  }

  bool saved = true;
  if (doc["profile"].is<JsonObject>()) {
    serializeJson(doc["profile"], gProfileJson);
    saved = contentSave("profile", gProfileJson) && saved;
  }
  if (doc["site"].is<JsonObject>()) {
    serializeJson(doc["site"], gSiteJson);
    saved = contentSave("site", gSiteJson) && saved;
    refreshSiteCache();
  }
  if (doc["urls"].is<JsonArray>()) {
    serializeJson(doc["urls"], gUrlsJson);
    saved = contentSave("urls", gUrlsJson) && saved;
  }
  if (doc["contacts"].is<JsonArray>()) {
    serializeJson(doc["contacts"], gContactsJson);
    saved = contentSave("contacts", gContactsJson) && saved;
  }
  if (doc["timeline"].is<JsonArray>()) {
    serializeJson(doc["timeline"], gTimelineJson);
    saved = contentSave("timeline", gTimelineJson) && saved;
  }

  JsonDocument res;
  res["ok"] = saved ? 1 : 0;
  res["urls"] = jsonArraySize(gUrlsJson);
  res["contacts"] = jsonArraySize(gContactsJson);
  res["timeline"] = jsonArraySize(gTimelineJson);
  String out;
  serializeJson(res, out);
  sendHeader(fd, 200, "application/json", out.length());
  sendAll(fd, out.c_str(), out.length());
  logf("[content] 已写入 flash（网页 %u / 联系 %u / 时间树 %u）", (unsigned)res["urls"],
       (unsigned)res["contacts"], (unsigned)res["timeline"]);
}

// 302：把浏览器的 /favicon.ico 请求转给站点图标（可以是你图库里的图片）
static void sendRedirect(int fd, const char *url) {
  char hdr[320];
  int n = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length: 0\r\n"
                   "Cache-Control: max-age=3600\r\nConnection: close\r\n\r\n",
                   url);
  sendAll(fd, hdr, n);
}

// ===================== LED 探测（诊断用）=====================
// 官方文档说 D4=IO12、D5=IO13，但经典版可能不同。这里依次把候选 GPIO
// 各闪 3 次，并把引脚号打进日志 —— 盯着板子看哪一下亮，对照日志就知道引脚了。
// 刻意跳过：GPIO9(BOOT)、GPIO11(VDD_SPI)、GPIO20/21(UART0 串口)
static const int PROBE_PINS[] = {12, 13, 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 18, 19};
static const int PROBE_N = (int)(sizeof(PROBE_PINS) / sizeof(PROBE_PINS[0]));
static int gProbeIdx = -1;  // -1 = 未运行
static uint32_t gProbePinStart = 0;
static int gProbePin = -1;

static void probeStart() {
  gProbeIdx = 0;
  gProbePinStart = 0;
  logf("[probe] 开始找 LED：%d 个引脚，每个闪 3 次", PROBE_N);
}

static void probeLoop() {
  if (gProbeIdx < 0) {
    return;
  }
  uint32_t now = millis();

  if (gProbeIdx >= PROBE_N) {  // 收工，恢复指示灯
    digitalWrite(LED_BUILTIN, HIGH);  // D4 = 联网指示，常亮
    ledcAttachPin(LED_BUILTIN_AUX, LEDC_CH);
    ledcWrite(LEDC_CH, (uint32_t)(gLedPct * ((1 << LEDC_BITS) - 1) / 100));
    gProbeIdx = -1;
    gProbePin = -1;
    logf("[probe] 探测结束");
    return;
  }

  if (gProbePinStart == 0) {
    gProbePinStart = now;
    gProbePin = PROBE_PINS[gProbeIdx];
    pinMode(gProbePin, OUTPUT);
    logf("[probe] 第 %d/%d 个 -> GPIO%d 闪烁", gProbeIdx + 1, PROBE_N, gProbePin);
  }

  uint32_t el = now - gProbePinStart;
  if (el < 2400) {
    digitalWrite(gProbePin, ((el / 400) % 2) == 0 ? HIGH : LOW);  // 3 次闪烁
  } else {
    digitalWrite(gProbePin, LOW);
    if (el >= 3000) {
      gProbeIdx++;
      gProbePinStart = 0;
    }
  }
}

// ===================== 统一的 HTTPS 请求（DDNS / 天气共用） =====================
// 用内置 CA 证书包做真实 TLS 校验；body 为 NULL 时发 GET，否则发 POST 表单。

extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

static bool httpsRequest(const char *url, const char *body, String &resp, int &code) {
  WiFiClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start);

  HTTPClient https;
  https.setConnectTimeout(10000);
  https.setTimeout(15000);
  if (!https.begin(client, url)) {
    logf("[net] HTTPS 初始化失败: %s", url);
    return false;
  }
  if (body != NULL) {
    https.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = https.POST((uint8_t *)body, strlen(body));
  } else {
    code = https.GET();
  }
  if (code > 0) {
    resp = https.getString();
  }
  https.end();
  client.stop();
  return code == 200;
}

// 天气不在板子上取：城市名通过 /api/content 的 site.city 交给浏览器，
// 由浏览器直接请求 wttr.in（对方允许跨域），板子零负担。

// ===================== DNSPod 动态域名（DDNS） =====================
// 更新策略：只在全局 IPv6 真的变了才调 API —— DNSPod 规定"1 小时内超过 5 次
// 无变化的修改请求"会把记录锁定 1 小时，所以绝不能定时无脑刷。

#define DDNS_POLL_MS 300000UL  // 5 分钟检查一次地址变化

static bool ddnsConfigured() {
#if DDNS_ENABLE
  return strcmp(DNSPOD_TOKEN, "YOUR_ID,YOUR_TOKEN") != 0 && DDNS_DOMAIN[0] != 0 &&
         DDNS_SUBDOMAIN[0] != 0;
#else
  return false;
#endif
}


// 从 JSON 里取字段：先定位 scopeKey（比如 "records"），再取 "key":"value" 里的值。
// 这样能避开 domain 对象里那个同名但**不带引号**的 id。
static bool ddnsJsonField(const String &s, const char *key, const char *scopeKey, char *out,
                          size_t n) {
  int from = 0;
  if (scopeKey != NULL) {
    int k = s.indexOf(scopeKey);
    if (k >= 0) {
      from = k;
    }
  }
  String pat = String("\"") + key + "\":\"";
  int i = s.indexOf(pat, from);
  if (i < 0) {
    return false;
  }
  i += pat.length();
  int j = s.indexOf('"', i);
  if (j < 0) {
    return false;
  }
  size_t len = (size_t)(j - i);
  if (len > n - 1) {
    len = n - 1;
  }
  memcpy(out, s.c_str() + i, len);
  out[len] = 0;
  return true;
}

// 判断两个 IPv6 字符串是不是同一个地址（容忍前导零/大小写/:: 压缩写法差异）
static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool ipv6ToBytes(const char *s, uint8_t out[16]) {
  memset(out, 0, 16);
  int groups[8] = {0};
  int ng = 0, dcolon = -1;
  const char *p = s;
  while (*p != 0) {
    if (*p == ':') {
      if (p[1] == ':') {
        dcolon = ng;
        p += 2;
        if (*p == 0) break;
        continue;
      }
      p++;
      continue;
    }
    unsigned v = 0;
    int digits = 0;
    while (*p != 0 && *p != ':') {
      int d = hexVal(*p);
      if (d < 0) return false;
      v = (v << 4) | (unsigned)d;
      digits++;
      p++;
    }
    if (digits == 0 || digits > 4 || ng >= 8) return false;
    groups[ng++] = (int)v;
  }
  if (dcolon < 0) {
    if (ng != 8) return false;
  } else {
    int move = ng - dcolon;
    for (int i = 0; i < move; i++) {
      groups[7 - i] = groups[ng - 1 - i];
    }
    for (int i = dcolon; i < 8 - move; i++) {
      groups[i] = 0;
    }
  }
  for (int i = 0; i < 8; i++) {
    out[2 * i] = (uint8_t)(groups[i] >> 8);
    out[2 * i + 1] = (uint8_t)(groups[i] & 0xFF);
  }
  return true;
}

static bool sameIPv6(const char *a, const char *b) {
  uint8_t x[16], y[16];
  if (!ipv6ToBytes(a, x) || !ipv6ToBytes(b, y)) {
    return strcmp(a, b) == 0;
  }
  return memcmp(x, y, 16) == 0;
}

static bool ddnsResolveRecordId() {
  String body = "login_token=" + String(DNSPOD_TOKEN) + "&format=json&domain=" + DDNS_DOMAIN +
                "&sub_domain=" + DDNS_SUBDOMAIN + "&record_type=AAAA";
  String resp;
  int code = 0;
  if (!httpsRequest("https://dnsapi.cn/Record.List", body.c_str(), resp, code)) {
    logf("[ddns] 查询记录失败: HTTP %d", code);
    return false;
  }
  if (!ddnsJsonField(resp, "id", "\"records\"", gDdnsRecordId, sizeof(gDdnsRecordId))) {
    logf("[ddns] 没找到 AAAA 记录，返回: %.140s", resp.c_str());
    return false;
  }
  // 线路 ID 必须跟着记录走：DNSPod 的记录可能挂在"电信/联通/移动"等线路上，
  // 挂错线路的话普通解析根本查不到（这个坑我踩过一次）
  ddnsJsonField(resp, "line_id", "\"records\"", gDdnsLineId, sizeof(gDdnsLineId));
  ddnsJsonField(resp, "value", "\"records\"", gDdnsLastV6, sizeof(gDdnsLastV6));
  logf("[ddns] 记录 %s.%s: id=%s line_id=%s 当前值=%s", DDNS_SUBDOMAIN, DDNS_DOMAIN,
       gDdnsRecordId, gDdnsLineId, gDdnsLastV6);
  return true;
}

static bool ddnsPush(const char *ipv6) {
  String body = "login_token=" + String(DNSPOD_TOKEN) + "&format=json&domain=" + DDNS_DOMAIN +
                "&record_id=" + gDdnsRecordId + "&sub_domain=" + DDNS_SUBDOMAIN +
                "&record_line_id=" + gDdnsLineId + "&value=" + ipv6 +
                "&ttl=" + String(DDNS_TTL);
  String resp;
  int code = 0;
  if (!httpsRequest("https://dnsapi.cn/Record.Ddns", body.c_str(), resp, code)) {
    snprintf(gDdnsMsg, sizeof(gDdnsMsg), "HTTP %d 失败", code);
    return false;
  }
  if (resp.indexOf("\"code\":\"1\"") < 0) {
    snprintf(gDdnsMsg, sizeof(gDdnsMsg), "API 报错: %.56s", resp.c_str());
    return false;
  }
  snprintf(gDdnsMsg, sizeof(gDdnsMsg), "已更新为 %s", ipv6);
  return true;
}

// 没配 Token 时也做一次 TLS 探测，先把"证书/TLS 能不能通"这件事验证掉
static void ddnsTlsProbe() {
  String resp;
  int code = 0;
  bool ok = httpsRequest("https://dnsapi.cn/Record.List",
                         "login_token=1,0123456789abcdef0123456789abcdef01234567&format=json"
                         "&domain=example.com",
                         resp, code);
  logf("[ddns] TLS 探测: HTTP %d (%s)", code, ok ? "握手成功" : "失败");
  if (resp.length()) {
    logf("[ddns] DNSPod 应答: %.110s", resp.c_str());
  }
}

static void ddnsLoop() {
  static uint32_t lastCheck = 0;

  if (!ddnsConfigured()) {
    return;
  }
  uint32_t now = millis();
  // 还没成功推送过时，10 秒试一次（等 SLAAC 拿到公网地址）；成功后 5 分钟查一次
  uint32_t interval = (gDdnsLastOkMs == 0) ? 10000UL : DDNS_POLL_MS;
  if (lastCheck != 0 && now - lastCheck < interval) {
    return;
  }
  lastCheck = now;

  char v6[48];
  getV6(v6, sizeof(v6));
  if (v6[0] == '-') {
    snprintf(gDdnsMsg, sizeof(gDdnsMsg), "等待公网 IPv6");
    return;
  }
  if (gDdnsRecordId[0] == 0) {
    if (!ddnsResolveRecordId()) {
      gDdnsResult = -1;
      snprintf(gDdnsMsg, sizeof(gDdnsMsg), "记录查询失败");
      return;
    }
  }
  if (sameIPv6(v6, gDdnsLastV6)) {
    // 地址没变 —— 绝不调 API（DNSPod 规定 1 小时内超过 5 次无变化请求会锁定记录）
    gDdnsResult = 1;
    gDdnsLastOkMs = now;
    snprintf(gDdnsMsg, sizeof(gDdnsMsg), "已是最新（%s）", v6);
    return;
  }

  if (ddnsPush(v6)) {
    snprintf(gDdnsLastV6, sizeof(gDdnsLastV6), "%s", v6);
    gDdnsLastOkMs = now;
    gDdnsResult = 1;
    logf("[ddns] 更新成功: http://%s.%s:%d/", DDNS_SUBDOMAIN, DDNS_DOMAIN, HTTP_PORT);
  } else {
    gDdnsResult = -1;
    logf("[ddns] 更新失败: %s", gDdnsMsg);
  }
}


// 统一的 JSON 响应出口
static void sendJson(int fd, const JsonDocument &doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  sendHeader(fd, code, "application/json", out.length());
  sendAll(fd, out.c_str(), out.length());
}

// ===================== 请求日志（后台可查，排查请求来源）=====================
static const int REQ_LOG_SIZE = 40;

struct ReqLogEntry {
  uint32_t ms;  // 开机以来的毫秒数
  char method[6];
  char path[44];
  char peer[48];
};

static ReqLogEntry gReqLog[REQ_LOG_SIZE];
static int gReqLogIdx = 0;

static void reqLogAdd(const char *method, const char *path, const char *peer) {
  ReqLogEntry &e = gReqLog[gReqLogIdx];
  e.ms = millis();
  snprintf(e.method, sizeof(e.method), "%s", method);
  snprintf(e.path, sizeof(e.path), "%s", path);
  snprintf(e.peer, sizeof(e.peer), "%s", peer);
  gReqLogIdx = (gReqLogIdx + 1) % REQ_LOG_SIZE;
}

static void sendRequestsJson(int fd) {
  JsonDocument doc;
  doc["total"] = gReqCount;
  doc["now"] = millis() / 1000;

  JsonArray arr = doc["recent"].to<JsonArray>();
  for (int i = 0; i < REQ_LOG_SIZE; i++) {
    int idx = (gReqLogIdx - 1 - i + REQ_LOG_SIZE) % REQ_LOG_SIZE;
    const ReqLogEntry &e = gReqLog[idx];
    if (e.ms == 0) {
      continue;
    }
    JsonObject o = arr.add<JsonObject>();
    o["t"] = (long)(e.ms / 1000);
    o["m"] = e.method;
    o["p"] = e.path;
    o["ip"] = e.peer;
  }
  sendJson(fd, doc);
}

static void sendStatusJson(int fd) {
  char v6[48];
  getV6(v6, sizeof(v6));

  JsonDocument doc;
  doc["up_s"] = (millis() - gConnAt) / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["temp"] = temperatureRead();
  doc["heap"] = ESP.getFreeHeap();
  doc["heap_total"] = ESP.getHeapSize();
  doc["ipv4"] = WiFi.localIP().toString();
  doc["ipv6"] = v6;
  doc["mac"] = WiFi.macAddress();
  doc["ssid"] = WiFi.SSID();
  doc["ch"] = WiFi.channel();
  doc["chip"] = ESP.getChipModel();
  doc["cpu"] = getCpuFrequencyMhz();
  doc["flash"] = ESP.getFlashChipSize() / 1024;
  doc["led"] = gLedPct;
  doc["req"] = gReqCount;
  doc["ddns_host"] = String(DDNS_SUBDOMAIN) + "." + DDNS_DOMAIN;
  doc["ddns_state"] = gDdnsResult;
  doc["ddns_age"] = gDdnsLastOkMs ? (long)((millis() - gDdnsLastOkMs) / 1000) : -1;
  doc["ddns_msg"] = gDdnsMsg;
  sendJson(fd, doc);
}

static void sendLogJson(int fd) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < LOG_LINES; i++) {
    const char *line = gLogRing[(gLogIdx + i) % LOG_LINES];
    if (line[0]) {
      arr.add(line);
    }
  }
  sendJson(fd, doc);
}

static void sendScanJson(int fd) {
  logf("[http] WiFi 扫描开始（阻塞 2~3 秒）");
  int found = WiFi.scanNetworks();

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < found; i++) {
    JsonObject ap = arr.add<JsonObject>();
    ap["ssid"] = WiFi.SSID(i);
    ap["rssi"] = WiFi.RSSI(i);
    ap["ch"] = WiFi.channel(i);
    ap["enc"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }
  WiFi.scanDelete();
  logf("[http] WiFi 扫描完成：%d 个 AP", found);
  sendJson(fd, doc);
}

static void sendLedAck(int fd) {
  char body[64];
  int n = snprintf(body, sizeof(body), "{\"ok\":1,\"led\":%d,\"on\":%d}",
                   gLedPct, gLedOn ? 1 : 0);
  sendHeader(fd, 200, "application/json", n);
  sendAll(fd, body, n);
}

static void handleLedApi(const char *path, const char *query, int fd) {
  bool on = gLedOn;
  int pct = gLedPct > 0 ? gLedPct : DEFAULT_BRIGHT;

  if (strcmp(path, "/led/on") == 0) {
    on = true;
  } else if (strcmp(path, "/led/off") == 0) {
    on = false;
  }

  const char *p;
  if ((p = strstr(query, "on=")) != NULL) {
    on = (p[3] == '1');
  }
  if ((p = strstr(query, "bright=")) != NULL) {
    int v = atoi(p + 7);
    if (v >= 1 && v <= 100) {
      pct = v;
    }
  }

  gLedOn = on;
  gLedPct = on ? pct : 0;
  ledcWrite(LEDC_CH, (uint32_t)(gLedPct * ((1 << LEDC_BITS) - 1) / 100));
  sendLedAck(fd);
}

// ===================== 非阻塞多连接处理 =====================
// 教训：浏览器会同时开好几条连接（含"预连接"的空连接）。如果服务器在
// "连上但还没发请求"的连接上阻塞等待，真正的 API 请求就会被饿死——
// 页面上表现为一直"离线"。所以：每条连接只占一个槽位，非阻塞收数据，
// 空闲超时直接回收，主循环永远不被单条连接卡住。

static const int MAX_CLIENTS = 8;

struct Conn {
  int fd;
  uint32_t deadline;  // 空闲回收时间
  int len;
  char buf[2048];  // 请求头（浏览器那套很长）+ POST body 都要装得下
  char peer[64];
};

static Conn gConn[MAX_CLIENTS];

static void closeConn(int i) {
  if (gConn[i].fd < 0) {
    return;
  }
  int fd = gConn[i].fd;

  // 关键：关闭前先把对端还没被读走的数据读干净，再半关闭，最后才 close。
  // 否则 lwIP 会发 RST 而不是 FIN —— 浏览器就报 ERR_CONNECTION_RESET，
  // 哪怕响应其实已经发出去了（RST 会把在途数据一起丢掉）。
  shutdown(fd, SHUT_WR);
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  char tmp[128];
  for (int k = 0; k < 16; k++) {
    if (recv(fd, tmp, sizeof(tmp), 0) <= 0) {
      break;
    }
  }

  close(fd);
  gConn[i].fd = -1;
  gConn[i].len = 0;
}

static void acceptNew() {
  for (int k = 0; k < MAX_LISTENERS; k++) {
    if (gListeners[k] < 0) {
      continue;
    }
    for (int n = 0; n < MAX_CLIENTS; n++) {
      struct sockaddr_storage ss;
      socklen_t sl = sizeof(ss);
      int c = accept(gListeners[k], (struct sockaddr *)&ss, &sl);
      if (c < 0) {
        break;  // 没有新连接了
      }
      int slot = -1;
      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (gConn[i].fd < 0) {
          slot = i;
          break;
        }
      }
      if (slot < 0) {
        // 槽位满了：优先顶掉一条"连上但还没发请求"的空闲连接，
        // 而不是把新连接直接丢掉（浏览器会开好几条预连接占着槽位）
        int victim = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
          if (gConn[i].len == 0 && (victim < 0 || gConn[i].deadline < gConn[victim].deadline)) {
            victim = i;
          }
        }
        if (victim < 0) {
          close(c);  // 全都是活跃连接，只能丢弃
          continue;
        }
        closeConn(victim);
        slot = victim;
      }
      fcntl(c, F_SETFL, fcntl(c, F_GETFL, 0) | O_NONBLOCK);  // 明确非阻塞
      gConn[slot].fd = c;
      gConn[slot].len = 0;
      gConn[slot].deadline = millis() + 3000;  // 3 秒还没发请求就回收
      fmtPeer(&ss, gConn[slot].peer, sizeof(gConn[slot].peer));
    }
  }
}

// 请求行收齐了：把这条连接切回阻塞（保证大响应能完整发出去），处理完关闭
static void serveConn(int i) {
  int fd = gConn[i].fd;
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl >= 0) {
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
  }
  struct timeval tv = {};
  tv.tv_sec = 3;
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  // 解析 "METHOD /path?query HTTP/1.1"，POST 还要取出 body
  char method[8] = "GET";
  char raw[192] = "/";
  sscanf(gConn[i].buf, "%7s %191s", method, raw);
  char path[128] = "/";
  char query[128] = "";
  char *q = strchr(raw, '?');
  if (q != NULL) {
    *q = 0;
    snprintf(query, sizeof(query), "%s", q + 1);
  }
  snprintf(path, sizeof(path), "%s", raw);

  const char *body = NULL;
  char *hdrEnd = strstr(gConn[i].buf, "\r\n\r\n");
  if (hdrEnd != NULL) {
    body = hdrEnd + 4;
    if (*body == 0) {
      body = NULL;  // 没带请求体
    }
  }

  reqLogAdd(method, path, gConn[i].peer);
  gReqCount++;
  snprintf(gLastPeer, sizeof(gLastPeer), "%s", gConn[i].peer);
  logf("[http] %s %s  <- %s", method, path, gConn[i].peer);

  // ---------------- 路由 ----------------
  if (strcmp(path, "/api/status") == 0 || strcmp(path, "/json") == 0) {
    sendStatusJson(fd);
  } else if (strcmp(path, "/api/requests") == 0) {
    sendRequestsJson(fd);
  } else if (strcmp(path, "/api/log") == 0) {
    sendLogJson(fd);
  } else if (strcmp(path, "/api/content") == 0) {
    if (strcmp(method, "POST") == 0) {
      if (body != NULL) {
        handleContentPost(fd, body, query);
      } else {
        sendText(fd, 400, "application/json", "{\"ok\":0,\"msg\":\"缺少请求体\"}");
      }
    } else {
      sendContentJson(fd);
    }
  } else if (strcmp(path, "/admin") == 0 || strcmp(path, "/edit") == 0) {
    size_t len = sizeof(ADMIN_PAGE) - 1;  // 内容编辑页（工具页）
    sendHeader(fd, 200, "text/html; charset=utf-8", len);
    sendAll(fd, ADMIN_PAGE, len);
  } else if (strcmp(path, "/api/scan") == 0) {
    sendScanJson(fd);
  } else if (strcmp(path, "/api/ledtest") == 0) {
    probeStart();
    sendText(fd, 200, "application/json", "{\"ok\":1,\"msg\":\"probe started\"}");
  } else if (strcmp(path, "/api/led") == 0 || strcmp(path, "/led/on") == 0 ||
             strcmp(path, "/led/off") == 0) {
    handleLedApi(path, query, fd);
  } else if (strcmp(path, "/favicon.ico") == 0) {
    // 站点图标交给浏览器直接去取（可指向自己的图库），板子不转发字节
    if (gFaviconUrl[0]) {
      sendRedirect(fd, gFaviconUrl);
    } else {
      sendText(fd, 204, "image/x-icon", "");
    }
  } else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
    size_t len = sizeof(WEB_PAGE) - 1;  // 页面内嵌在固件里，不占 RAM
    sendHeader(fd, 200, "text/html; charset=utf-8", len);
    sendAll(fd, WEB_PAGE, len);
  } else {
    sendText(fd, 404, "text/plain; charset=utf-8", "404 not found\n");
  }

  closeConn(i);
}

static bool requestReady(int i) {
  char *hdrEnd = strstr(gConn[i].buf, "\r\n\r\n");
  if (hdrEnd == NULL) {
    return gConn[i].len >= (int)sizeof(gConn[i].buf) - 1;  // 头都塞满了，硬着头皮处理
  }
  int bodyStart = (int)(hdrEnd - gConn[i].buf) + 4;

  // 小写/大写都要认：Content-Length
  int need = 0;
  for (char *p = gConn[i].buf; p < hdrEnd; p++) {
    if (tolower((unsigned char)p[0]) == 'c' &&
        strncasecmp(p, "content-length:", 15) == 0) {
      need = atoi(p + 15);
      break;
    }
  }
  return gConn[i].len >= bodyStart + need;
}

static void pollServer() {
  acceptNew();

  uint32_t now = millis();
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (gConn[i].fd < 0) {
      continue;
    }

    char tmp[256];
    int r = recv(gConn[i].fd, tmp, sizeof(tmp), 0);

    if (r > 0) {
      int room = (int)sizeof(gConn[i].buf) - 1 - gConn[i].len;
      if (r > room) {
        r = room;
      }
      if (r > 0) {
        memcpy(gConn[i].buf + gConn[i].len, tmp, r);
        gConn[i].len += r;
        gConn[i].buf[gConn[i].len] = 0;
      }
      // 必须等整个请求收完再响应：
      //  - 请求头要到 \r\n\r\n（只读请求行就回包的话，残余请求头会让 close 触发 RST）
      //  - POST 还要等 body 收够 Content-Length 指定的字节数
      if (requestReady(i)) {
        serveConn(i);
      }
    } else if (r == 0) {
      closeConn(i);  // 对端已关闭
    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      closeConn(i);  // 真出错
    } else if ((int32_t)(now - gConn[i].deadline) >= 0) {
      closeConn(i);  // 空闲太久（浏览器预连接），直接回收
    }
  }
}


static void printConnectionInfo() {
  logf("[wifi] connected! %lu ms", (unsigned long)(millis() - gConnAt));
  logf("       SSID    : %s", WiFi.SSID().c_str());
  logf("       IP      : %s", WiFi.localIP().toString().c_str());
  logf("       gateway : %s   subnet %s", WiFi.gatewayIP().toString().c_str(),
       WiFi.subnetMask().toString().c_str());
  logf("       DNS     : %s", WiFi.dnsIP().toString().c_str());
  logf("       RSSI    : %d dBm   channel %d", WiFi.RSSI(), WiFi.channel());
  logf("       MAC     : %s", WiFi.macAddress().c_str());
  logf("       hostname: %s", WiFi.getHostname());
  logf("       -> now try:  ping %s", WiFi.localIP().toString().c_str());
}

static void beginConnect() {
  gAttempt++;
  gConnAt = millis();
  gLastRetry = millis();
  logf("[wifi] attempt #%lu -> \"%s\" ...", (unsigned long)gAttempt, WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    gConn[i].fd = -1;  // 连接槽位初始化（0 是合法 fd，必须显式置 -1）
  }
  // D5 交给硬件 LEDC PWM：占空比由网页控制，CPU 不参与
  ledcSetup(LEDC_CH, 5000, LEDC_BITS);
  ledcAttachPin(LED_BUILTIN_AUX, LEDC_CH);
  ledcWrite(LEDC_CH, 0);

  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.begin(115200, SERIAL_8N1, 20, 21);
#endif
  delay(400);

  logf("");
  logf("===== CORE-ESP32C3 WiFi auto-connect =====");
  logf("chip %s rev%d  cpu %lu MHz  heap %lu B",
       ESP.getChipModel(), ESP.getChipRevision(), (unsigned long)getCpuFrequencyMhz(),
       (unsigned long)ESP.getFreeHeap());

  if (strcmp(WIFI_SSID, PLACEHOLDER_SSID) == 0) {
    gConfigured = false;
    logf("");
    logf("!! WiFi 还没配置 !!");
    logf("!! 打开 include/wifi_config.h，把 WIFI_SSID / WIFI_PASSWORD 填上，再 Upload !!");
    return;
  }

  gConfigured = true;
  contentBegin();  // 从 flash 读取网页列表 / 联系方式
  WiFi.persistent(false);        // 不要每次都把参数写进 flash
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("core-esp32c3");  // 路由器客户端列表里好认
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(WIFI_MODEM_SLEEP);  // 见 wifi_config.h：0=响应快，1=省电但延迟几百 ms
  beginConnect();
}

void loop() {
  uint32_t now = millis();

  if (!gConfigured) {
    // 没填 WiFi：1Hz 慢闪，串口不再刷屏
    if (now - gLastBlink >= 1000) {
      gLastBlink = now;
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
    return;
  }

  wl_status_t st = WiFi.status();

  if (!gConnected) {
    // ---- 连接中：100ms 快闪 ----
    if (now - gLastBlink >= 100) {
      gLastBlink = now;
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }

    if (st == WL_CONNECTED) {
      gConnected = true;
      digitalWrite(LED_BUILTIN, HIGH);      // D4 常亮 = 网络已连接
      gLastRssi = now;
      printConnectionInfo();
      if (!gIpv6Tried) {
        gIpv6Tried = WiFi.enableIpV6();  // 启动 IPv6：link-local + SLAAC 自动配置
        logf("[wifi] IPv6 %s", gIpv6Tried ? "已启用" : "启用失败");
      }
      printIPv6Status();
      startServer();
      // 把可直接收藏的后台链接打出来（带令牌，省得手输）
      if (strlen(WRITE_TOKEN) > 0) {
        logf("[http] 后台管理: http://%s:%d/admin?k=%s", WiFi.localIP().toString().c_str(),
             HTTP_PORT, WRITE_TOKEN);
      } else {
        logf("[http] 后台管理: http://%s:%d/admin （未设令牌，任何人都能修改内容）",
             WiFi.localIP().toString().c_str(), HTTP_PORT);
      }
      if (ddnsConfigured()) {
        logf("[ddns] 已配置：%s.%s（Token 已就位）", DDNS_SUBDOMAIN, DDNS_DOMAIN);
      } else {
        static bool probed = false;
        if (!probed) {
          probed = true;
          ddnsTlsProbe();  // 没配 Token 也先验证 TLS 通不通
        }
      }
    } else if (now - gLastRetry >= RETRY_MS) {
      logf("[wifi] still not connected: %s", statusName(st));
      WiFi.disconnect();
      delay(50);
      beginConnect();
    }
    return;
  }

  // ---- 已连接：D4 常亮，CPU 不再碰这个脚 ----
  if (st != WL_CONNECTED) {
    gConnected = false;
    gAttempt = 0;
    digitalWrite(LED_BUILTIN, LOW);       // 掉线 -> 灭
    logf("[wifi] connection lost, reconnecting ...");
    beginConnect();
    return;
  }

  // 处理 HTTP 请求（非阻塞 accept，不会拖住别的活）
  pollServer();

  // LED 探测（诊断用，未运行时是空转）
  probeLoop();

  // 动态域名：地址变了才更新
  ddnsLoop();


  // 每 30 秒报一次状态，顺便看信号强弱（放板子位置时可以对着看）
  if (now - gLastRssi >= RSSI_MS) {
    gLastRssi = now;
    logf("[wifi] up %lu s | RSSI %d dBm | IP %s | heap %lu B",
         (unsigned long)((now - gConnAt) / 1000), WiFi.RSSI(),
         WiFi.localIP().toString().c_str(), (unsigned long)ESP.getFreeHeap());
    printIPv6Status();  // SLAAC 可能要几秒到几十秒，每次心跳再查一遍

    // 全局 IPv6 就绪后，把外网访问地址打出来（只打一次）
    static bool sUrlShown = false;
    esp_netif_t *n6 = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_ip6_addr_t g6;
    if (n6 && esp_netif_get_ip6_global(n6, &g6) == ESP_OK) {
      logf("[http] 外网: http://[" IPV6STR "]:%d/", IPV62STR(g6), HTTP_PORT);
      sUrlShown = true;
    } else if (!sUrlShown) {
      logf("[http] 还没有全局 IPv6，外网访问暂时不可用");
    }
  }

  delay(2);  // 别空转烧 CPU；对 HTTP 的响应速度毫无影响
}