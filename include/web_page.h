// ============================================================
//  首页 —— 由板子通过 HTTP 提供给浏览器（内嵌固件，无外部依赖）
//
//  内容来自后端 /api/content（在 /admin 用表单修改，无需重烧固件）：
//    profile   网名 / 头像 URL / 一句话 / 背景图 URL
//    urls      网页列表（数量任意，1 个也居中，多则自动换行）
//    contacts  联系方式（img 图片优先，没有则显示 icon）
//    timeline  时间树（可滚动、无外框）
//  实时数据来自 /api/status（每秒刷新）
//
//  主题：右下角可切换亮色/暗色，选择记在浏览器 localStorage。
//
//  注意：内容里不能出现 )HTML" 这个结束标记。
// ============================================================

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="zh-CN" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<link id="favicon" rel="icon" href="data:,">
<title>ESP32-C3 小站</title>
<style>
:root{
  --card: rgba(255,255,255,.055);
  --card-hover: rgba(255,255,255,.10);
  --line: rgba(255,255,255,.13);
  --text: #f2f4f8;
  --sub: rgba(242,244,248,.62);
  --sub2: rgba(242,244,248,.40);
  --dot: #8fd6a8;
  --bg: linear-gradient(165deg,#1b1d24 0%,#101116 55%,#0a0b0e 100%);
  --veil: rgba(8,9,12,.52);
  --page: #101116;
  --radius: 14px;
}
html[data-theme="light"]{
  --card: rgba(15,20,30,.035);
  --card-hover: rgba(15,20,30,.07);
  --line: rgba(15,20,30,.10);
  --text: #1b1f27;
  --sub: rgba(27,31,39,.62);
  --sub2: rgba(27,31,39,.42);
  --dot: #2f9e68;
  --bg: linear-gradient(165deg,#fcfdff 0%,#f1f3f7 60%,#e9ecf1 100%);
  --veil: rgba(255,255,255,.35);
  --page: #f4f5f8;
}
*{box-sizing:border-box}
html,body{margin:0;padding:0}
body{
  min-height:100vh;color:var(--text);background:var(--page);
  font:14px/1.62 -apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Hiragino Sans GB","Microsoft YaHei",sans-serif;
  -webkit-font-smoothing:antialiased;transition:background .25s ease,color .25s ease;
}
#bg{position:fixed;inset:0;z-index:-2;background:var(--bg);background-size:cover;background-position:center}
#bg.hasimg::after{content:"";position:absolute;inset:0;background:var(--veil)}

.page{max-width:1060px;margin:0 auto;padding:40px 22px 56px;min-height:100vh;
  display:grid;grid-template-columns:252px 1fr;gap:32px;align-content:center;align-items:start}
@media(max-width:860px){ .page{grid-template-columns:1fr;gap:22px;padding-top:30px} }

.card{background:var(--card);border:1px solid var(--line);border-radius:var(--radius)}
.pad{padding:16px 18px}

/* ---------------- 左栏 ---------------- */
.side{text-align:center}
.avatar{width:104px;height:104px;border-radius:50%;margin:0 auto;overflow:hidden;
  display:grid;place-items:center;font:600 30px/1 ui-monospace,monospace;
  background:var(--card);border:1px solid var(--line)}
.avatar img{width:100%;height:100%;object-fit:cover;display:block}
.name{margin:15px 0 3px;font-size:23px;font-weight:600;letter-spacing:.01em}
.tagline{margin:0;font-size:12.5px;color:var(--sub)}
/* 个人标签：无边框，用圆点分隔 */
.tags{display:flex;flex-wrap:wrap;justify-content:center;gap:5px;margin-top:7px;
  font-size:12.5px;color:var(--sub)}
.tags .sep{color:var(--sub2);opacity:.7}
.side .card{margin-top:16px;text-align:left;font-size:12.5px}
.kv{display:flex;justify-content:space-between;gap:12px;padding:3px 0}
.kv span{color:var(--sub2)}
.kv b{font-weight:500;color:var(--text);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.contacts{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;margin-top:16px}
.contact{width:34px;height:34px;border-radius:50%;display:grid;place-items:center;font-size:15px;
  background:var(--card);border:1px solid var(--line);overflow:hidden;text-decoration:none;
  color:var(--text);transition:background .15s ease,transform .15s ease}
.contact:hover{background:var(--card-hover);transform:translateY(-2px)}
/* 用图片当图标时，图片本身就是图标，不再套圆圈边框 */
.contact.hasimg{background:transparent;border-color:transparent}
.contact.hasimg:hover{background:transparent;opacity:.85}
.contact.pending{opacity:.38;cursor:default}
.contact.pending:hover{transform:none;background:var(--card)}
.contact.pending.hasimg:hover{background:transparent}
.contact img{width:100%;height:100%;object-fit:cover;display:block}

/* ---------------- 右栏 ---------------- */
/* 时钟与天气：无外框、内容居中 */
.row{display:grid;grid-template-columns:1.1fr .9fr;gap:26px;margin-bottom:30px;align-items:start}
@media(max-width:680px){ .row{grid-template-columns:1fr;gap:20px} }
.plain{padding:2px 0;text-align:center}
.clock-time{font:600 44px/1.12 ui-monospace,SFMono-Regular,Menlo,monospace;letter-spacing:.01em}
.clock-date{font-size:12.5px;color:var(--sub);margin-top:8px}
/* 天气 */
.weather .w-city{font-size:12.5px;color:var(--sub2)}
.weather .w-temp{font:600 34px/1.25 ui-monospace,SFMono-Regular,Menlo,monospace;margin-top:2px}
.weather .w-temp small{font-size:14px;font-weight:400;color:var(--sub);margin-left:2px}
.weather .w-desc{font-size:13.5px;margin-top:2px}
.weather .w-extra{font-size:11.5px;color:var(--sub2);margin-top:7px}

.section{margin-bottom:28px}
.section-title{margin:0 0 12px;font-size:12px;font-weight:500;color:var(--sub2);letter-spacing:.12em}
/* 网页列表：自适应。1 个居中、3 个一行、多了自动换行，都不会出现半空的一行 */
.links{display:flex;flex-wrap:wrap;gap:12px;justify-content:center}
.link{flex:1 1 190px;max-width:100%;min-height:58px;display:flex;align-items:center;
  justify-content:center;gap:9px;padding:12px 16px;border-radius:12px;text-decoration:none;
  color:var(--text);background:var(--card);border:1px solid var(--line);
  transition:background .15s ease,border-color .15s ease,transform .15s ease}
@media(min-width:681px){ .link{flex:0 1 208px} }
.link:hover{background:var(--card-hover);border-color:var(--line);transform:translateY(-2px)}
.link .ic{font-size:17px;line-height:1}
.link .thumb{width:22px;height:22px;border-radius:5px;object-fit:cover}
.link .nm{font-size:13.5px;font-weight:500;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}

/* 时间树：无外框、可滚动 */
.tl-wrap{max-height:236px;overflow-y:auto;padding-right:6px}
.tl-wrap::-webkit-scrollbar{width:5px}
.tl-wrap::-webkit-scrollbar-thumb{background:var(--line);border-radius:5px}
.tl{position:relative;padding-left:20px}
.tl::before{content:"";position:absolute;left:4px;top:8px;bottom:8px;width:1px;background:var(--line)}
.ev{position:relative;padding-bottom:15px}
.ev:last-child{padding-bottom:0}
.ev::before{content:"";position:absolute;left:-19.5px;top:6px;width:7px;height:7px;border-radius:50%;
  background:var(--page);border:1px solid var(--line)}
.ev.latest::before{background:var(--dot);border-color:var(--dot)}
.ev .ti{font-size:13.5px;font-weight:500}
.ev .dt{font-size:11.5px;color:var(--sub2);margin-top:2px}
.ev .de{font-size:12.5px;color:var(--sub);margin-top:3px}
.empty{font-size:12.5px;color:var(--sub2)}
.foot{margin-top:4px;font-size:11.5px;color:var(--sub2)}

/* 右下角主题切换 */
.theme-toggle{position:fixed;right:20px;bottom:20px;width:38px;height:38px;border-radius:50%;
  display:grid;place-items:center;font-size:16px;cursor:pointer;color:var(--text);
  background:var(--card);border:1px solid var(--line);
  -webkit-backdrop-filter:blur(8px);backdrop-filter:blur(8px);
  transition:background .15s ease,transform .15s ease}
.theme-toggle:hover{background:var(--card-hover);transform:translateY(-2px)}
</style>
</head>
<body>
<div id="bg"></div>

<div class="page">
  <!-- ---------- 左栏 ---------- -->
  <aside class="side">
    <div class="avatar" id="avatar"><span id="avatar-fallback">E</span></div>
    <h1 class="name" id="name">ESP32</h1>
    <p class="tagline" id="tagline"></p>
    <div class="tags" id="tags"></div>

    <div class="card pad">
      <div class="kv"><span>设备</span><b id="i-chip">--</b></div>
      <div class="kv"><span>域名</span><b id="i-host">--</b></div>
      <div class="kv"><span>在线</span><b id="i-uptime">--</b></div>
    </div>

    <nav class="contacts" id="contacts" aria-label="联系方式"></nav>
  </aside>

  <!-- ---------- 右栏 ---------- -->
  <main>
    <div class="row">
      <section class="plain clock">
        <div class="clock-time" id="clock">--:--:--</div>
        <div class="clock-date" id="date">—</div>
      </section>

      <section class="plain weather">
        <div class="w-city" id="w-city">天气</div>
        <div class="w-temp" id="w-temp">--<small>°C</small></div>
        <div class="w-desc" id="w-desc">获取中…</div>
        <div class="w-extra" id="w-extra"></div>
      </section>
    </div>

    <section class="section">
      <h2 class="section-title">网页列表</h2>
      <div class="links" id="links"></div>
    </section>

    <section class="section">
      <h2 class="section-title" id="timeline-title">时间树</h2>
      <div class="tl-wrap"><div class="tl" id="timeline"></div></div>
    </section>

    <div class="foot" id="foot"></div>
  </main>
</div>

<button class="theme-toggle" id="theme-toggle" type="button" aria-label="切换亮色/暗色">☀</button>

<script>
(function () {
  "use strict";

  var WEEKDAYS = ["日", "一", "二", "三", "四", "五", "六"];
  var $ = function (sel) { return document.querySelector(sel); };

  /* ================= 主题切换 ================= */
  function applyTheme(theme) {
    document.documentElement.dataset.theme = theme;
    $("#theme-toggle").textContent = theme === "light" ? "☾" : "☀";
    try { localStorage.setItem("theme", theme); } catch (e) {}
  }
  (function initTheme() {
    var saved = null;
    try { saved = localStorage.getItem("theme"); } catch (e) {}
    if (!saved) {
      saved = window.matchMedia && matchMedia("(prefers-color-scheme: light)").matches
        ? "light" : "dark";
    }
    applyTheme(saved);
  })();
  $("#theme-toggle").addEventListener("click", function () {
    applyTheme(document.documentElement.dataset.theme === "light" ? "dark" : "light");
  });

  /* ================= 小工具 ================= */
  function pad2(n) { return n < 10 ? "0" + n : String(n); }

  function formatDuration(seconds) {
    var s = Math.floor(seconds);
    var d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600);
    var m = Math.floor(s % 3600 / 60), sec = s % 60;
    if (d) return d + " 天 " + h + " 小时";
    if (h) return h + " 小时 " + m + " 分";
    if (m) return m + " 分 " + sec + " 秒";
    return sec + " 秒";
  }

  function make(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text != null) node.textContent = text;
    return node;
  }

  /* 外链图片失败时自动回落（头像→首字母，图标→emoji），避免破图 */
  function loadImage(url, onOk, onFail) {
    var img = new Image();
    img.onload = function () { onOk(img); };
    img.onerror = onFail || function () {};
    img.src = url;
  }

  /* ================= 渲染后台内容 ================= */
  /* 站点信息：浏览器标签页的名字与图标 */
  function renderSite(site, fallbackName) {
    if (site && site.title) {
      document.title = site.title;
    } else if (fallbackName) {
      document.title = fallbackName;
    }
    if (site && site.favicon) {
      var link = document.getElementById("favicon");
      if (link) link.href = site.favicon;
    }
    if (site && site.city) {
      loadWeather(site.city);
      setInterval(function () { loadWeather(site.city); }, 10 * 60 * 1000);  // 10 分钟刷新
    }
  }

  function renderProfile(p) {
    if (!p || typeof p !== "object") return;

    var name = p.name || "ESP32";
    $("#name").textContent = name;
    $("#tagline").textContent = p.tagline || "";

    // 个人标签：无边框，用 • 分隔
    var tagBox = $("#tags");
    tagBox.innerHTML = "";
    (p.tags || []).forEach(function (t, i) {
      if (!t) return;
      if (tagBox.children.length) tagBox.appendChild(make("span", "sep", "•"));
      tagBox.appendChild(make("span", "tg", t));
    });

    var fallback = $("#avatar-fallback");
    fallback.textContent = name.replace(/[^\w\u4e00-\u9fa5]/g, "").slice(0, 2).toUpperCase() || "U";
    if (p.avatar) {
      loadImage(p.avatar, function (img) {
        var box = $("#avatar");
        box.innerHTML = "";
        box.appendChild(img);
      });
    }
    if (p.bg) {
      loadImage(p.bg, function () {
        var bg = $("#bg");
        bg.style.backgroundImage = "url('" + p.bg + "')";
        bg.classList.add("hasimg");
      });
    }
  }

  function renderContacts(list) {
    var box = $("#contacts");
    box.innerHTML = "";
    (list || []).forEach(function (c) {
      if (!c) return;
      // 还没填链接的：显示成暗色不可点，方便后台补链接时能看到位置
      var node = document.createElement(c.url ? "a" : "span");
      node.className = "contact" + (c.url ? "" : " pending");
      if (c.url) {
        node.href = c.url;
        node.target = "_blank";
        node.rel = "noopener noreferrer";
      } else {
        node.title = (c.name || "") + "（未设置链接）";
      }
      node.setAttribute("aria-label", c.name || "contact");
      // 图标可选：图片 > emoji > 名字首字母
      if (c.img) {
        loadImage(c.img, function (img) {
          img.alt = c.name || "";
          node.appendChild(img);
        }, function () { node.textContent = contactFallback(c); });
      } else {
        node.textContent = contactFallback(c);
      }
      box.appendChild(node);
    });
  }

  function contactFallback(c) {
    if (c.icon) return c.icon;
    return (c.name || "?").trim().slice(0, 1).toUpperCase();
  }

  function renderLinks(list) {
    var box = $("#links");
    box.innerHTML = "";
    (list || []).forEach(function (u) {
      if (!u || !u.url) return;
      var a = make("a", "link");
      a.href = u.url;
      if (/^https?:/i.test(u.url)) {
        a.target = "_blank";
        a.rel = "noopener noreferrer";
      }
      a.title = u.desc || u.name || "";
      // 图标是可选的：填了图片用图片，填了 emoji 用 emoji，都没填就只显示名字
      if (u.img) {
        loadImage(u.img, function (img) {
          img.className = "thumb";
          img.alt = "";
          a.insertBefore(img, a.firstChild);
        });
      } else if (u.icon) {
        a.insertBefore(make("span", "ic", u.icon), a.firstChild);
      }
      a.appendChild(make("span", "nm", u.name || u.url));
      box.appendChild(a);
    });
    if (!box.children.length) {
      box.appendChild(make("div", "empty", "还没有配置，去 /admin 添加"));
    }
  }

  function renderTimeline(list) {
    var box = $("#timeline");
    box.innerHTML = "";
    // 顺序完全由后台决定（所见即所得），页面不再自作主张倒序
    var items = (list || []).slice();
    items.forEach(function (e, i) {
      if (!e) return;
      var row = make("div", "ev" + (i === 0 ? " latest" : ""));
      row.appendChild(make("div", "ti", e.title || ""));
      if (e.date) row.appendChild(make("div", "dt", e.date));
      if (e.desc) row.appendChild(make("div", "de", e.desc));
      box.appendChild(row);
    });
    if (!box.children.length) box.appendChild(make("div", "empty", "还没有配置时间树"));
    $("#timeline-title").textContent = items.length ? "时间树 · " + items.length + " 条" : "时间树";
  }

  function loadContent() {
    fetch("/api/content", { cache: "no-store" })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        renderProfile(j.profile);
        renderSite(j.site, (j.profile || {}).name);
        renderContacts(j.contacts);
        renderLinks(j.urls);
        renderTimeline(j.timeline);
      })
      .catch(function () { /* 读取失败保留上一次渲染 */ });
  }

  /* ================= 天气：浏览器直接请求 Open-Meteo（免费、无需 key），板子零负担 =================
     两步：先按城市名做地理编码拿到经纬度，再取当前天气。
     注意：请填"市级"名称（如 Beijing、Shanghai），省级/大地名可能被解析成同名小地方。 */
  var WMO_ZH = {
    0: "晴", 1: "大部晴朗", 2: "局部多云", 3: "阴", 45: "雾", 48: "雾凇",
    51: "小毛毛雨", 53: "毛毛雨", 55: "大毛毛雨", 56: "冻毛毛雨", 57: "冻毛毛雨",
    61: "小雨", 63: "中雨", 65: "大雨", 66: "冻雨", 67: "冻雨",
    71: "小雪", 73: "中雪", 75: "大雪", 77: "米雪",
    80: "小阵雨", 81: "中阵雨", 82: "强阵雨", 85: "小阵雪", 86: "大阵雪",
    95: "雷阵雨", 96: "雷阵雨伴冰雹", 99: "雷阵雨伴冰雹"
  };

  function loadWeather(city) {
    if (!city) {
      $("#w-desc").textContent = "未设置城市";
      return;
    }
    var geoUrl = "https://geocoding-api.open-meteo.com/v1/search?name=" +
                 encodeURIComponent(city) + "&count=1&language=zh&format=json";

    fetch(geoUrl, { cache: "no-store" })
      .then(function (r) { return r.json(); })
      .then(function (g) {
        var hit = (g.results || [])[0];
        if (!hit) throw new Error("找不到这个城市");
        var wxUrl = "https://api.open-meteo.com/v1/forecast?latitude=" + hit.latitude +
                    "&longitude=" + hit.longitude +
                    "&current=temperature_2m,relative_humidity_2m,apparent_temperature," +
                    "weather_code,wind_speed_10m&timezone=auto";
        return fetch(wxUrl, { cache: "no-store" })
          .then(function (r) { return r.json(); })
          .then(function (j) { return { hit: hit, wx: j }; });
      })
      .then(function (o) {
        var cur = o.wx.current || {};
        var where = o.hit.name || city;
        if (o.hit.admin1 && o.hit.admin1 !== o.hit.name) where += " · " + o.hit.admin1;
        $("#w-city").textContent = where;
        $("#w-temp").innerHTML = Math.round(cur.temperature_2m) + "<small>°C</small>";
        $("#w-desc").textContent = WMO_ZH[cur.weather_code] || ("天气码 " + cur.weather_code);

        var extra = [];
        if (cur.apparent_temperature != null) extra.push("体感 " + Math.round(cur.apparent_temperature) + " °C");
        if (cur.relative_humidity_2m != null) extra.push("湿度 " + cur.relative_humidity_2m + "%");
        if (cur.wind_speed_10m != null) extra.push("风速 " + Math.round(cur.wind_speed_10m) + " km/h");
        extra.push("数据来自 Open-Meteo");
        $("#w-extra").textContent = extra.join(" · ");
      })
      .catch(function () {
        $("#w-desc").textContent = "天气获取失败";
        $("#w-extra").textContent = "（浏览器需要能访问外网；城市建议填市级名称）";
      });
  }

  /* ================= 实时状态 ================= */
  function tick() {
    var d = new Date();
    $("#clock").textContent = pad2(d.getHours()) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds());
    $("#date").textContent = d.getFullYear() + " 年 " + (d.getMonth() + 1) + " 月 " + d.getDate() +
      " 日 星期" + WEEKDAYS[d.getDay()];

    fetch("/api/status", { cache: "no-store" })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        $("#i-chip").textContent = j.chip + " · " + j.cpu + " MHz";
        $("#i-host").textContent = j.ddns_host || "--";
        $("#i-uptime").textContent = formatDuration(j.up_s);
        $("#foot").textContent = (j.ddns_host || "") + " · 页面由 ESP32-C3 直接提供 · " + j.req + " 次请求";
      })
      .catch(function () { /* 离线时保持原样 */ });
  }

  /* 轮询：页面在前台时 1 秒一次；切到后台自动降到 15 秒一次，
     免得几个标签页挂着就把板子的请求数拉爆 */
  function scheduleTick() {
    tick();
    setTimeout(scheduleTick, document.hidden ? 15000 : 1000);
  }
  document.addEventListener("visibilitychange", function () {
    if (!document.hidden) tick();      // 切回前台立刻刷新一次
  });

  loadContent();
  scheduleTick();
  setInterval(loadContent, 30000);   // 后台改完内容后最多 30 秒自动生效
})();
</script>
</body>
</html>
)HTML";
