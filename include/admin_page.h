// ============================================================
//  后台管理页 /admin —— 表单式编辑，改完写进板子 flash，无需重烧固件
//
//  四块内容：
//    profile   侧写：网名 / 一句话 / 头像图片 URL / 背景图片 URL
//    urls      网页列表：图标、图片 URL、名字、链接、说明
//    contacts  联系方式：图标、图片 URL、名字、链接
//    timeline  时间树：日期、标题、说明
//
//  表单结构由 JS 里的 SCHEMA 描述，新增字段只需改 SCHEMA，不用改模板代码。
//  注意：内容里不能出现 )HTML" 这个结束标记。
// ============================================================

static const char ADMIN_PAGE[] = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<link id="favicon" rel="icon" href="data:,">
<title>后台管理</title>
<style>
:root{
  --bg:#f4f5f7; --card:#fff; --line:#e3e6ec; --text:#1b1f27;
  --sub:#6b7480; --accent:#2f6df6; --danger:#d9534f;
}
*{box-sizing:border-box}
body{margin:0;padding:26px 20px 90px;background:var(--bg);color:var(--text);
  font:14px/1.6 -apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif}
.wrap{max-width:960px;margin:0 auto}
h1{margin:0 0 4px;font-size:20px}
.sub{margin:0 0 22px;font-size:13px;color:var(--sub)}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px 18px;margin-bottom:16px}
.card h2{margin:0 0 12px;font-size:13px;font-weight:600;color:var(--sub);letter-spacing:.06em;
  display:flex;align-items:center;justify-content:space-between}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}
label{display:block;font-size:12px;color:var(--sub);margin-bottom:5px}
input{width:100%;padding:8px 10px;font:inherit;color:var(--text);background:#fbfcfd;
  border:1px solid var(--line);border-radius:8px}
