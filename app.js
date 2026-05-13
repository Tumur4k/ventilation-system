// ── STATE ──
let warehouses = [
    { id:1, name:'Warehouse 1', active:false, temp:0, humidity:0, relay:'GPIO 4',  espNode:'esp1', autoMode:false, autoPhase:'ON', autoRemain:'' },
    { id:2, name:'Warehouse 2', active:false, temp:0, humidity:0, relay:'GPIO 5',  espNode:'esp1', autoMode:false, autoPhase:'ON', autoRemain:'' },
    { id:3, name:'Warehouse 3', active:false, temp:0, humidity:0, relay:'GPIO 18', espNode:'esp2', autoMode:false, autoPhase:'ON', autoRemain:'' },
    { id:4, name:'Warehouse 4', active:false, temp:0, humidity:0, relay:'GPIO 19', espNode:'esp2', autoMode:false, autoPhase:'ON', autoRemain:'' },
    { id:5, name:'Warehouse 5', active:false, temp:0, humidity:0, relay:'GPIO 21', espNode:'esp3', autoMode:false, autoPhase:'ON', autoRemain:'' },
];

// ── ESP STATUS ──
let espStatus = { esp1: false, esp2: false, esp3: false };

const uptimeStart = Date.now();
const users = [
    { u: "admin",    p: "123" },
    { u: "engineer", p: "123" },
    { u: "operator", p: "123" }
];

// ── MQTT ──
const client = new Paho.MQTT.Client("broker.hivemq.com", Number(8000), "clientId-" + Math.random());

client.onConnectionLost = function(responseObject) {
    addLog("MQTT CONNECTION LOST — Reconnecting...", "sys");
    setTimeout(() => client.connect({ onSuccess: onMQTTConnect, useSSL: false }), 3000);
};

client.onMessageArrived = function(message) {
    const topic   = message.destinationName;
    const payload = message.payloadString;
    const parts   = topic.split('/');

    // 1. ESP STATUS
    if (parts[0] === "warehouse" && parts[1] === "system" &&
        parts[2] && parts[2].startsWith("esp") && parts[2].endsWith("_status")) {

        const espKey   = parts[2];
        const nodeNum  = espKey.replace("_status", "");
        const nodeId   = nodeNum.replace("esp", "node");
        const card     = document.getElementById(nodeId);
        const isOnline = payload === "ONLINE";

        espStatus[nodeNum] = isOnline;

        if (card) {
            const dot  = card.querySelector('.status-dot');
            const text = card.querySelector('.status-text');
            if (isOnline) {
                card.classList.add('online');
                dot.style.backgroundColor = "#00ff88";
                dot.style.boxShadow       = "0 0 12px rgba(0,255,136,0.4)";
                text.innerText            = "ONLINE";
                text.style.color          = "#00ff88";
            } else {
                card.classList.remove('online');
                dot.style.backgroundColor = "#ff2d55";
                dot.style.boxShadow       = "none";
                text.innerText            = "OFFLINE";
                text.style.color          = "#ff2d55";

                warehouses.forEach(w => {
                    if (w.espNode === nodeNum) {
                        w.active   = false;
                        w.autoMode = false;
                    }
                });
            }
        }
        addLog(`${nodeNum.toUpperCase()} → ${payload}`, isOnline ? "on" : "off");
        renderCards();
        return;
    }

    // 2. WAREHOUSE DATA
    if (parts.length >= 3) {
        const id   = parseInt(parts[1]);
        const type = parts[2];
        const w    = warehouses.find(x => x.id === id);
        if (!w) return;

        // ── ДҮГНЭЛТ: өгөгдөл ирж байвал тухайн ESP заавал ONLINE ──
        // esp_status retained message ирээгүй байсан ч зөв ажиллана
        if (!espStatus[w.espNode]) {
            markESPOnline(w.espNode);
        }

        if (type === "temp")     w.temp     = parseFloat(payload);
        if (type === "humidity") w.humidity = parseFloat(payload);
        if (type === "status")   w.active   = (payload === "ON");

        // ── AUTO STATUS: "AUTO|ON|29m45s" эсвэл "MANUAL" ──
        if (type === "auto_status") {
            if (payload === "MANUAL") {
                w.autoMode   = false;
                w.autoPhase  = 'ON';
                w.autoRemain = '';
            } else {
                const p = payload.split('|');
                w.autoMode   = true;
                w.autoPhase  = p[1] || 'ON';
                w.autoRemain = p[2] || '';
            }
        }

        renderCards();
    }
};

// ── ESP NODE CARD-ЫГ ONLINE БОЛГОХ ──
function markESPOnline(nodeNum) {
    if (espStatus[nodeNum]) return;
    espStatus[nodeNum] = true;
    const nodeId = nodeNum.replace("esp", "node");
    const card   = document.getElementById(nodeId);
    if (card) {
        card.classList.add('online');
        const dot  = card.querySelector('.status-dot');
        const text = card.querySelector('.status-text');
        if (dot)  { dot.style.backgroundColor = "#00ff88"; dot.style.boxShadow = "0 0 12px rgba(0,255,136,0.4)"; }
        if (text) { text.innerText = "ONLINE"; text.style.color = "#00ff88"; }
    }
    addLog(`${nodeNum.toUpperCase()} → ONLINE`, "on");
    renderCards();
}

