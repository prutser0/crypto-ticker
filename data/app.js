const MAX_TICKERS = 15;
let config = null;
let statusInterval = null;
let tickerInterval = null;

const TYPE_MAP = ['Crypto', 'Stock', 'Forex'];

document.addEventListener('DOMContentLoaded', () => {
    loadConfig();
    loadStatus();
    startAutoRefresh();
});

function startAutoRefresh() {
    statusInterval = setInterval(loadStatus, 5000);
    tickerInterval = setInterval(loadTickers, 10000);
}

async function loadConfig() {
    try {
        const res = await fetch('/api/config');
        config = await res.json();

        document.getElementById('brightness').value = config.brightness || 128;
        document.getElementById('brightnessVal').textContent = config.brightness || 128;
        document.getElementById('baseTime').value = (config.baseTimeMs || 8000) / 1000;
        document.getElementById('baseTimeVal').textContent = (config.baseTimeMs || 8000) / 1000;
        document.getElementById('coinGeckoKey').value = config.coinGeckoApiKey || '';
        document.getElementById('twelveDataKey').value = config.twelveDataApiKey || '';

        renderTickers();
        loadTickers();
    } catch (e) {
        showMessage('Failed to load config: ' + e.message, 'error');
    }
}

async function loadStatus() {
    try {
        const res = await fetch('/api/status');
        const status = await res.json();

        document.getElementById('version').textContent = 'v' + (status.version || '0.0.0');
        document.getElementById('ssid').textContent = status.ssid || '--';
        document.getElementById('ip').textContent = status.ip || '--';
        document.getElementById('rssi').textContent = status.rssi ? status.rssi + ' dBm' : '--';
        document.getElementById('heap').textContent = status.heap ? formatBytes(status.heap) : '--';
        document.getElementById('uptime').textContent = status.uptime || '--';
    } catch (e) {
        console.error('Status update failed:', e);
    }
}

async function loadTickers() {
    try {
        const res = await fetch('/api/tickers');
        const tickers = await res.json();

        tickers.forEach((ticker, i) => {
            const priceEl = document.getElementById('price-' + i);
            if (priceEl) {
                if (ticker.valid && ticker.price !== undefined) {
                    priceEl.textContent = '$' + ticker.price.toLocaleString(undefined, {
                        minimumFractionDigits: 2,
                        maximumFractionDigits: 2
                    });
                    priceEl.className = 'ticker-price';
                } else {
                    priceEl.textContent = 'N/A';
                    priceEl.className = 'ticker-price invalid';
                }
            }
        });
    } catch (e) {
        console.error('Ticker update failed:', e);
    }
}

function renderTickers() {
    const list = document.getElementById('tickerList');
    list.innerHTML = '';

    if (!config || !config.tickers) return;

    config.tickers.forEach((ticker, i) => {
        const div = document.createElement('div');
        div.className = 'ticker-item';
        div.innerHTML = `
            <div class="ticker-slot">#${i + 1}</div>
            <input type="text" value="${ticker.symbol || ''}" placeholder="Symbol" id="symbol-${i}">
            <input type="text" value="${ticker.apiId || ''}" placeholder="API ID" id="apiId-${i}">
            <select id="type-${i}">
                <option value="0" ${ticker.type === 0 ? 'selected' : ''}>Crypto</option>
                <option value="1" ${ticker.type === 1 ? 'selected' : ''}>Stock</option>
                <option value="2" ${ticker.type === 2 ? 'selected' : ''}>Forex</option>
            </select>
            <input type="number" value="${ticker.timeMultiplier || 1.0}" step="0.1" min="0.5" max="5" id="mult-${i}">
            <input type="checkbox" ${ticker.enabled ? 'checked' : ''} id="enabled-${i}">
            <div class="ticker-price" id="price-${i}">--</div>
            <button class="btn-remove" onclick="removeTicker(${i})">×</button>
        `;
        list.appendChild(div);
    });

    updateTickerCount();
    updateAddButton();
}

