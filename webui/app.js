(function () {
  function post(obj) {
    try {
      if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.pdl) {
        window.webkit.messageHandlers.pdl.postMessage(JSON.stringify(obj));
        return;
      }
    } catch (e) {}
    console.log("pdl bridge", obj);
  }

  const feed1 = document.getElementById("feed-1");
  const feed2 = document.getElementById("feed-2");
  let activePane = 1;

  function typeBadge(type) {
    const t = (type || "").toUpperCase();
    if (t.indexOf("NUM") >= 0) return { cls: "numeric", label: "Numeric" };
    if (t.indexOf("TONE") >= 0) return { cls: "tone", label: "Tone" };
    if (t.indexOf("ALPHA") >= 0 || t.indexOf("ALP") >= 0) return { cls: "alpha", label: "Alpha" };
    if (t) return { cls: "other", label: type };
    return { cls: "other", label: "Msg" };
  }

  function renderCards(el, rows) {
    el.innerHTML = "";
    if (!rows || !rows.length) {
      const empty = document.createElement("div");
      empty.className = "empty";
      empty.textContent = "No messages yet — connect audio or PagerCast.";
      el.appendChild(empty);
      return;
    }
    const frag = document.createDocumentFragment();
    rows.forEach(function (m) {
      if (typeof m === "string") {
        m = { message: m, addr: "", time: "", date: "", mode: "", type: "", bitrate: "" };
      }
      const badge = typeBadge(m.type);
      const card = document.createElement("article");
      card.className = "msg";
      card.innerHTML =
        '<div class="msg-main">' +
          '<div class="msg-top">' +
            '<span class="msg-time"></span>' +
            '<span class="msg-addr"></span>' +
          '</div>' +
          '<div class="msg-meta"></div>' +
          '<div class="msg-body"></div>' +
        '</div>' +
        '<div class="msg-side">' +
          '<span class="badge"></span>' +
          '<span class="star">☆</span>' +
        '</div>';
      const time = [m.time, m.date].filter(Boolean).join(" · ");
      card.querySelector(".msg-time").textContent = time || "—";
      card.querySelector(".msg-addr").textContent = m.addr || "—————";
      const meta = [];
      if (m.addr) meta.push('CAP CODE <b>' + escapeHtml(m.addr) + '</b>');
      let phone = m.phone || "";
      let body = m.message || "";
      if (!phone) {
        const ph = /^(\+\d{8,15})\s+(.*)$/.exec(body);
        if (ph) {
          phone = ph[1];
          body = ph[2];
        }
      }
      if (phone) meta.push('PHONE <b>' + escapeHtml(phone) + '</b>');
      if (m.mode) meta.push('MODE <b>' + escapeHtml(m.mode) + '</b>');
      if (m.bitrate) meta.push('RATE <b>' + escapeHtml(m.bitrate) + '</b>');
      card.querySelector(".msg-meta").innerHTML = meta.join('<span style="opacity:.35"> · </span>');
      card.querySelector(".msg-body").textContent = body;
      const b = card.querySelector(".badge");
      b.className = "badge " + badge.cls;
      b.textContent = badge.label;
      frag.appendChild(card);
    });
    el.appendChild(frag);
  }

  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  window.pdlSetPanes = function (p1, p2) {
    renderCards(feed1, p1);
    renderCards(feed2, p2);
    const n = (p1 ? p1.length : 0) + (p2 ? p2.length : 0);
    document.getElementById("footer-count").textContent = n + " messages";
  };

  window.pdlSetStatus = function (s) {
    s = s || {};
    const sig = Math.max(0, Math.min(100, s.signal || 0));
    const q = Math.max(0, Math.min(100, s.quality || 0));
    document.getElementById("signal-fill").style.width = sig.toFixed(0) + "%";
    document.getElementById("signal-val").textContent = sig.toFixed(0) + "%";
    document.getElementById("signal-hint").textContent =
      sig > 20 ? "Signal looks good" : (sig > 0 ? "Weak signal" : "Waiting for audio");
    document.getElementById("quality-fill").style.width = q.toFixed(0) + "%";
    document.getElementById("quality-val").textContent = q.toFixed(1) + "%";
    document.getElementById("bch-val").textContent =
      "BCH " + (s.bch != null ? s.bch : "—") +
      (s.cw != null ? " (" + s.cw + " cw)" : "");
    document.getElementById("nav-input").textContent = s.input || "—";
    document.getElementById("nav-mode").textContent = s.mode || "Decoder";
    document.getElementById("footer-status").innerHTML =
      '<span class="pulse"></span> ' + (s.pcStatus || s.pcLabel || "Ready");
    document.getElementById("pc-pill-label").textContent = s.pcLabel || "Offline";
    const dot = document.getElementById("pc-dot");
    dot.className = "dot";
    if (s.pcState === 2) dot.classList.add("ok");
    else if (s.pcState === 1) dot.classList.add("warn");
    const tabs = document.getElementById("pc-src-tabs");
    if (tabs) {
      const show = !!s.pcEnable;
      tabs.hidden = !show;
      if (show) {
        const want = !!s.pcWant;
        document.querySelectorAll(".stab").forEach(function (t) {
          const on = (t.getAttribute("data-src") === "pagercast") === want;
          t.classList.toggle("active", on);
          t.setAttribute("aria-selected", on ? "true" : "false");
        });
      }
    }
  };

  window.pdlSetSettings = function (s) {
    s = s || {};
    document.getElementById("ui-mode").value = s.uiMode === "web" ? "web" : "gtk";
    document.getElementById("pc-enable").checked = !!s.pcEnable;
    document.getElementById("api-base").value = s.apiBase || "";
    document.getElementById("frequency").value = s.frequency || "";
    document.getElementById("api-key").value = s.apiKey || "";
    document.getElementById("host-suffix").value = s.hostSuffix || "";
    document.getElementById("capture").value = s.capture || "default";
    const sel = document.getElementById("nodes");
    sel.innerHTML = "";
    (s.nodes || []).forEach(function (n) {
      const opt = document.createElement("option");
      opt.value = n.url;
      opt.textContent = n.name;
      if (n.url === s.apiBase) opt.selected = true;
      sel.appendChild(opt);
    });
  };

  /* Rail: Messages vs Settings focus on narrow screens; both visible on desktop */
  document.querySelectorAll(".rail-item[data-panel]").forEach(function (btn) {
    btn.addEventListener("click", function () {
      document.querySelectorAll(".rail-item[data-panel]").forEach(function (b) {
        b.classList.toggle("active", b === btn);
      });
      const panel = btn.getAttribute("data-panel");
      document.querySelector(".shell").classList.toggle("show-settings", panel === "settings");
      if (panel === "settings") post({ cmd: "get_settings" });
    });
  });

  document.querySelectorAll(".ftab").forEach(function (tab) {
    tab.addEventListener("click", function () {
      document.querySelectorAll(".ftab").forEach(function (t) {
        t.classList.remove("active");
        t.setAttribute("aria-selected", "false");
      });
      tab.classList.add("active");
      tab.setAttribute("aria-selected", "true");
      activePane = parseInt(tab.getAttribute("data-pane"), 10) || 1;
      feed1.classList.toggle("hidden", activePane !== 1);
      feed2.classList.toggle("hidden", activePane !== 2);
    });
  });

  document.querySelectorAll(".stab").forEach(function (tab) {
    tab.addEventListener("click", function () {
      const src = tab.getAttribute("data-src");
      post({ cmd: "pagercast", action: src === "pagercast" ? "connect" : "disconnect" });
    });
  });

  document.querySelectorAll(".snav").forEach(function (btn) {
    btn.addEventListener("click", function () {
      document.querySelectorAll(".snav").forEach(function (b) {
        b.classList.remove("active");
        b.setAttribute("aria-selected", "false");
      });
      btn.classList.add("active");
      btn.setAttribute("aria-selected", "true");
      const sec = btn.getAttribute("data-sec");
      document.querySelectorAll(".sec").forEach(function (s) {
        s.classList.toggle("active", s.id === "sec-" + sec);
      });
    });
  });

  function handleCmdEl(el) {
    const cmd = el.getAttribute("data-cmd");
    if (!cmd) return;
    if (cmd === "dialog") post({ cmd: "dialog", name: el.getAttribute("data-name") || "options" });
    else if (cmd === "clear") post({ cmd: "clear", pane: parseInt(el.getAttribute("data-pane") || "0", 10) });
    else if (cmd === "pagercast") post({ cmd: "pagercast", action: el.getAttribute("data-action") || "toggle" });
  }
  document.querySelectorAll("[data-cmd]").forEach(function (el) {
    el.addEventListener("click", function (e) {
      e.preventDefault();
      handleCmdEl(el);
    });
  });

  function collectSave() {
    post({
      cmd: "save_settings",
      pcEnable: document.getElementById("pc-enable").checked ? 1 : 0,
      apiBase: document.getElementById("api-base").value,
      frequency: document.getElementById("frequency").value,
      apiKey: document.getElementById("api-key").value,
      hostSuffix: document.getElementById("host-suffix").value,
      capture: document.getElementById("capture").value
    });
  }
  document.getElementById("btn-save").addEventListener("click", collectSave);
  document.getElementById("btn-pc-connect").addEventListener("click", function () {
    collectSave();
    post({ cmd: "pagercast", action: "connect" });
  });
  document.getElementById("btn-pc-disconnect").addEventListener("click", function () {
    post({ cmd: "pagercast", action: "disconnect" });
  });
  document.getElementById("btn-pc-refresh").addEventListener("click", function () {
    post({ cmd: "pagercast", action: "refresh" });
  });
  document.getElementById("nodes").addEventListener("change", function () {
    document.getElementById("api-base").value = this.value;
  });
  document.getElementById("btn-apply-ui").addEventListener("click", function () {
    post({ cmd: "set_ui_mode", mode: document.getElementById("ui-mode").value, restart: 1 });
  });
  document.getElementById("btn-more").addEventListener("click", function () {
    post({ cmd: "dialog", name: "about" });
  });

  post({ cmd: "ready" });
})();