function onMQTTConnect() {
    addLog("MQTT Connected to Cluster", "sys");
    client.subscribe("warehouse/#");
    // Нэн даруй + 3 секундын дараа дахин status асуух (retained message дутуу ирэх тохиолдолд)
    setTimeout(() => sendStatusRequest(), 500);
    setTimeout(() => sendStatusRequest(), 3000);
}

function sendStatusRequest() {
    try {
        const msg = new Paho.MQTT.Message("GET_STATUS");
        msg.destinationName = "warehouse/system/request_update";
        client.send(msg);
    } catch(e) {}
}

client.connect({ onSuccess: onMQTTConnect, useSSL: false });

// ── LOGIN ──
function checkLogin() {
    const uInput    = document.getElementById('username').value;
    const pInput    = document.getElementById('password').value;
    const validUser = users.find(u => u.u === uInput && u.p === pInput);
    if (validUser) {
        localStorage.setItem('isLoggedIn', 'true');
        localStorage.setItem('currentUser', validUser.u);
        showDashboard(validUser.u);
    } else {
        document.getElementById('loginError').style.display = 'block';
    }
}

function showDashboard(username) {
    document.getElementById('loginOverlay').style.display = 'none';
    document.getElementById('mainWrapper').style.display  = 'block';
    addLog(`AUTHORIZED ACCESS: ${username.toUpperCase()}`, 'sys');
    renderCards();
}

// ── MANUAL CONTROL ──
function toggle(id) {
    const w = warehouses.find(x => x.id === id);
    if (!espStatus[w.espNode]) {
        addLog(`CMD BLOCKED: ${w.espNode.toUpperCase()} OFFLINE`, "off");
        return;
    }
    // Гараар хянах үед авто горим унтарна (ESP дээрх callback хийнэ)
    const command = w.active ? "OFF" : "ON";
    const msg = new Paho.MQTT.Message(command);
    msg.destinationName = `warehouse/${id}/control`;
    client.send(msg);
    addLog(`MANUAL CMD: WH${id} → ${command}`, "sys");
}

function setAll(s) {
    const command = s ? "ON" : "OFF";
    const msg = new Paho.MQTT.Message(command);
    msg.destinationName = "warehouse/all/control";
    client.send(msg);
    addLog(`MASTER ${command} BROADCAST`, s ? 'on' : 'off');
}

// ── AUTO CYCLE CONTROL ──
function toggleAuto(id) {
    const w = warehouses.find(x => x.id === id);
    if (!espStatus[w.espNode]) {
        addLog(`AUTO BLOCKED: ${w.espNode.toUpperCase()} OFFLINE`, "off");
        return;
    }
    const command = w.autoMode ? "OFF" : "ON";
    const msg = new Paho.MQTT.Message(command);
    msg.destinationName = `warehouse/${id}/auto`;
    client.send(msg);
    addLog(`AUTO ${command}: WH${id} ${command === 'ON' ? '(30min ON / 5min OFF)' : '→ MANUAL'}`, command === 'ON' ? 'warn' : 'sys');
}

function setAllAuto(s) {
    const command = s ? "ON" : "OFF";
    const msg = new Paho.MQTT.Message(command);
    msg.destinationName = "warehouse/all/auto";
    client.send(msg);
    addLog(`MASTER AUTO ${command} — ALL WAREHOUSES`, s ? 'warn' : 'sys');
}