input:focus{outline:none;border-color:var(--accent);background:#fff}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:flex-end;padding:12px;border:1px solid var(--line);
  border-radius:10px;margin-bottom:10px;background:#fbfcfd}
.row .f{display:flex;flex-direction:column}
.row .f.grow{flex:1 1 auto;min-width:160px}
.del{flex:0 0 auto;height:34px;padding:0 12px;border-radius:8px;border:1px solid var(--line);
  background:#fff;color:var(--danger);cursor:pointer;font:inherit}
.del:hover{background:#fff5f5;border-color:#f0c9c8}
.move{flex:0 0 auto;width:32px;height:34px;border-radius:8px;border:1px solid var(--line);
  background:#fff;color:var(--sub);cursor:pointer;font:14px/1 inherit;padding:0}
.move:hover:not(:disabled){background:#f5f8ff;border-color:#c9dbff;color:var(--accent)}
.move:disabled{opacity:.35;cursor:default}
.hbtns{display:flex;gap:8px}
.add{padding:5px 12px;border-radius:8px;border:1px solid var(--line);background:#fff;
  color:var(--accent);cursor:pointer;font:12px/1.4 inherit}
.add:hover{background:#f5f8ff;border-color:#c9dbff}
.bar{position:fixed;left:0;right:0;bottom:0;background:rgba(255,255,255,.94);
  border-top:1px solid var(--line);padding:12px 20px;display:flex;align-items:center;gap:14px;
  -webkit-backdrop-filter:blur(8px);backdrop-filter:blur(8px)}
.bar .inner{max-width:960px;margin:0 auto;display:flex;align-items:center;gap:14px;width:100%}
button.primary{padding:9px 22px;border-radius:9px;border:1px solid var(--accent);background:var(--accent);
  color:#fff;font:600 14px inherit;cursor:pointer}
button.primary:hover{filter:brightness(1.06)}
#status{font-size:13px;color:var(--sub)}
#status.ok{color:#1f8a4c}
#status.err{color:var(--danger)}
.hint{font-size:11.5px;color:var(--sub);margin-top:6px}
/* 开发板状态网格 */
.devgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:8px 18px;font-size:12.5px}
.devgrid .kv2{display:flex;justify-content:space-between;gap:10px;padding:3px 0;border-bottom:1px dashed var(--line)}
.devgrid .kv2 span{color:var(--sub);white-space:nowrap}
.devgrid .kv2 b{font-weight:500;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-family:ui-monospace,monospace}
.ok2{color:#1f8a4c}
.err2{color:var(--danger)}

/* 请求日志表格 */
.tablewrap{max-height:280px;overflow:auto;border:1px solid var(--line);border-radius:10px;margin-top:10px}
table{width:100%;border-collapse:collapse;font-size:12px}
th,td{text-align:left;padding:6px 10px;border-bottom:1px solid var(--line);white-space:nowrap}
th{position:sticky;top:0;background:#fbfcfd;color:var(--sub);font-weight:600;z-index:1}
td.ip{color:var(--sub);font-family:ui-monospace,monospace;font-size:11.5px}
td.p{font-family:ui-monospace,monospace}
tr:last-child td{border-bottom:0}
/* 授权状态条 */
.auth{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:16px;
  padding:10px 14px;border:1px solid var(--line);border-radius:10px;background:#fff;font-size:13px}
.auth .ok{color:#1f8a4c}
.auth button.link{border:0;background:none;color:var(--accent);cursor:pointer;font:inherit;padding:0}
.auth input{flex:1 1 220px;max-width:320px}
</style>
</head>
<body>
<div class="wrap">
  <h1>后台管理</h1>
  <p class="sub">改完点底部"保存"，内容写进板子 flash 并立即生效，<b>不用重新烧固件</b>。图片填 URL 即可。</p>

  <div class="auth" id="auth-bar"></div>

  <div class="card">
    <h2>开发板状态 <span class="hint" id="dev-updated"></span></h2>
    <div class="devgrid" id="dev-grid">加载中…</div>
  </div>

  <div class="card">
    <h2>侧写</h2>
    <div class="grid">
      <div><label>网名</label><input id="p-name" placeholder="ESP32"></div>
      <div><label>一句话</label><input id="p-tagline" placeholder="ESP32-C3 · 住在客厅的小板子"></div>
      <div><label>头像图片 URL</label><input id="p-avatar" placeholder="https://你的图库/avatar.webp"></div>
      <div><label>背景图片 URL</label><input id="p-bg" placeholder="https://你的图库/bg.webp"></div>
      <div><label>个人标签（空格或逗号分隔）</label><input id="p-tags" placeholder="折腾 ESP32 IPv6 自建服务"></div>
    </div>
  </div>

  <div class="card">
    <h2>站点</h2>
    <div class="grid">
      <div><label>网站名字（浏览器标签页标题）</label><input id="s-title" placeholder="ESP32"></div>
      <div><label>网站图标 URL（favicon）</label><input id="s-favicon" placeholder="https://你的图库/icon.png"></div>
      <div><label>天气城市</label><input id="s-city" placeholder="Beijing / Shanghai / Guangzhou"></div>
    </div>
    <div class="hint">图标建议 32×32 或 180×180 的 png/ico，留空用浏览器默认。
      天气由<b>浏览器</b>直接向 Open-Meteo 取（板子不参与、零负担）。城市请填<b>市级</b>名称——
      填省级/大地名会被解析成同名小地方。</div>
  </div>

  <div class="card">
    <h2>网页列表 <span class="hbtns"><button class="add" data-reverse="urls">反转顺序</button><button class="add" data-add="urls">+ 添加</button></span></h2>
    <div id="urls"></div>
    <div class="hint">数量不限：1 个会居中显示，3 个一行，更多自动换行。</div>
  </div>

  <div class="card">
    <h2>联系方式 <span class="hbtns"><button class="add" data-reverse="contacts">反转顺序</button><button class="add" data-add="contacts">+ 添加</button></span></h2>
    <div id="contacts"></div>
    <div class="hint">图标和图标图片都<b>可以留空</b>，留空就只显示名字。链接留空则不显示这一条。</div>
  </div>

  <div class="card">
    <h2>时间树 <span class="hbtns"><button class="add" data-reverse="timeline">反转顺序</button><button class="add" data-add="timeline">+ 添加</button></span></h2>
    <div id="timeline"></div>
    <div class="hint">从上到下就是网页上的显示顺序（最上面那条会带圆点高亮）。用 ↑ ↓ 调整，或点"反转顺序"整体倒过来。</div>
  </div>

  <div class="card">
    <h2>请求日志 <button class="add" id="req-reload">刷新</button></h2>
    <div class="hint" id="req-summary">加载中…</div>
    <div class="tablewrap">
      <table id="req-table">
        <thead><tr><th>时间</th><th>方法</th><th>路径</th><th>来源</th></tr></thead>
        <tbody></tbody>
      </table>
    </div>
    <div class="hint">
      每 5 秒自动刷新。首页开着时每秒会有一次 <code>/api/status</code>（正常现象）；
      如果<b>没开页面</b>却一直有请求，看"来源"列——多半是扫描器在扫你的公开域名。
    </div>
  </div>
</div>

<div class="bar"><div class="inner">
  <button class="primary" id="save" disabled>保存到板子</button>
  <span id="status"></span>
</div></div>

<script>
(function () {
  "use strict";

  /* 表单结构：新增字段改这里即可，模板与读写逻辑都是通用的 */
  var SCHEMA = {
    urls: [
      { key: "name", label: "名字", width: 140, placeholder: "我的博客" },
      { key: "url",  label: "链接", grow: true, placeholder: "https://…" },
      { key: "icon", label: "图标（可选）", width: 90, placeholder: "🌐 可留空" },
      { key: "img",  label: "图标图片（可选）", grow: true, placeholder: "https://…（可留空）" },
      { key: "desc", label: "说明（可选）", width: 130, placeholder: "可留空" }
    ],
    contacts: [
      { key: "name", label: "名字", width: 130, placeholder: "GitHub" },
      { key: "url",  label: "链接", grow: true, placeholder: "https://…" },
      { key: "icon", label: "图标（可选）", width: 90, placeholder: "🐙 可留空" },
      { key: "img",  label: "图标图片（可选）", grow: true, placeholder: "https://…（可留空）" }
    ],
    timeline: [
      { key: "date",  label: "日期", width: 110, placeholder: "2026.10" },
      { key: "title", label: "标题", width: 190, placeholder: "网站上线" },
      { key: "desc",  label: "说明", grow: true, placeholder: "可留空" }
    ]
  };

  var $ = function (sel) { return document.querySelector(sel); };
  var loaded = false;              // 内容是否已读取完成（没读完不允许保存，避免把内容清空）

  /* ---------- 授权：令牌从链接 (?k=) 或上次保存里取，不需要手输 ---------- */
  var TOKEN_KEY = "wtoken";

  function readTokenFromUrl() {
    var m = location.search.match(/[?&]k=([^&#]+)/);
    if (m) {
      try { localStorage.setItem(TOKEN_KEY, decodeURIComponent(m[1])); } catch (e) {}
    }
  }

  function getToken() {
    try { return localStorage.getItem(TOKEN_KEY) || ""; } catch (e) { return ""; }
  }

  function renderAuthBar() {
    var bar = $("#auth-bar");
    bar.innerHTML = "";
    var token = getToken();

    if (token) {
      var ok = document.createElement("span");
      ok.className = "ok";
      ok.textContent = "🔓 已授权，保存时会自动带上令牌";
      var change = document.createElement("button");
      change.className = "link";
      change.type = "button";
      change.textContent = "换一个令牌";
      change.addEventListener("click", function () {
        try { localStorage.removeItem(TOKEN_KEY); } catch (e) {}
        renderAuthBar();
      });
      bar.appendChild(ok);
      bar.appendChild(change);
      return;
    }

    var hint = document.createElement("span");
    hint.textContent = "🔒 需要写入令牌：直接用带令牌的链接打开本页即可（形如 /admin?k=…），也可以在这里填一次：";
    var input = document.createElement("input");
    input.placeholder = "写入令牌";
    input.addEventListener("change", function () {
      try { localStorage.setItem(TOKEN_KEY, input.value.trim()); } catch (e) {}
      renderAuthBar();
    });
    bar.appendChild(hint);
    bar.appendChild(input);
  }

  /* ---------- 通用行编辑器 ---------- */
  function makeField(spec, value) {
    var box = document.createElement("div");
    box.className = "f" + (spec.grow ? " grow" : "");
    if (spec.width) box.style.flex = "0 0 " + spec.width + "px";
    var label = document.createElement("label");
    label.textContent = spec.label;
    var input = document.createElement("input");
    input.placeholder = spec.placeholder || "";
    input.value = value || "";
    input.dataset.key = spec.key;
    box.appendChild(label);
    box.appendChild(input);
    return box;
  }

  function appendRow(kind, data) {
    var container = document.getElementById(kind);
    var row = document.createElement("div");
    row.className = "row";
    SCHEMA[kind].forEach(function (spec) {
      row.appendChild(makeField(spec, data ? data[spec.key] : ""));
    });

    // 上移 / 下移：列表顺序就是网页上的显示顺序
    var up = document.createElement("button");
    up.className = "move";
    up.type = "button";
    up.title = "上移";
    up.textContent = "↑";
    up.addEventListener("click", function () {
      var prev = row.previousElementSibling;
      if (prev) container.insertBefore(row, prev);
      refreshMoves(kind);
    });

    var down = document.createElement("button");
    down.className = "move";
    down.type = "button";
    down.title = "下移";
    down.textContent = "↓";
    down.addEventListener("click", function () {
      var next = row.nextElementSibling;
      if (next) container.insertBefore(next, row);
      refreshMoves(kind);
    });

    var del = document.createElement("button");
    del.className = "del";
    del.type = "button";
    del.textContent = "删除";
    del.addEventListener("click", function () {
      row.remove();
      refreshMoves(kind);
    });

    row.appendChild(up);
    row.appendChild(down);
    row.appendChild(del);
    container.appendChild(row);
    refreshMoves(kind);
  }

  /* 第一行的 ↑ 和最后一行的 ↓ 置灰 */
  function refreshMoves(kind) {
    var rows = document.querySelectorAll("#" + kind + " .row");
    rows.forEach(function (row, i) {
      var btns = row.querySelectorAll(".move");
      if (btns.length === 2) {
        btns[0].disabled = i === 0;
        btns[1].disabled = i === rows.length - 1;
      }
    });
  }

  /* 一键反转整栏顺序 */
  function reverseRows(kind) {
    var container = document.getElementById(kind);
    var rows = Array.prototype.slice.call(container.children).reverse();
    container.innerHTML = "";
    rows.forEach(function (r) { container.appendChild(r); });
    refreshMoves(kind);
  }

  function readRows(kind) {
    var out = [];
    document.querySelectorAll("#" + kind + " .row").forEach(function (row) {
      var item = {};
      var hasValue = false;
      SCHEMA[kind].forEach(function (spec) {
        var input = row.querySelector('[data-key="' + spec.key + '"]');
        var v = input ? input.value.trim() : "";
        item[spec.key] = v;
        if (v) hasValue = true;
      });
      if (hasValue) out.push(item);              // 全空的行直接丢弃
    });
    return out;
  }

  function render(kind, list) {
    document.getElementById(kind).innerHTML = "";
    (list || []).forEach(function (item) { appendRow(kind, item); });
  }

  document.querySelectorAll("[data-add]").forEach(function (btn) {
    btn.addEventListener("click", function () { appendRow(btn.dataset.add, null); });
  });

  document.querySelectorAll("[data-reverse]").forEach(function (btn) {
    btn.addEventListener("click", function () { reverseRows(btn.dataset.reverse); });
  });

  /* ---------- 读取 / 保存 ---------- */
  function fill(content) {
    var p = content.profile || {};
    $("#p-name").value = p.name || "";
    $("#p-tagline").value = p.tagline || "";
    $("#p-avatar").value = p.avatar || "";
    $("#p-bg").value = p.bg || "";
    $("#p-tags").value = (p.tags || []).join(" ");

    var s = content.site || {};
    $("#s-title").value = s.title || "";
    $("#s-favicon").value = s.favicon || "";
    $("#s-city").value = s.city || "";
    applySite(s, p.name);            // 后台页自己的标签页也跟着变

    render("urls", content.urls);
    render("contacts", content.contacts);
    render("timeline", content.timeline);
    loaded = true;
    $("#save").disabled = false;
  }

  /* 站点名字与图标 */
  function applySite(site, fallbackName) {
    document.title = (site && site.title) || (fallbackName ? fallbackName + " · 后台管理" : "后台管理");
    if (site && site.favicon) {
      var link = document.getElementById("favicon");
      if (link) link.href = site.favicon;
    }
  }

  function load() {
    fetch("/api/content", { cache: "no-store" })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        fill(j);
        setStatus("已读取当前内容", "");
      })
      .catch(function () { setStatus("读取失败", "err"); });
  }

  function setStatus(text, cls) {
    var el = $("#status");
    el.textContent = text;
    el.className = cls || "";
  }

  /* ---------- 开发板状态监控 ---------- */
  function fmtUptime(seconds) {
    var s = Math.floor(seconds);
    var d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600);
    var m = Math.floor(s % 3600 / 60), sec = s % 60;
    if (d) return d + " 天 " + h + " 小时";
    if (h) return h + " 小时 " + m + " 分";
    if (m) return m + " 分 " + sec + " 秒";
    return sec + " 秒";
  }

  function loadDevice() {
    fetch("/api/status", { cache: "no-store" })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        var ddnsText = j.ddns_state === 1 ? "正常" : (j.ddns_state === -1 ? "失败" : "等待");
        var ddnsCls = j.ddns_state === 1 ? "ok2" : (j.ddns_state === -1 ? "err2" : "");
        var rows = [
          ["芯片", j.chip + " · " + j.cpu + " MHz"],
          ["Flash", Math.round(j.flash / 1024) + " MB"],
          ["可用内存", Math.round(j.heap / 1024) + " KB / " + Math.round(j.heap_total / 1024) + " KB"],
          ["芯片温度", Number(j.temp).toFixed(1) + " °C"],
          ["WiFi", (j.ssid || "-") + " · " + j.rssi + " dBm · ch" + j.ch],
          ["局域网 IP", j.ipv4 || "-"],
          ["公网 IPv6", (!j.ipv6 || j.ipv6 === "-") ? "（未获取）" : "[" + j.ipv6 + "]"],
          ["MAC", j.mac || "-"],
          ["在线时长", fmtUptime(j.up_s || 0)],
          ["累计请求", (j.req || 0) + " 次"],
          ["域名 / DDNS", (j.ddns_host || "-") + " · " + ddnsText]
        ];

        var box = $("#dev-grid");
        box.innerHTML = "";
        rows.forEach(function (r) {
          var d = document.createElement("div");
          d.className = "kv2";
          var k = document.createElement("span");
          k.textContent = r[0];
          var v = document.createElement("b");
          v.textContent = r[1];
          if (r[0] === "域名 / DDNS" && ddnsCls) v.className = ddnsCls;
          d.appendChild(k);
          d.appendChild(v);
          box.appendChild(d);
        });
        $("#dev-updated").textContent = "每 3 秒刷新 · " + new Date().toLocaleTimeString();
      })
      .catch(function () { $("#dev-grid").textContent = "状态读取失败（板子离线？）"; });
  }

  /* ---------- 请求日志 ---------- */
  function ageText(seconds) {
    if (seconds < 5) return "刚刚";
    if (seconds < 60) return seconds + " 秒前";
    if (seconds < 3600) return Math.floor(seconds / 60) + " 分钟前";
    return Math.floor(seconds / 3600) + " 小时前";
  }

  function loadRequests() {
    fetch("/api/requests", { cache: "no-store" })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        var rows = j.recent || [];
        var body = $("#req-table tbody");
        body.innerHTML = "";

        rows.forEach(function (e) {
          var tr = document.createElement("tr");
          var age = Math.max(0, (j.now || 0) - (e.t || 0));
          [ageText(age), e.m || "", e.p || "", e.ip || ""].forEach(function (txt, i) {
            var td = document.createElement("td");
            td.textContent = txt;
            if (i === 2) td.className = "p";
            if (i === 3) td.className = "ip";
            tr.appendChild(td);
          });
          body.appendChild(tr);
        });

        // 小结：最近这些请求里各路径、各来源的次数
        var byPath = {}, byIp = {};
        rows.forEach(function (e) {
          byPath[e.p] = (byPath[e.p] || 0) + 1;
          byIp[e.ip] = (byIp[e.ip] || 0) + 1;
        });
        var topPaths = Object.keys(byPath).sort(function (a, b) { return byPath[b] - byPath[a]; })
          .slice(0, 3).map(function (k) { return k + " ×" + byPath[k]; }).join("，");
        var ips = Object.keys(byIp).length;
        $("#req-summary").textContent = "开机以来共 " + (j.total || 0) + " 次请求；最近 " +
          rows.length + " 条里：" + (topPaths || "无") + "；来源 " + ips + " 个";
      })
      .catch(function () { $("#req-summary").textContent = "请求日志读取失败"; });
  }

  $("#save").addEventListener("click", function () {
    if (!loaded) {
      setStatus("内容还没读取完，稍等一下再保存", "err");
      return;
    }
    var token = getToken();

    var payload = {
      profile: {
        name: $("#p-name").value.trim(),
        tagline: $("#p-tagline").value.trim(),
        avatar: $("#p-avatar").value.trim(),
        bg: $("#p-bg").value.trim(),
        // 标签：空格、逗号、顿号都能分隔
        tags: $("#p-tags").value.split(/[\s,，、]+/).filter(function (t) { return t.length > 0; })
      },
      site: {
        title: $("#s-title").value.trim(),
        favicon: $("#s-favicon").value.trim(),
        city: $("#s-city").value.trim()
      },
      urls: readRows("urls"),
      contacts: readRows("contacts"),
      timeline: readRows("timeline")
    };

    // 防止误清空：某一栏空了就确认一次
    var emptied = ["urls", "contacts", "timeline"].filter(function (kind) {
      return payload[kind].length === 0 && document.querySelectorAll("#" + kind + " .row").length > 0;
    });
    if (emptied.length === 0) {
      emptied = ["urls", "contacts", "timeline"].filter(function (kind) {
        return payload[kind].length === 0;
      });
    }
    if (emptied.length && !confirm("这几栏会被清空：" + emptied.join("、") + "。确定保存吗？")) {
      setStatus("已取消", "");
      return;
    }

    setStatus("保存中…", "");
    fetch("/api/content?k=" + encodeURIComponent(token), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    })
      .then(function (r) { return r.json().then(function (j) { return { code: r.status, body: j }; }); })
      .then(function (res) {
        if (res.code === 200 && res.body.ok) {
          setStatus("✅ 已保存（网页 " + res.body.urls + " / 联系 " + res.body.contacts +
                    " / 时间树 " + res.body.timeline + "）", "ok");
        } else {
          setStatus("❌ " + (res.body.msg || ("HTTP " + res.code)), "err");
        }
      })
      .catch(function () { setStatus("❌ 网络错误", "err"); });
  });

  readTokenFromUrl();
  renderAuthBar();
  load();
  loadDevice();
  setInterval(loadDevice, 3000);          // 开发板状态每 3 秒刷新
  loadRequests();
  setInterval(loadRequests, 5000);        // 请求日志每 5 秒自动刷新
  $("#req-reload").addEventListener("click", loadRequests);
})();
</script>
</body>
</html>
)HTML";