function addTicker() {
    if (!config.tickers) config.tickers = [];

    if (config.tickers.length >= MAX_TICKERS) {
        showMessage('Maximum ' + MAX_TICKERS + ' tickers allowed', 'error');
        return;
    }

    config.tickers.push({
        symbol: '',
        apiId: '',
        type: 0,
        timeMultiplier: 1.0,
        enabled: true
    });

    renderTickers();
}

function removeTicker(index) {
    if (!config.tickers) return;
    config.tickers.splice(index, 1);
    renderTickers();
}

function updateTickerCount() {
    const count = config.tickers ? config.tickers.length : 0;
    document.getElementById('tickerCount').textContent = count + '/' + MAX_TICKERS;
}

function updateAddButton() {
    const btn = document.getElementById('addBtn');
    btn.disabled = config.tickers && config.tickers.length >= MAX_TICKERS;
}

async function saveConfig() {
    if (!config) {
        showMessage('No config loaded', 'error');
        return;
    }

    config.brightness = parseInt(document.getElementById('brightness').value);
    config.baseTimeMs = parseInt(document.getElementById('baseTime').value) * 1000;
    config.coinGeckoApiKey = document.getElementById('coinGeckoKey').value;
    config.twelveDataApiKey = document.getElementById('twelveDataKey').value;

    config.tickers = config.tickers.map((ticker, i) => ({
        symbol: document.getElementById('symbol-' + i).value,
        apiId: document.getElementById('apiId-' + i).value,
        type: parseInt(document.getElementById('type-' + i).value),
        timeMultiplier: parseFloat(document.getElementById('mult-' + i).value),
        enabled: document.getElementById('enabled-' + i).checked
    }));

    config.numTickers = config.tickers.length;

    try {
        const res = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });

        if (res.ok) {
            showMessage('Configuration saved successfully', 'success');
            setTimeout(() => loadConfig(), 1000);
        } else {
            const text = await res.text();
            showMessage('Save failed: ' + text, 'error');
        }
    } catch (e) {
        showMessage('Save failed: ' + e.message, 'error');
    }
}

function uploadFirmware() {
    const fileInput = document.getElementById('firmwareFile');
    const file = fileInput.files[0];

    if (!file) {
        showMessage('Please select a firmware file', 'error');
        return;
    }

    if (!file.name.endsWith('.bin')) {
        showMessage('Please select a .bin file', 'error');
        return;
    }

    const formData = new FormData();
    formData.append('firmware', file);

    const xhr = new XMLHttpRequest();

    xhr.upload.addEventListener('progress', (e) => {
        if (e.lengthComputable) {
            const percent = (e.loaded / e.total) * 100;
            document.getElementById('uploadProgress').style.display = 'block';
            document.getElementById('progressBar').style.width = percent + '%';
            document.getElementById('progressBar').textContent = Math.round(percent) + '%';
        }
    });

    xhr.addEventListener('load', () => {
        if (xhr.status === 200) {
            showMessage('Firmware uploaded successfully. Device will restart...', 'success');
            document.getElementById('uploadProgress').style.display = 'none';
        } else {
            showMessage('Upload failed: ' + xhr.responseText, 'error');
            document.getElementById('uploadProgress').style.display = 'none';
        }
    });

    xhr.addEventListener('error', () => {
        showMessage('Upload failed: Network error', 'error');
        document.getElementById('uploadProgress').style.display = 'none';
    });

    xhr.open('POST', '/update');
    xhr.send(formData);
}

function restartDevice() {
    if (!confirm('Are you sure you want to restart the device?')) {
        return;
    }

    fetch('/api/restart', { method: 'POST' })
        .then(() => {
            showMessage('Device restarting...', 'success');
            clearInterval(statusInterval);
            clearInterval(tickerInterval);
        })
        .catch(e => {
            showMessage('Restart failed: ' + e.message, 'error');
        });
}

function showMessage(text, type) {
    const msg = document.getElementById('message');
    msg.textContent = text;
    msg.className = 'message ' + type;
    setTimeout(() => {
        msg.className = 'message';
        msg.style.display = 'none';
    }, 5000);
}

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(1) + ' MB';
}