// ── RENDER ──
function renderCards() {
    const grid = document.getElementById('whGrid');
    if (!grid) return;

    grid.innerHTML = warehouses.map(w => {
        const isESPOnline = espStatus[w.espNode];
        const isDisabled  = !isESPOnline;
        const autoClass   = w.autoMode ? (w.autoPhase === 'ON' ? 'auto-running' : 'auto-rest') : '';

        // Auto badge text
        let autoStripHtml = '';
        if (w.autoMode && !isDisabled) {
            const icon  = w.autoPhase === 'ON' ? '▶' : '⏸';
            const label = w.autoPhase === 'ON' ? `RUNNING — ${w.autoRemain} үлдсэн` : `REST — ${w.autoRemain} үлдсэн`;
            autoStripHtml = `<div class="auto-strip ${autoClass}">
                <span class="auto-strip-icon">${icon}</span>
                AUTO CYCLE &nbsp;${label}
            </div>`;
        } else {
            autoStripHtml = `<div class="auto-strip"></div>`;
        }

        return `
        <div class="wh-card ${w.active ? 'active' : 'inactive'} ${isDisabled ? 'esp-offline' : ''} ${w.autoMode ? 'auto-mode' : ''}">
            <div class="wh-top-bar"></div>
            <div class="wh-header">
                <div>
                    <div class="wh-id">${w.relay}</div>
                    <div class="wh-name">${w.name}</div>
                </div>
                <div class="relay-badge ${w.active ? 'on' : 'off'}">
                    ${isDisabled ? 'NO SIGNAL' : (w.active ? 'ACTIVE' : 'IDLE')}
                </div>
            </div>
            <div class="wh-body">
                <div class="fan-wrap">${fanSVG(w.active && isESPOnline)}</div>
                <div class="wh-stats">
                    <div class="stat-row">
                        <span class="stat-lbl">TEMPERATURE</span>
                        <span class="stat-val ${w.temp >= 30 ? 'hot' : 'ok'}">
                            ${isDisabled ? '--' : w.temp + '°C'}
                        </span>
                    </div>
                    <div class="stat-row">
                        <span class="stat-lbl">HUMIDITY</span>
                        <span class="stat-val">${isDisabled ? '--' : w.humidity + '%'}</span>
                    </div>
                    <div class="stat-row">
                        <span class="stat-lbl">MODE</span>
                        <span class="stat-val ${w.autoMode ? 'mode-auto' : 'mode-manual'}">
                            ${isDisabled ? '--' : (w.autoMode ? 'AUTO' : 'MANUAL')}
                        </span>
                    </div>
                </div>
            </div>
            ${autoStripHtml}
            <div class="wh-footer">
                <button
                    class="toggle-btn ${isDisabled ? 'btn-disabled' : (w.active ? 'turn-off' : 'turn-on')}"
                    onclick="toggle(${w.id})"
                    ${isDisabled ? 'disabled' : ''}>
                    ${isDisabled ? 'OFFLINE' : (w.active ? 'SHUTDOWN' : 'ACTIVATE')}
                </button>
                <button
                    class="auto-btn ${w.autoMode ? 'auto-active' : ''}"
                    onclick="toggleAuto(${w.id})"
                    ${isDisabled ? 'disabled' : ''}>
                    ${w.autoMode ? 'AUTO OFF' : 'AUTO'}
                </button>
            </div>
        </div>`;
    }).join('');

    updateMetrics();
}

function updateMetrics() {
    const active     = warehouses.filter(w => w.active).length;
    const autoActive = warehouses.filter(w => w.autoMode).length;
    const temps      = warehouses.map(w => w.temp).filter(t => t > 0);
    const avg        = temps.length ? Math.round(temps.reduce((a, b) => a + b, 0) / temps.length) : 0;

    if (document.getElementById('activeCount'))
        document.getElementById('activeCount').innerHTML = `${active}<span class="metric-unit">/ 5</span>`;
    if (document.getElementById('avgTemp'))
        document.getElementById('avgTemp').innerHTML = `${avg}<span class="metric-unit">°C</span>`;
    if (document.getElementById('powerDraw'))
        document.getElementById('powerDraw').innerHTML = `${active * 48}<span class="metric-unit">W</span>`;
    if (document.getElementById('autoCount'))
        document.getElementById('autoCount').innerHTML = `${autoActive}<span class="metric-unit">/ 5</span>`;
}

// ── HELPERS ──
function fanSVG(active) {
    const color = active ? 'var(--green)' : 'var(--dim)';
    return `<svg class="fan-svg" viewBox="0 0 64 64" style="width:50px;height:50px;">
      <g class="fan-blades" style="transform-origin:32px 32px;animation:spin ${active ? '0.8s' : '0s'} linear infinite">
        <circle cx="32" cy="32" r="4" fill="${color}"/>
        <path d="M32 10C35 10 40 15 40 20C40 25 35 30 32 30C29 30 24 25 24 20C24 15 29 10 32 10Z" fill="${color}"/>
        <path d="M32 10C35 10 40 15 40 20C40 25 35 30 32 30C29 30 24 25 24 20C24 15 29 10 32 10Z" fill="${color}" transform="rotate(120 32 32)"/>
        <path d="M32 10C35 10 40 15 40 20C40 25 35 30 32 30C29 30 24 25 24 20C24 15 29 10 32 10Z" fill="${color}" transform="rotate(240 32 32)"/>
      </g>
    </svg>`;
}

function addLog(msg, type = 'sys') {
    const box = document.getElementById('logBox');
    if (!box) return;
    const t  = new Date().toTimeString().slice(0, 8);
    const el = document.createElement('div');
    el.className = 'log-entry';
    el.innerHTML = `<span class="log-time">${t}</span><span class="log-msg ${type}">> ${msg}</span>`;
    box.prepend(el);
}

function updateUptime() {
    const elapsed = Math.floor((Date.now() - uptimeStart) / 1000);
    const h = Math.floor(elapsed / 3600);
    const m = Math.floor((elapsed % 3600) / 60);
    const el = document.getElementById("uptime");
    if (el) el.innerHTML = `${String(h).padStart(2,"0")}<span class="metric-unit">h</span> ${String(m).padStart(2,"0")}<span class="metric-unit">m</span>`;
}

function updateClock() {
    const el = document.getElementById('clock');
    if (el) el.textContent = new Date().toTimeString().slice(0, 8);
}

setInterval(updateUptime, 1000);
setInterval(updateClock,  1000);
updateUptime();
updateClock();

window.onload = function() {
    if (localStorage.getItem('isLoggedIn') === 'true') {
        showDashboard(localStorage.getItem('currentUser'));
    }
};
