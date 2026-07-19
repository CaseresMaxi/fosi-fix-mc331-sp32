(() => {
  const $ = (id) => document.getElementById(id);

  const els = {
    version: $("version"),
    usbPill: $("usb-pill"),
    usbTitle: $("usb-title"),
    usbDetail: $("usb-detail"),
    uptime: $("uptime"),
    lastFix: $("last-fix"),
    fixCount: $("fix-count"),
    connectCount: $("connect-count"),
    ram: $("ram"),
    psram: $("psram"),
    flash: $("flash"),
    wifiState: $("wifi-state"),
    wifiSsid: $("wifi-ssid"),
    wifiIp: $("wifi-ip"),
    wifiHost: $("wifi-host"),
    ssid: $("ssid"),
    password: $("password"),
    hostname: $("hostname"),
    retry: $("retry"),
    periodicFix: $("periodic-fix"),
    autoMode: $("autoMode"),
    logView: $("log-view"),
    wsState: $("ws-state"),
    otaProgress: $("ota-progress"),
    otaMsg: $("ota-msg"),
    setupBanner: $("setup-banner"),
    setupUrl: $("setup-url"),
    wifiList: $("wifi-list"),
    saveMsg: $("save-msg"),
    wifiHint: $("wifi-hint"),
  };

  let bootSkew = 0;
  let ws;
  let setupMode = false;

  const fmtBytes = (n) => {
    if (n == null) return "—";
    if (n >= 1048576) return `${(n / 1048576).toFixed(2)} MB`;
    if (n >= 1024) return `${(n / 1024).toFixed(1)} KB`;
    return `${n} B`;
  };

  const fmtUptime = (ms) => {
    const s = Math.floor(ms / 1000);
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    const sec = s % 60;
    if (h > 0) return `${h}h ${m}m ${sec}s`;
    if (m > 0) return `${m}m ${sec}s`;
    return `${sec}s`;
  };

  const fmtAgo = (deviceMs) => {
    if (!deviceMs) return "Nunca";
    const age = Math.max(0, Date.now() - (bootSkew + deviceMs));
    if (age < 5000) return "Ahora";
    return `Hace ${fmtUptime(age)}`;
  };

  const setUsbUi = (usb) => {
    const connected = !!usb.connected;
    els.usbPill.className = "status-pill " + (connected ? "ok" : "warn");
    els.usbPill.textContent = connected ? "MC331 conectado" : "MC331 desconectado";
    els.usbTitle.textContent = connected ? "MC331 listo" : "Esperando MC331";
    els.usbDetail.textContent = connected
      ? `VID 0x${Number(usb.vid).toString(16)} · PID 0x${Number(usb.pid).toString(16)} · ${usb.state}`
      : `Estado: ${usb.state || "waiting"}`;
    els.lastFix.textContent = fmtAgo(usb.lastFixMs);
    els.fixCount.textContent = usb.fixCount ?? 0;
    els.connectCount.textContent = usb.connectCount ?? 0;
  };

  const renderStatus = (data) => {
    if (!data) return;
    bootSkew = Date.now() - (data.uptimeMs || 0);
    els.version.textContent = `v${data.version || "?"}`;
    els.uptime.textContent = fmtUptime(data.uptimeMs || 0);
    els.ram.textContent = `${fmtBytes(data.heapFree)} / ${fmtBytes(data.heapSize)}`;
    els.psram.textContent = `${fmtBytes(data.psramFree)} / ${fmtBytes(data.psramSize)}`;
    els.flash.textContent = fmtBytes(data.flashSize);

    if (data.wifi) {
      setupMode = !!data.wifi.setupMode || data.wifi.mode === "ap";
      els.setupBanner.hidden = !setupMode;
      const host = data.wifi.hostname || "fosifix";
      els.setupUrl.textContent = `http://${host}.local`;
      els.wifiState.textContent = setupMode
        ? "Modo configuración (AP)"
        : (data.wifi.connected ? "Conectado a tu red" : "Desconectado");
      els.wifiSsid.textContent = data.wifi.ssid || "—";
      els.wifiIp.textContent = data.wifi.ip || "—";
      els.wifiHost.textContent = `${host}.local`;
      els.wifiHint.innerHTML = setupMode
        ? `Después de guardar, volvé a tu WiFi de casa y abrí <strong>http://${host}.local</strong>.`
        : `Estás en tu red. Controlá todo desde <strong>http://${host}.local</strong> o <strong>http://${data.wifi.ip}</strong>.`;
    }

    if (data.usb) setUsbUi(data.usb);

    if (data.settings) {
      if (!els.hostname.value) els.hostname.value = data.settings.hostname || "fosifix";
      if (!els.retry.value) els.retry.value = data.settings.retryIntervalMs || 3000;
      if (data.settings.periodicFixMs != null) {
        els.periodicFix.value = Math.round((data.settings.periodicFixMs || 30000) / 1000);
      }
      els.autoMode.checked = data.settings.autoMode !== false;
    }
  };

  const appendLog = (entry) => {
    const line = document.createElement("div");
    const level = (entry.level || "INFO").toLowerCase();
    line.className = `log-line ${level}`;
    const t = entry.t != null ? `[${entry.t}]` : "";
    line.innerHTML = `<span class="lvl">${t} ${entry.level || "INFO"}</span>${entry.msg || ""}`;
    els.logView.appendChild(line);
    els.logView.scrollTop = els.logView.scrollHeight;
    while (els.logView.children.length > 300) {
      els.logView.removeChild(els.logView.firstChild);
    }
  };

  const loadSettings = async () => {
    const res = await fetch("/api/settings");
    const data = await res.json();
    els.ssid.value = data.ssid || "";
    els.hostname.value = data.hostname || "fosifix";
    els.retry.value = data.retryIntervalMs || 3000;
    els.periodicFix.value = Math.round((data.periodicFixMs || 30000) / 1000);
    els.autoMode.checked = data.autoMode !== false;
    if (data.setupMode) {
      els.setupBanner.hidden = false;
    }
  };

  const loadLogs = async () => {
    const res = await fetch("/api/logs");
    const logs = await res.json();
    els.logView.innerHTML = "";
    logs.forEach(appendLog);
  };

  const scanWifi = async () => {
    const btn = $("btn-scan");
    btn.disabled = true;
    btn.textContent = "Buscando…";
    els.wifiList.innerHTML = "<li class='muted'>Escaneando redes…</li>";
    try {
      const res = await fetch("/api/wifi/scan");
      const list = await res.json();
      els.wifiList.innerHTML = "";
      list
        .filter((n) => n.ssid)
        .sort((a, b) => b.rssi - a.rssi)
        .forEach((n) => {
          const li = document.createElement("li");
          li.innerHTML = `<span class="ssid">${n.ssid}</span><span class="meta">${n.rssi} dBm${n.secure ? " · 🔒" : ""}</span>`;
          li.addEventListener("click", () => {
            els.ssid.value = n.ssid;
            [...els.wifiList.children].forEach((c) => c.classList.remove("active"));
            li.classList.add("active");
          });
          els.wifiList.appendChild(li);
        });
      if (!els.wifiList.children.length) {
        els.wifiList.innerHTML = "<li class='muted'>No se encontraron redes</li>";
      }
    } catch (_) {
      els.wifiList.innerHTML = "<li class='muted'>Error al escanear</li>";
    } finally {
      btn.disabled = false;
      btn.textContent = "Buscar redes";
    }
  };

  const connectWs = () => {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    ws = new WebSocket(`${proto}://${location.hostname}:81/`);
    ws.onopen = () => { els.wsState.textContent = "WS conectado"; };
    ws.onclose = () => {
      els.wsState.textContent = "WS reconectando…";
      setTimeout(connectWs, 2000);
    };
    ws.onerror = () => { els.wsState.textContent = "WS error"; };
    ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(ev.data);
        if (msg.type === "status") renderStatus(msg.data);
        if (msg.type === "log") appendLog(msg.data);
      } catch (_) {}
    };
  };

  $("btn-fix").addEventListener("click", async () => {
    $("btn-fix").disabled = true;
    try {
      await fetch("/api/fix", { method: "POST" });
    } finally {
      $("btn-fix").disabled = false;
    }
  });

  $("btn-reboot").addEventListener("click", async () => {
    if (!confirm("¿Reiniciar FosiFix?")) return;
    await fetch("/api/reboot", { method: "POST" });
  });

  $("btn-clear-logs").addEventListener("click", () => {
    els.logView.innerHTML = "";
  });

  $("btn-scan").addEventListener("click", () => { scanWifi(); });

  $("settings-form").addEventListener("submit", async (e) => {
    e.preventDefault();
    const host = els.hostname.value || "fosifix";
    const body = {
      ssid: els.ssid.value.trim(),
      password: els.password.value,
      hostname: host,
      retryIntervalMs: Number(els.retry.value) || 3000,
      periodicFixMs: Math.max(5, Number(els.periodicFix.value) || 30) * 1000,
      autoMode: !!els.autoMode.checked,
      reboot: true,
    };
    if (!body.ssid) {
      alert("Elegí o escribí un SSID");
      return;
    }
    $("btn-save-wifi").disabled = true;
    try {
      const res = await fetch("/api/settings", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      const data = await res.json();
      els.saveMsg.hidden = false;
      if (data.ok) {
        els.saveMsg.textContent =
          `Guardado. Reconectate a tu WiFi de casa y abrí http://${host}.local`;
        els.saveMsg.className = "hint success";
      } else {
        els.saveMsg.textContent = "No se pudo guardar";
        els.saveMsg.className = "hint";
      }
    } finally {
      $("btn-save-wifi").disabled = false;
    }
  });

  $("ota-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const file = $("firmware").files[0];
    if (!file) return;

    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/ota");
    xhr.upload.onprogress = (ev) => {
      if (!ev.lengthComputable) return;
      const pct = Math.round((ev.loaded / ev.total) * 100);
      els.otaProgress.value = pct;
      els.otaMsg.textContent = `Subiendo ${pct}%`;
    };
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        els.otaMsg.textContent = "OTA OK. Reiniciando…";
      } else {
        els.otaMsg.textContent = "OTA falló";
      }
    };
    xhr.onerror = () => { els.otaMsg.textContent = "Error de red"; };
    const fd = new FormData();
    fd.append("firmware", file);
    xhr.send(fd);
  });

  const pollFallback = async () => {
    try {
      const res = await fetch("/api/status");
      renderStatus(await res.json());
    } catch (_) {}
  };

  loadSettings().catch(() => {});
  loadLogs().catch(() => {});
  connectWs();
  pollFallback();
  setInterval(pollFallback, 5000);
  scanWifi().catch(() => {});
})();
