/*
  4x4 WS2812 Matrix - Realtime WebSocket Control
  ------------------------------------------------
  ESP32 hosts its own WiFi network (AP mode) and runs a WebSocket
  server. Connect your phone/laptop to this network, open the
  webapp (served from this same ESP32), click pixels -> they light
  up instantly. No reflashing needed after this is running.

  Libraries needed (Library Manager):
    - FastLED
    - ESPAsyncWebServer  (by me-no-dev / ESP32Async fork)
    - AsyncTCP           (dependency of the above, for ESP32)
*/

#include <WiFi.h>
#include <FastLED.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

// ---------- Matrix config ----------
#define LED_PIN     5        // GPIO you soldered IN to
#define NUM_LEDS    16
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define MATRIX_W    4
#define MATRIX_H    4

CRGB leds[NUM_LEDS];

// serpentine mapping, confirmed from your bring-up test
uint8_t XY(uint8_t x, uint8_t y) {
  if (y % 2 == 0) {
    return (y * MATRIX_W) + x;          // even rows: left -> right
  } else {
    return (y * MATRIX_W) + (MATRIX_W - 1 - x); // odd rows: right -> left
  }
}

// ---------- WiFi AP config ----------
const char* AP_SSID = "LEDMatrix";
const char* AP_PASS = "matrix123";   // min 8 chars, change if you want

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ---------- Webapp HTML (served directly from ESP32 flash) ----------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>4bit-Matriks</title>
<style>
    /* krvnium signature style */
    :root {
        --bg: #0f1115;
        --bg-panel: #171922;
        --border: #2a2e40;
        --text-muted: #828bb8;
        --text-main: #c8d3f5;
        --text-bright: #ffffff;
        --acc-purple: #c099ff;
        --acc-green: #c3e88d;
        --acc-pink: #ff757f;
        --acc-blue: #82aaff;
        
        --dot-red: #ff5f56;
        --dot-ylw: #ffbd2e;
        --dot-grn: #27c93f;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }
    
    body {
        font-family: 'IBM Plex Mono', 'Courier New', Courier, monospace;
        background-color: var(--bg);
        color: var(--text-main);
        line-height: 1.5;
        font-size: 14px;
        min-height: 100vh;
        display: flex;
        flex-direction: column;
    }

    /* Layout & Containers */
    .container {
        max-width: 900px;
        margin: 0 auto;
        padding: 2rem 1.5rem;
        flex: 1;
        width: 100%;
    }

    /* Terminal Header */
    .term-header {
        display: flex;
        align-items: center;
        gap: 0.5rem;
        margin-bottom: 2rem;
    }
    .dots {
        display: flex;
        gap: 6px;
    }
    .dot {
        width: 10px; height: 10px; border-radius: 50%;
    }
    .breadcrumb {
        color: var(--text-muted);
        font-size: 0.85rem;
        margin-left: 0.5rem;
    }

    /* Title Area */
    .hero { margin-bottom: 3rem; }
    h1 {
        font-size: 2rem;
        color: var(--acc-purple);
        font-weight: bold;
        margin-bottom: 0.5rem;
        letter-spacing: -0.5px;
    }
    .subtitle {
        display: flex;
        justify-content: space-between;
        align-items: center;
        color: var(--text-muted);
        font-size: 0.9rem;
    }
    .status-pill {
        color: var(--acc-green);
        font-weight: bold;
    }
    .status-pill.offline { color: var(--acc-pink); }

    /* Layout Grid */
    .layout {
        display: flex;
        flex-direction: column;
        gap: 3rem;
    }
    @media (min-width: 768px) {
        .layout {
            flex-direction: row;
            align-items: flex-start;
        }
        .col-left, .col-right { flex: 1; width: 50%; }
        .col-left { max-width: 400px; }
    }

    /* Sections / Timeline */
    .section {
        position: relative;
        padding-left: 1.5rem;
        border-left: 1px solid var(--border);
        margin-bottom: 2.5rem;
    }
    .section::before {
        content: '';
        position: absolute;
        left: -4px;
        top: 0.25rem;
        width: 7px;
        height: 7px;
        border-radius: 50%;
    }
    .sec-00::before { background-color: var(--acc-green); }
    .sec-01::before { background-color: var(--acc-purple); }

    .sec-header {
        font-size: 0.75rem;
        color: var(--text-muted);
        text-transform: uppercase;
        letter-spacing: 1px;
        margin-bottom: 1rem;
        display: flex;
        align-items: center;
        gap: 0.75rem;
    }
    h2 { font-size: 1.25rem; color: var(--text-bright); margin-bottom: 1rem; }

    /* Tags */
    .tags { display: flex; gap: 0.5rem; margin-bottom: 1.5rem; flex-wrap: wrap;}
    .tag {
        font-size: 0.75rem;
        padding: 0.2rem 0.6rem;
        border-radius: 12px;
        border: 1px solid transparent;
    }
    .tag-grn { color: var(--acc-green); border-color: rgba(195, 232, 141, 0.3); background: rgba(195, 232, 141, 0.05); }
    .tag-pur { color: var(--acc-purple); border-color: rgba(192, 153, 255, 0.3); background: rgba(192, 153, 255, 0.05); }
    .tag-blu { color: var(--acc-blue); border-color: rgba(130, 170, 255, 0.3); background: rgba(130, 170, 255, 0.05); }

    /* Resource Box (Panels) */
    .panel {
        background: var(--bg-panel);
        border: 1px solid var(--border);
        border-radius: 8px;
        padding: 1.25rem;
        margin-bottom: 1rem;
    }
    .panel-label {
        font-size: 0.7rem;
        color: var(--text-muted);
        margin-bottom: 0.75rem;
        letter-spacing: 1px;
    }

    /* Matrix Grid */
    .matrix {
        display: grid;
        grid-template-columns: repeat(4, 1fr);
        gap: 4px;
        width: 100%;
        aspect-ratio: 1;
        margin-bottom: 1rem;
    }
    .cell {
        background-color: #000000;
        border: 1px solid var(--border);
        border-radius: 6px;
        cursor: crosshair;
        transition: transform 0.1s;
    }
    .cell:active { transform: scale(0.95); }

    /* Custom Color Picker */
    .sv-picker {
        width: 100%;
        height: 160px;
        border-radius: 4px;
        position: relative;
        cursor: crosshair;
        background-color: #ff0000; /* Updated via JS */
        margin-bottom: 0.75rem;
        touch-action: none;
    }
    .sv-picker .sat {
        position: absolute; inset: 0; background: linear-gradient(to right, #fff, rgba(255,255,255,0)); border-radius: 4px; pointer-events: none;
    }
    .sv-picker .val {
        position: absolute; inset: 0; background: linear-gradient(to top, #000, rgba(0,0,0,0)); border-radius: 4px; pointer-events: none;
    }
    .sv-cursor {
        width: 12px; height: 12px;
        border: 2px solid #fff;
        border-radius: 50%;
        position: absolute;
        transform: translate(-50%, -50%);
        pointer-events: none;
        box-shadow: 0 0 2px rgba(0,0,0,0.5);
    }
    
    .hue-slider {
        width: 100%;
        appearance: none;
        height: 12px;
        border-radius: 6px;
        background: linear-gradient(to right, #f00 0%, #ff0 17%, #0f0 33%, #0ff 50%, #00f 67%, #f0f 83%, #f00 100%);
        outline: none;
        margin-bottom: 1rem;
    }
    .hue-slider::-webkit-slider-thumb {
        appearance: none; width: 16px; height: 16px; border-radius: 50%; background: #fff; cursor: pointer; border: 2px solid var(--border);
    }

    /* Controls & Inputs */
    .hex-row { display: flex; gap: 0.5rem; align-items: center; margin-bottom: 1rem; }
    .hex-row input {
        background: var(--bg); border: 1px solid var(--border);
        color: var(--text-bright); padding: 0.4rem 0.75rem;
        font-family: inherit; font-size: 0.9rem; border-radius: 4px; flex: 1;
    }
    
    .swatches { display: flex; gap: 0.5rem; flex-wrap: wrap; align-items: center; }
    .swatch {
        width: 24px; height: 24px; border-radius: 50%; cursor: pointer;
        border: 1px solid var(--border); flex-shrink: 0;
    }
    .swatch-add {
        background: transparent; color: var(--text-muted);
        display: flex; justify-content: center; align-items: center;
        font-size: 1.2rem; line-height: 1; border: 1px dashed var(--text-muted);
    }
    .swatch-add:hover { border-color: var(--text-main); color: var(--text-main); }

    /* Brightness Slider */
    .range-slider { width: 100%; appearance: none; height: 4px; background: var(--border); border-radius: 2px; outline: none; margin: 1rem 0; }
    .range-slider::-webkit-slider-thumb { appearance: none; width: 14px; height: 14px; border-radius: 50%; background: var(--acc-blue); cursor: pointer; }

    /* Buttons */
    .btn-row { display: flex; gap: 0.5rem; margin-top: 1rem; }
    button {
        background: var(--bg); border: 1px solid var(--border);
        color: var(--text-main); padding: 0.5rem 1rem;
        font-family: inherit; font-size: 0.8rem; cursor: pointer;
        border-radius: 4px; transition: all 0.2s;
        display: inline-flex; align-items: center; justify-content: center; gap: 0.4rem;
    }
    button:hover:not(:disabled) { background: var(--border); color: var(--text-bright); }
    button:disabled { opacity: 0.5; cursor: not-allowed; }
    .btn-primary { border-color: var(--acc-purple); color: var(--acc-purple); }
    .btn-primary:hover:not(:disabled) { background: rgba(192, 153, 255, 0.1); }
    .btn-danger { border-color: var(--acc-pink); color: var(--acc-pink); }
    
    /* Animation Strip */
    .frames-strip {
        display: flex; gap: 0.5rem; overflow-x: auto; padding-bottom: 0.5rem; margin-top: 1rem;
    }
    .frame-thumb {
        position: relative; width: 44px; flex-shrink: 0;
    }
    .frame-grid {
        display: grid; grid-template-columns: repeat(4, 1fr); gap: 1px;
        width: 100%; aspect-ratio: 1; background: var(--border); border: 1px solid var(--border); border-radius: 2px; overflow: hidden;
    }
    .frame-grid div { background: #000; }
    .frame-del {
        position: absolute; top: -6px; right: -6px; width: 16px; height: 16px;
        background: var(--acc-pink); color: #000; border: none; border-radius: 50%;
        font-size: 10px; display: flex; align-items: center; justify-content: center;
        cursor: pointer; padding: 0; min-width: 0; line-height: 1;
    }
    
    .footer {
        margin-top: 3rem; text-align: center; color: var(--text-muted); font-size: 0.75rem;
    }

</style>
</head>
<body>

<div class="container">
    <div class="term-header">
        <div class="dots">
            <span class="dot" style="background: var(--dot-red)"></span>
            <span class="dot" style="background: var(--dot-ylw)"></span>
            <span class="dot" style="background: var(--dot-grn)"></span>
        </div>
        <span class="breadcrumb">krvnium@arch ~ 4bit-matriks</span>
    </div>

    <div class="hero">
        <h1>4bit.matriks</h1>
        <div class="subtitle">
            <span>local network → websocket → embedded</span>
            <span class="status-pill" id="status-text">status: active</span>
        </div>
    </div>

    <div class="layout">
        <div class="col-left">
            <div class="section sec-00">
                <div class="sec-header">MATRIX</div>
                <h2>Interactive Grid</h2>
                <div class="tags">
                    <span class="tag tag-grn">LIVE SYNC</span>
                    <span class="tag tag-pur">4x4 WS2812</span>
                </div>
                
                <div class="matrix" id="matrix">
                    </div>
                
                <div class="panel">
                    <div class="panel-label">BRIGHTNESS</div>
                    <input type="range" class="range-slider" id="bright-slider" min="0" max="255" value="128">
                </div>
            </div>
        </div>

        <div class="col-right">
            <div class="section sec-01">
                <div class="sec-header">TOOLS</div>
                <h2>Color & Animation</h2>
                
                <div class="panel">
                    <div class="panel-label">COLOR PICKER</div>
                    
                    <div class="sv-picker" id="sv-picker">
                        <div class="sat"></div>
                        <div class="val"></div>
                        <div class="sv-cursor" id="sv-cursor"></div>
                    </div>
                    <input type="range" class="hue-slider" id="hue-slider" min="0" max="360" value="0">
                    
                    <div class="hex-row">
                        <span style="color: var(--text-muted);">#</span>
                        <input type="text" id="hex-input" value="FF0000" maxlength="6">
                        <div id="color-preview" style="width:28px; height:28px; border-radius:4px; border:1px solid var(--border); background:#ff0000;"></div>
                    </div>

                    <div class="swatches" id="swatches">
                        <div class="swatch swatch-add" id="btn-add-swatch">+</div>
                    </div>
                </div>

                <div class="panel">
                    <div class="panel-label">ANIMATION (MAX 10)</div>
                    <button id="btn-add-frame">+ Add Frame</button>
                    
                    <div class="frames-strip" id="frames-strip">
                        </div>

                    <div class="btn-row">
                        <button class="btn-primary" id="btn-play" disabled>▶ Play Once</button>
                        <button class="btn-primary" id="btn-loop" disabled>↻ Loop</button>
                        <button class="btn-danger" id="btn-stop" style="display:none;">■ Stop</button>
                    </div>
                </div>
            </div>
        </div>
    </div>
    
    <div class="footer">built by krvnium</div>
</div>

<script>
    // --- State ---
    let ws = null;
    let grid = Array(16).fill('#000000');
    let swatches = ['#ff0000', '#00ff00', '#0000ff', '#ffff00', '#ff00ff', '#00ffff', '#ffffff', '#000000'];
    let frames = [];
    
    // Color State (HSV)
    let curH = 0; // 0-360
    let curS = 1; // 0-1
    let curV = 1; // 0-1
    let curHex = '#ff0000';

    // Animation State
    let playInterval = null;
    let isPlaying = false;
    let animIdx = 0;

    // Throttle State
    let brightTimeout = null;
    let lastBrightSend = 0;

    // --- DOM Elements ---
    const elStatus = document.getElementById('status-text');
    const elMatrix = document.getElementById('matrix');
    const elSvPicker = document.getElementById('sv-picker');
    const elSvCursor = document.getElementById('sv-cursor');
    const elHueSlider = document.getElementById('hue-slider');
    const elHexInput = document.getElementById('hex-input');
    const elColorPreview = document.getElementById('color-preview');
    const elSwatches = document.getElementById('swatches');
    const elBtnAddSwatch = document.getElementById('btn-add-swatch');
    const elBrightSlider = document.getElementById('bright-slider');
    
    const elBtnAddFrame = document.getElementById('btn-add-frame');
    const elFramesStrip = document.getElementById('frames-strip');
    const elBtnPlay = document.getElementById('btn-play');
    const elBtnLoop = document.getElementById('btn-loop');
    const elBtnStop = document.getElementById('btn-stop');

    // --- WebSocket ---
    function connectWS() {
        const host = location.hostname || '192.168.4.1'; 
        ws = new WebSocket(`ws://${host}/ws`);
        
        ws.onopen = () => {
            elStatus.textContent = 'status: active';
            elStatus.className = 'status-pill';
        };
        
        ws.onclose = () => {
            elStatus.textContent = 'status: offline';
            elStatus.className = 'status-pill offline';
            setTimeout(connectWS, 2000); // auto-reconnect pattern
        };
        
        ws.onerror = () => { ws.close(); };
    }

    function sendPixels(pixelArray) {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ pixels: pixelArray }));
        }
    }

    function sendBrightness(val) {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ brightness: parseInt(val) }));
        }
    }

    // --- Init UI ---
    function initMatrix() {
        elMatrix.innerHTML = '';
        for (let i = 0; i < 16; i++) {
            const cell = document.createElement('div');
            cell.className = 'cell';
            cell.dataset.index = i;
            cell.addEventListener('mousedown', (e) => paintCell(i, e.target));
            elMatrix.appendChild(cell);
        }
    }

    function renderSwatches() {
        // Clear existing (keep the add button)
        const toRemove = elSwatches.querySelectorAll('.swatch:not(.swatch-add)');
        toRemove.forEach(el => el.remove());
        
        swatches.forEach(color => {
            const btn = document.createElement('div');
            btn.className = 'swatch';
            btn.style.backgroundColor = color;
            btn.addEventListener('click', () => setFromHex(color));
            elSwatches.insertBefore(btn, elBtnAddSwatch);
        });
    }

    // --- Matrix Interactions ---
    function paintCell(idx, el) {
        grid[idx] = curHex;
        el.style.backgroundColor = curHex;
        sendPixels(grid);
    }
    
    function updateGridVisuals(pixelArray) {
        const cells = elMatrix.children;
        for(let i=0; i<16; i++){
            cells[i].style.backgroundColor = pixelArray[i];
            grid[i] = pixelArray[i];
        }
    }

    // --- Color Math & Picker ---
    function hsvToRgb(h, s, v) {
        let r, g, b;
        let i = Math.floor(h / 60);
        let f = h / 60 - i;
        let p = v * (1 - s);
        let q = v * (1 - f * s);
        let t = v * (1 - (1 - f) * s);
        switch (i % 6) {
            case 0: r = v, g = t, b = p; break;
            case 1: r = q, g = v, b = p; break;
            case 2: r = p, g = v, b = t; break;
            case 3: r = p, g = q, b = v; break;
            case 4: r = t, g = p, b = v; break;
            case 5: r = v, g = p, b = q; break;
        }
        return [Math.round(r * 255), Math.round(g * 255), Math.round(b * 255)];
    }

    function rgbToHex(r, g, b) {
        return "#" + (1 << 24 | r << 16 | g << 8 | b).toString(16).slice(1).toUpperCase();
    }

    function hexToRgb(hex) {
        let shorthandRegex = /^#?([a-f\d])([a-f\d])([a-f\d])$/i;
        hex = hex.replace(shorthandRegex, (m, r, g, b) => r + r + g + g + b + b);
        let result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
        return result ? [parseInt(result[1], 16), parseInt(result[2], 16), parseInt(result[3], 16)] : null;
    }

    function rgbToHsv(r, g, b) {
        r /= 255, g /= 255, b /= 255;
        let max = Math.max(r, g, b), min = Math.min(r, g, b);
        let h, s, v = max;
        let d = max - min;
        s = max == 0 ? 0 : d / max;
        if (max == min) { h = 0; }
        else {
            switch (max) {
                case r: h = (g - b) / d + (g < b ? 6 : 0); break;
                case g: h = (b - r) / d + 2; break;
                case b: h = (r - g) / d + 4; break;
            }
            h /= 6;
        }
        return [h * 360, s, v];
    }

    function updateColor() {
        const rgb = hsvToRgb(curH, curS, curV);
        curHex = rgbToHex(rgb[0], rgb[1], rgb[2]);
        elHexInput.value = curHex.substring(1);
        elColorPreview.style.backgroundColor = curHex;
        
        // Update picker background (pure hue)
        const pureRgb = hsvToRgb(curH, 1, 1);
        elSvPicker.style.backgroundColor = rgbToHex(pureRgb[0], pureRgb[1], pureRgb[2]);
        
        // Update cursor
        elSvCursor.style.left = `${curS * 100}%`;
        elSvCursor.style.top = `${(1 - curV) * 100}%`;
    }

    function setFromHex(hex) {
        const rgb = hexToRgb(hex);
        if(!rgb) return;
        const hsv = rgbToHsv(rgb[0], rgb[1], rgb[2]);
        curH = hsv[0]; curS = hsv[1]; curV = hsv[2];
        elHueSlider.value = curH;
        updateColor();
    }

    // Picker SV Dragging
    let isDraggingSV = false;
    function handleSVMove(e) {
        if (!isDraggingSV) return;
        const rect = elSvPicker.getBoundingClientRect();
        
        // Handle both mouse and touch events
        let clientX = e.clientX;
        let clientY = e.clientY;
        if(e.touches && e.touches.length > 0) {
            clientX = e.touches[0].clientX;
            clientY = e.touches[0].clientY;
        }

        let x = Math.max(0, Math.min(clientX - rect.left, rect.width));
        let y = Math.max(0, Math.min(clientY - rect.top, rect.height));
        
        curS = x / rect.width;
        curV = 1 - (y / rect.height);
        updateColor();
    }

    elSvPicker.addEventListener('mousedown', (e) => { isDraggingSV = true; handleSVMove(e); });
    elSvPicker.addEventListener('touchstart', (e) => { isDraggingSV = true; handleSVMove(e); });
    document.addEventListener('mousemove', handleSVMove);
    document.addEventListener('touchmove', handleSVMove, {passive: false});
    document.addEventListener('mouseup', () => isDraggingSV = false);
    document.addEventListener('touchend', () => isDraggingSV = false);

    // Hue Slider
    elHueSlider.addEventListener('input', (e) => {
        curH = e.target.value;
        updateColor();
    });

    // Hex Input
    elHexInput.addEventListener('input', (e) => {
        let val = e.target.value;
        if (val.length === 6) setFromHex('#' + val);
    });

    // Add Swatch
    elBtnAddSwatch.addEventListener('click', () => {
        if (swatches.length < 12 && !swatches.includes(curHex)) {
            swatches.push(curHex);
            renderSwatches();
        }
    });

    // --- Brightness Slider ---
    elBrightSlider.addEventListener('input', (e) => {
        const val = e.target.value;
        const now = Date.now();
        if (now - lastBrightSend > 100) {
            sendBrightness(val);
            lastBrightSend = now;
        } else {
            clearTimeout(brightTimeout);
            brightTimeout = setTimeout(() => {
                sendBrightness(val);
                lastBrightSend = Date.now();
            }, 100);
        }
    });

    // --- Animation ---
    function renderFrames() {
        elFramesStrip.innerHTML = '';
        frames.forEach((fGrid, idx) => {
            const thumb = document.createElement('div');
            thumb.className = 'frame-thumb';
            
            const mGrid = document.createElement('div');
            mGrid.className = 'frame-grid';
            fGrid.forEach(c => {
                const px = document.createElement('div');
                px.style.backgroundColor = c;
                mGrid.appendChild(px);
            });
            
            const del = document.createElement('button');
            del.className = 'frame-del';
            del.textContent = '×';
            del.onclick = () => {
                frames.splice(idx, 1);
                renderFrames();
            };
            
            thumb.appendChild(mGrid);
            thumb.appendChild(del);
            elFramesStrip.appendChild(thumb);
        });

        const canPlay = frames.length >= 2;
        elBtnPlay.disabled = !canPlay;
        elBtnLoop.disabled = !canPlay;
        elBtnAddFrame.disabled = frames.length >= 10;
    }

    elBtnAddFrame.addEventListener('click', () => {
        if (frames.length < 10) {
            frames.push([...grid]); // deep copy current matrix state
            renderFrames();
        }
    });

    function playAnim(loop) {
        if (frames.length < 2) return;
        isPlaying = true;
        animIdx = 0;
        
        elBtnPlay.style.display = 'none';
        elBtnLoop.style.display = 'none';
        elBtnStop.style.display = 'inline-flex';

        playInterval = setInterval(() => {
            const f = frames[animIdx];
            updateGridVisuals(f);
            sendPixels(f);
            
            animIdx++;
            if (animIdx >= frames.length) {
                if (loop) animIdx = 0;
                else stopAnim();
            }
        }, 300); // Fixed interval per requirements
    }

    function stopAnim() {
        clearInterval(playInterval);
        isPlaying = false;
        elBtnPlay.style.display = 'inline-flex';
        elBtnLoop.style.display = 'inline-flex';
        elBtnStop.style.display = 'none';
    }

    elBtnPlay.addEventListener('click', () => playAnim(false));
    elBtnLoop.addEventListener('click', () => playAnim(true));
    elBtnStop.addEventListener('click', stopAnim);

    // --- Bootstrap ---
    initMatrix();
    renderSwatches();
    updateColor();
    connectWS();
</script>
</body>
</html>
)rawliteral";

// ---------- Helpers ----------
void hexToRGB(const String& hex, uint8_t &r, uint8_t &g, uint8_t &b) {
  // expects "#RRGGBB"
  long num = strtol(hex.c_str() + 1, NULL, 16);
  r = (num >> 16) & 0xFF;
  g = (num >> 8) & 0xFF;
  b = num & 0xFF;
}

void handleWsMessage(uint8_t *data, size_t len) {
  String msg = String((char*)data, len);

  // Brightness message: {"brightness": 0-255}
  int brightPos = msg.indexOf("\"brightness\"");
  if (brightPos != -1) {
    int colonPos = msg.indexOf(':', brightPos);
    int val = msg.substring(colonPos + 1).toInt(); // toInt() stops at trailing }/whitespace
    val = constrain(val, 0, 255);
    FastLED.setBrightness((uint8_t)val);
    FastLED.show(); // re-render current buffer at new brightness
    return;
  }

  // Pixel frame message: {"pixels":["#rrggbb", ... x16]}
  // Lightweight manual parse (no ArduinoJson dependency needed for this simple shape)
  int idx = 0;
  int searchStart = 0;
  while (idx < NUM_LEDS) {
    int hashPos = msg.indexOf('#', searchStart);
    if (hashPos == -1) break;
    String hex = msg.substring(hashPos, hashPos + 7); // "#rrggbb"
    uint8_t r, g, b;
    hexToRGB(hex, r, g, b);

    // map linear index i (row-major, 0..15 as sent by webapp) to (x,y) then to physical XY()
    uint8_t x = idx % MATRIX_W;
    uint8_t y = idx / MATRIX_W;
    leds[XY(x, y)] = CRGB(r, g, b);

    searchStart = hashPos + 7;
    idx++;
  }
  FastLED.show();
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    handleWsMessage(data, len);
  }
}

void setup() {
  Serial.begin(115200);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(50);
  FastLED.clear();
  FastLED.show();

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());  // usually 192.168.4.1

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });

  server.begin();
}

void loop() {
  ws.cleanupClients();
}
