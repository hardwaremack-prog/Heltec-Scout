// Scout Studio - page code: tabs, USB link, loading, charts, tables, map, exports
(() => {
'use strict';
const C = window.ScoutCore;
const $ = s => document.querySelector(s), $$ = s => [...document.querySelectorAll(s)];
const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const css = v => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const fmtT = t => t ? new Date(t * 1000).toLocaleString([], { month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit' }) : '—';
const fmtDur = s => s >= 3600 ? (s / 3600).toFixed(1) + ' h' : s >= 60 ? Math.round(s / 60) + ' min' : Math.round(s) + ' s';
const fmtDist = m => m >= 1000 ? (m / 1000).toFixed(1) + ' km' : Math.round(m) + ' m';

const S = { files: {}, M: null, track: [], wifiEst: {}, manual: {}, manualStart: {}, channels: [], tab: 'install', map: null, picking: false };
try { S.channels = JSON.parse(localStorage.getItem('scoutChannels') || '[]'); } catch (e) {}

// ---------- tabs ----------
$$('#tabs button').forEach(b => b.onclick = () => showTab(b.dataset.t));
function showTab(t) {
  S.tab = t;
  $$('#tabs button').forEach(x => x.classList.toggle('on', x.dataset.t === t));
  $$('section').forEach(s => s.classList.toggle('hide', s.id !== (t === 'map' ? 'map-s' : t)));
  if (S.M) renderTab(t);
}
function renderTab(t) {
  ({ overview: rOverview, wifi: rWifi, ble: rBle, lora: rLora, rf: rRf, map: rMap })[t]?.();
}

// ---------- loading ----------
function addFiles(sessFiles) {
  for (const sid in sessFiles) { S.files[sid] = Object.assign(S.files[sid] || {}, sessFiles[sid]); }
  rebuild();
}
async function rebuild() {
  if (!Object.keys(S.files).length) return;
  S.M = C.buildModel(S.files);
  for (const s of S.M.sessions) if (S.manualStart[s.id] != null) s.manualStart = S.manualStart[s.id];
  await C.decodeLora(S.M, S.channels);
  S.locStats = C.locate(S.M, { track: S.track, wifiEst: S.wifiEst, manual: S.manual });
  $$('#tabs button').forEach(b => b.disabled = false);
  $('#loaded').classList.remove('hide');
  rSessions(); rLocStats(); rChannels();
  if (S.tab !== 'data' && S.tab !== 'install') renderTab(S.tab);
}

async function readFileList(list) {
  const out = {};
  for (const f of list) {
    const text = await f.text();
    const path = (f.webkitRelativePath || f.name).replace(/\\/g, '/');
    if (/\.scout$/i.test(f.name) || text.startsWith('##SCOUT')) { const b = C.parseBundle(text, f.name.replace(/\.\w+$/, '')); for (const s in b) out[s] = Object.assign(out[s] || {}, b[s]); continue; }
    if (!/\.(csv|txt)$/i.test(f.name)) continue;
    const m = path.match(/(s\d{4})\/[^/]+$/);           // .../scout/s0042/wifi.csv
    const sid = m ? m[1] : 'imported';
    (out[sid] = out[sid] || {})[f.name] = text;
  }
  if (!Object.keys(out).length) { alert('No Scout logs found in those files.'); return; }
  addFiles(out);
}
$('#fileIn').onchange = e => readFileList(e.target.files);
$('#dirIn').onchange = e => readFileList(e.target.files);
const drop = $('#drop');
drop.ondragover = e => { e.preventDefault(); drop.classList.add('over'); };
drop.ondragleave = () => drop.classList.remove('over');
drop.ondrop = e => { e.preventDefault(); drop.classList.remove('over'); readFileList(e.dataTransfer.files); };
$('#sample').onclick = async () => {
  const t = await (await fetch('sample/sample.scout')).text();
  addFiles(C.parseBundle(t));
  const g = await (await fetch('sample/sample-track.gpx')).text();
  S.track = C.parseTrack(g); $('#trackInfo').textContent = `Sample phone track: ${S.track.length} points.`;
  await rebuild(); showTab('overview');
};

// ---------- sessions & clock ----------
function rSessions() {
  const M = S.M;
  $('#sessTable').innerHTML = '<tr><th>Session</th><th>Board</th><th>Wi-Fi</th><th>BT</th><th>LoRa</th><th>900 MHz</th><th>Clock</th><th>Start time</th></tr>' +
    M.sessions.map(s => {
      const rows = M.wifi.concat(M.ble, M.lora, M.rf, M.events).filter(r => r.s === s.id);
      const up0 = rows.reduce((a, r) => Math.min(a, r.up), 1e12), up1 = rows.reduce((a, r) => Math.max(a, r.up), 0);
      const t0 = s.offset != null ? s.offset + (up0 < 1e12 ? up0 : 0) : null;
      const clock = s.offset != null ? '✓ set' : s.manualStart != null ? 'typed in' : '<span class="pill bad">unknown</span>';
      const val = s.manualStart != null ? new Date((s.manualStart) * 1000 - new Date().getTimezoneOffset() * 60000).toISOString().slice(0, 16) : '';
      return `<tr><td><b>${s.id}</b><div class=muted>${up1 ? fmtDur(up1 - Math.min(up0, up1)) : ''}</div></td><td>${esc(s.info.board || '')}</td>
        <td class=n>${s.counts.wifi || 0}</td><td class=n>${s.counts.ble || 0}</td><td class=n>${s.counts.lora || 0}</td><td class=n>${s.counts.rf || 0}</td><td>${clock}</td>
        <td>${s.offset != null ? fmtT(t0) : `<input type="datetime-local" data-start="${s.id}" value="${val}">`}</td></tr>`;
    }).join('');
  $('#pinSess').innerHTML = M.sessions.map(s => `<option>${s.id}</option>`).join('');
}
$('#sessTable').onchange = e => {
  const sid = e.target.dataset.start; if (!sid) return;
  S.manualStart[sid] = e.target.value ? Date.parse(e.target.value) / 1000 : null;   // treated as uptime 0 for that session
  rebuild();
};

// ---------- locations ----------
function rLocStats() {
  const n = S.locStats, tot = Object.values(n).reduce((a, b) => a + b, 0) || 1;
  const card = (k, v, c) => `<div class="card stat"><div class="k">${k}</div><div class="v"${c ? ` style="color:${c}"` : ''}>${Math.round(v / tot * 100)}%</div></div>`;
  $('#locStats').innerHTML = card('Board GPS', n.gps) + card('Phone track', n.track) + card('Wi-Fi estimate', n.wifi) + card('Fixed / pin', n.fixed + n.manual) + card('No location', n.none, n.none ? css('--bad') : '');
}
$('#trackIn').onchange = async e => {
  let pts = [];
  for (const f of e.target.files) pts = pts.concat(C.parseTrack(await f.text()));
  S.track = pts.sort((a, b) => a.t - b.t);
  $('#trackInfo').textContent = S.track.length ? `${S.track.length} points, ${fmtT(S.track[0].t)} to ${fmtT(S.track[S.track.length - 1].t)}.` : 'No timed points found in that file.';
  rebuild();
};
$('#geoSvc').onchange = e => { $('#geoKey').classList.toggle('hide', e.target.value === 'beacondb'); $('#geoKey').placeholder = e.target.value === 'google' ? 'Google API key' : 'https://…/v1/geolocate'; };
$('#geoGo').onclick = async () => {
  const svc = $('#geoSvc').value, key = $('#geoKey').value.trim();
  const url = svc === 'beacondb' ? 'https://api.beacondb.net/v1/geolocate' : svc === 'google' ? 'https://www.googleapis.com/geolocation/v1/geolocate?key=' + encodeURIComponent(key) : key;
  if (!/^https:\/\//.test(url) || (svc === 'google' && !key)) { $('#geoInfo').textContent = 'Enter a key or URL first.'; return; }
  // only scans that don't already have GPS or a track point
  let scans = C.wifiScans(S.M).filter(sc => !sc.aps.some(a => a.L && (a.L.src === 'gps' || a.L.src === 'track')));
  const max = Math.max(1, +$('#geoMax').value || 150);
  if (scans.length > max) { const step = scans.length / max; scans = Array.from({ length: max }, (_, i) => scans[Math.floor(i * step)]); }
  if (!scans.length) { $('#geoInfo').textContent = 'Every Wi-Fi scan already has a better location.'; return; }
  $('#geoProg').classList.remove('hide'); $('#geoGo').disabled = true;
  let ok = 0, miss = 0, err = '';
  for (let i = 0; i < scans.length; i++) {
    $('#geoProg i').style.width = (i / scans.length * 100) + '%';
    const body = C.geolocateBody(scans[i]); if (!body) { miss++; continue; }
    try {
      const r = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
      if (r.status === 404) { miss++; }
      else if (!r.ok) { err = `The service said ${r.status}.`; if (r.status === 403 || r.status === 429) break; }
      else { const j = await r.json(); if (j.location) { S.wifiEst[scans[i].s + '|' + scans[i].up] = { lat: j.location.lat, lon: j.location.lng, acc: Math.round(j.accuracy || 100) }; ok++; } else miss++; }
    } catch (e) { err = 'Couldn\'t reach the service (offline, or it blocks requests from web pages).'; break; }
    $('#geoInfo').textContent = `${ok} found, ${miss} not in the database…`;
    if (svc === 'beacondb') await new Promise(r => setTimeout(r, 300));    // be polite to a free service
  }
  $('#geoProg i').style.width = '100%'; $('#geoGo').disabled = false;
  $('#geoInfo').textContent = `${ok} scans located, ${miss} not in the database. ${err}`;
  rebuild();
};
$('#pinSet').onclick = () => {
  const lat = parseFloat($('#pinLat').value), lon = parseFloat($('#pinLon').value);
  if (!isFinite(lat) || !isFinite(lon)) return;
  S.manual[$('#pinSess').value] = { lat, lon }; rebuild();
};
$('#pinClear').onclick = () => { delete S.manual[$('#pinSess').value]; rebuild(); };
$('#pinPick').onclick = () => { S.picking = true; showTab('map'); $('#mapNote').textContent = 'Tap the map where session ' + $('#pinSess').value + ' was.'; };

// ---------- Meshtastic channels ----------
function rChannels() {
  $('#chList').innerHTML = S.channels.length ? 'Your channels: ' + S.channels.map((c, i) => `<span class=pill>${esc(c.name)} <a href="#" data-del="${i}">✕</a></span>`).join('') : '';
}
$('#chAdd').onclick = () => {
  const name = $('#chName').value.trim(), psk = $('#chPsk').value.trim();
  if (!name || !psk) return;
  try { C.keyFromPsk(psk); } catch (e) { alert('That key isn\'t valid base64.'); return; }
  S.channels.push({ name, psk }); try { localStorage.setItem('scoutChannels', JSON.stringify(S.channels)); } catch (e) {}
  $('#chName').value = $('#chPsk').value = ''; rebuild();
};
$('#chList').onclick = e => { const i = e.target.dataset.del; if (i == null) return; e.preventDefault(); S.channels.splice(+i, 1); try { localStorage.setItem('scoutChannels', JSON.stringify(S.channels)); } catch (x) {} rebuild(); };

// ---------- USB link ----------
class Link {
  constructor() { this.buf = new Uint8Array(0); this.waiters = []; }
  async open() {
    this.port = await navigator.serial.requestPort();
    await this.port.open({ baudRate: 921600, bufferSize: 1 << 16 });
    try { await this.port.setSignals({ dataTerminalReady: false, requestToSend: false }); } catch (e) {}
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    this.alive = true;
    (async () => {
      try { while (this.alive) { const { value, done } = await this.reader.read(); if (done) break; if (value) { const n = new Uint8Array(this.buf.length + value.length); n.set(this.buf); n.set(value, this.buf.length); this.buf = n; this.waiters.splice(0).forEach(w => w()); } } }
      catch (e) { this.alive = false; this.waiters.splice(0).forEach(w => w()); }
    })();
  }
  wait(ms) { return new Promise(res => { const t = setTimeout(res, ms); this.waiters.push(() => { clearTimeout(t); res(); }); }); }
  async line(timeout = 4000) {
    const end = Date.now() + timeout;
    for (;;) {
      const i = this.buf.indexOf(10);
      if (i >= 0) { const l = new TextDecoder().decode(this.buf.subarray(0, i)).replace(/\r$/, ''); this.buf = this.buf.slice(i + 1); return l; }
      if (!this.alive || Date.now() > end) throw new Error('The board stopped answering.');
      await this.wait(end - Date.now());
    }
  }
  async bytes(n, onProg) {
    const end = () => Date.now() + 8000; let deadline = end(); const out = new Uint8Array(n); let got = 0;
    while (got < n) {
      if (this.buf.length) { const k = Math.min(n - got, this.buf.length); out.set(this.buf.subarray(0, k), got); this.buf = this.buf.slice(k); got += k; deadline = end(); onProg && onProg(got); continue; }
      if (!this.alive || Date.now() > deadline) throw new Error('Transfer stalled.');
      await this.wait(deadline - Date.now());
    }
    return out;
  }
  async send(s) { await this.writer.write(new TextEncoder().encode(s + '\n')); }
  async until(prefix, timeout) { for (;;) { const l = await this.line(timeout); if (l.startsWith('@ERR')) throw new Error(l.slice(5)); if (l.startsWith(prefix)) return l; } }
  async info() {
    for (let i = 0; i < 4; i++) {
      this.buf = new Uint8Array(0); await this.send('?INFO');
      try { const l = await this.until('@INFO ', 2500); return JSON.parse(l.slice(6)); } catch (e) { await this.wait(1200); }
    }
    throw new Error('No answer. Is Heltec Scout installed on this board?');
  }
  async ls(dir) {
    await this.send('?LS ' + dir); const out = [];
    for (;;) { const l = await this.line(); if (l.startsWith('@ERR')) throw new Error(l); if (l === '@END') return out;
      if (l.startsWith('@F ')) { const m = l.match(/^@F (\d+) (.+)$/); out.push({ dir: false, size: +m[1], path: m[2] }); }
      else if (l.startsWith('@D ')) out.push({ dir: true, path: l.slice(3) }); }
  }
  async get(path, onProg) {
    await this.send('?GET ' + path);
    const h = await this.until('@DATA ', 5000); const n = +h.slice(6);
    const data = await this.bytes(n, onProg);
    await this.until('@END', 5000);
    return data;
  }
  async close() { this.alive = false; try { await this.reader.cancel(); this.reader.releaseLock(); this.writer.releaseLock(); await this.port.close(); } catch (e) {} }
}
let link = null;
$('#usbConnect').onclick = async () => {
  if (!('serial' in navigator)) { $('#usbInfo').textContent = 'USB needs Chrome or Edge on a computer. You can still load files.'; return; }
  try {
    link = new Link(); await link.open();
    $('#usbInfo').textContent = 'Talking to the board…';
    await link.wait(600);
    const info = await link.info();
    await link.send('?TIME ' + Math.floor(Date.now() / 1000)); await link.until('@OK');
    const list = (await link.ls('/scout')).filter(f => f.dir).map(f => f.path.split('/').pop()).sort().reverse();
    $('#usbInfo').innerHTML = `${esc(info.board)} · fw ${esc(info.fw)} · ${info.store === 'none' ? 'no storage' : esc(info.store)} · clock set ✓`;
    $('#usbSessions').innerHTML = list.length ? `<div class=row><label class=muted><input type=checkbox id=usbAll> all</label></div>` +
      list.map(s => `<div><label><input type=checkbox value="${s}" ${s === list[0] ? 'checked' : ''}> ${s}${'/scout/' + s === info.dir ? ' <span class=muted>(running now)</span>' : ''}</label></div>`).join('') +
      `<div class=row><button class=btn id=usbPull>Download</button></div>` : '<p class=muted>No sessions on the card yet.</p>';
    $('#usbAll') && ($('#usbAll').onchange = e => $$('#usbSessions input[value]').forEach(x => x.checked = e.target.checked));
    $('#usbPull') && ($('#usbPull').onclick = pull);
    $('#usbConnect').classList.add('hide'); $('#usbClose').classList.remove('hide');
  } catch (e) { $('#usbInfo').textContent = e.message || String(e); if (link) await link.close(); link = null; }
};
$('#usbClose').onclick = async () => { if (link) await link.close(); link = null; $('#usbConnect').classList.remove('hide'); $('#usbClose').classList.add('hide'); $('#usbSessions').innerHTML = ''; $('#usbInfo').textContent = 'Disconnected.'; };
async function pull() {
  const pick = $$('#usbSessions input[value]').filter(x => x.checked).map(x => x.value);
  if (!pick.length) return;
  $('#usbProg').classList.remove('hide'); $('#usbPull').disabled = true;
  try {
    const jobs = [];
    for (const s of pick) for (const f of await link.ls('/scout/' + s)) if (!f.dir) jobs.push(Object.assign(f, { s }));
    const total = jobs.reduce((a, j) => a + j.size, 0) || 1; let done = 0; const out = {};
    for (const j of jobs) {
      $('#usbInfo').textContent = `Downloading ${j.s}/${j.path.split('/').pop()}…`;
      const data = await link.get(j.path, g => { $('#usbProg i').style.width = ((done + g) / total * 100) + '%'; });
      done += j.size;
      (out[j.s] = out[j.s] || {})[j.path.split('/').pop()] = new TextDecoder().decode(data);
    }
    $('#usbInfo').textContent = `Got ${pick.length} session${pick.length > 1 ? 's' : ''} (${(total / 1e6).toFixed(1)} MB).`;
    addFiles(out);
  } catch (e) { $('#usbInfo').textContent = 'Stopped: ' + e.message; }
  $('#usbPull').disabled = false;
}

// ---------- charts ----------
function canvas2d(cv) {
  const r = cv.getBoundingClientRect(), d = devicePixelRatio || 1;
  cv.width = Math.max(1, r.width * d); cv.height = Math.max(1, r.height * d);
  const g = cv.getContext('2d'); g.setTransform(d, 0, 0, d, 0, 0); g.clearRect(0, 0, r.width, r.height);
  g.font = '11px system-ui'; return [g, r.width, r.height];
}
function lineChart(cv, series, xLabel) {
  const [g, W, H] = canvas2d(cv); const P = { l: 34, r: 8, t: 8, b: 20 };
  const xs = series.flatMap(s => s.pts.map(p => p[0])); if (!xs.length) return;
  const x0 = Math.min(...xs), x1 = Math.max(...xs) || x0 + 1;
  g.strokeStyle = css('--line'); g.fillStyle = css('--mute');
  for (let i = 0; i <= 4; i++) { const y = P.t + (H - P.t - P.b) * i / 4; g.beginPath(); g.moveTo(P.l, y); g.lineTo(W - P.r, y); g.stroke(); }
  for (const s of series) {
    const ym = Math.max(1, ...s.pts.map(p => p[1]));
    g.strokeStyle = s.color; g.lineWidth = 1.6; g.beginPath();
    s.pts.forEach((p, i) => { const x = P.l + (p[0] - x0) / (x1 - x0 || 1) * (W - P.l - P.r), y = H - P.b - p[1] / ym * (H - P.t - P.b); i ? g.lineTo(x, y) : g.moveTo(x, y); });
    g.stroke(); g.lineWidth = 1;
  }
  g.fillText(xLabel(x0), P.l, H - 4); const e = xLabel(x1); g.fillText(e, W - P.r - g.measureText(e).width, H - 4);
}
function barChart(cv, labels, vals, color, opts = {}) {
  const [g, W, H] = canvas2d(cv); const P = { l: 8, r: 8, t: 8, b: 30 };
  const n = vals.length; if (!n) return; const m = Math.max(1, ...vals); const bw = (W - P.l - P.r) / n;
  vals.forEach((v, i) => { const h = v / m * (H - P.t - P.b); g.fillStyle = color; g.fillRect(P.l + i * bw + 1, H - P.b - h, Math.max(1, bw - 2), h);
    if (opts.values && v) { g.fillStyle = css('--mute'); const t = String(v); g.fillText(t, P.l + i * bw + bw / 2 - g.measureText(t).width / 2, H - P.b - h - 3); } });
  g.fillStyle = css('--mute');
  const every = Math.ceil(n / Math.max(1, Math.floor(W / 46)));
  labels.forEach((l, i) => { if (i % every) return; const t = String(l); g.fillText(t, P.l + i * bw + bw / 2 - g.measureText(t).width / 2, H - P.b + 14); });
  return { g, W, H, P, bw };
}

// time axis: real time when known, else minutes since power-up
function tx(r) { const t = C.timeOf(S.M, r); return t != null ? t : null; }
const hasClock = () => S.M.sessions.some(s => s.offset != null || s.manualStart != null);

// ---------- overview ----------
function rOverview() {
  const M = S.M, ws = C.wifiSummary(M), bs = C.bleSummary(M), ns = C.loraNodes(M), rf = C.rfSummary(M);
  const stat = (k, v, sub) => `<div class="card stat"><div class="k">${k}</div><div class="v">${v}</div>${sub ? `<div class=muted>${sub}</div>` : ''}</div>`;
  const times = C.KINDS && [].concat(M.wifi, M.ble, M.lora, M.rf).map(tx).filter(t => t != null);
  const span = times.length ? Math.max(...times) - Math.min(...times) : 0;
  $('#ovStats').innerHTML = stat('Sessions', M.sessions.length, span ? fmtDur(span) + ' of logs' : '') +
    stat('Wi-Fi networks', ws.length, ws.filter(a => a.enc === 'open').length + ' open') +
    stat('Bluetooth devices', bs.length, bs.filter(d => d.name).length + ' with names') +
    stat('LoRa nodes', ns.length, M.lora.length + ' packets') +
    stat('900 MHz hits', M.rf.filter(r => r.kind === 'hit').length, rf.hits.length + ' frequencies');
  const notes = [];
  const fol = bs.filter(d => d.follower);
  if (fol.length) notes.push(`<div class="alert"><b>${fol.length} Bluetooth device${fol.length > 1 ? 's' : ''} seemed to follow you</b> — ${fol.slice(0, 4).map(d => esc(d.name || d.kind || d.addr)).join(', ')}. See the Bluetooth tab.</div>`);
  const open = [...new Set(ws.filter(a => a.enc === 'open' && a.ssid).map(a => a.ssid))];
  if (open.length) notes.push(`<div class="card">🔓 <b>${open.length} open Wi-Fi network name${open.length > 1 ? 's' : ''}</b>: ${open.slice(0, 6).map(esc).join(', ')}${open.length > 6 ? '…' : ''}</div>`);
  const named = ns.filter(n => n.longName);
  if (ns.length) notes.push(`<div class="card">📡 <b>Meshtastic</b>: heard ${ns.length} node${ns.length > 1 ? 's' : ''}${named.length ? ' including ' + named.slice(0, 5).map(n => esc(n.longName)).join(', ') : ''}. Best signal ${Math.max(...ns.map(n => n.best)).toFixed(0)} dBm.</div>`);
  const texts = M.lora.filter(p => p.dec && p.dec.text);
  if (texts.length) notes.push(`<div class="card">💬 <b>${texts.length} text message${texts.length > 1 ? 's' : ''}</b> on public channels. See the LoRa tab.</div>`);
  if (rf.hits.length) { const busy = rf.hits.slice().sort((a, b) => b.n - a.n)[0]; const spread = rf.hits.length > 40;
    notes.push(`<div class="card">📶 <b>900 MHz</b>: busiest at ${busy.f.toFixed(3)} MHz. ${spread ? 'Activity is spread across the band, which looks like frequency-hopping gear such as smart meters.' : 'Activity sits on a few fixed frequencies.'}</div>`); }
  if (S.locStats.none) notes.push(`<div class="warn">${S.locStats.none} rows have no location yet. Add a phone track, a Wi-Fi estimate or a pin on the <a href="#" onclick="return false" id="goData">Get data</a> tab.</div>`);
  $('#ovNotes').innerHTML = notes.join('') || '<p class=muted>Nothing unusual.</p>';
  $('#goData') && ($('#goData').onclick = () => showTab('data'));
  // activity: per minute buckets on one axis (real time if known)
  const clk = hasClock();
  const key = r => clk ? tx(r) : r.up;
  const per = (rows, fn) => { const m = new Map(); for (const r of rows) { const k = key(r); if (k == null) continue; const b = Math.floor(k / 60) * 60; m.set(b, fn(m.get(b) || 0, r)); } return [...m.entries()].sort((a, b) => a[0] - b[0]); };
  const scans = new Map(); for (const r of M.wifi) { const k = r.s + '|' + r.up; scans.set(k, (scans.get(k) || 0) + 1); }
  const wpts = []; for (const [k, v] of scans) { const [s, up] = k.split('|'); const t = key({ s, up: +up, utc: null }); if (t != null) wpts.push([t, v]); } wpts.sort((a, b) => a[0] - b[0]);
  const bw = new Map(); for (const r of M.ble) { const k = r.s + '|' + r.up; bw.set(k, (bw.get(k) || 0) + 1); }
  const bpts = []; for (const [k, v] of bw) { const [s, up] = k.split('|'); const t = key({ s, up: +up, utc: null }); if (t != null) bpts.push([t, v]); } bpts.sort((a, b) => a[0] - b[0]);
  lineChart($('#ovChart'), [
    { color: css('--wifi'), pts: wpts }, { color: css('--ble'), pts: bpts },
    { color: css('--lora'), pts: per(M.lora, a => a + 1) }, { color: css('--rf'), pts: per(M.rf.filter(r => r.kind === 'hit'), a => a + 1) }],
    v => clk ? fmtT(v) : Math.round(v / 60) + ' min');
  $('#ovAxis').textContent = clk ? '' : 'The board\'s clock was never set, so the time line is minutes since power-up.';
}

// ---------- tables ----------
function table(el, cols, rows, opts = {}) {
  const st = el._sort || (el._sort = { k: opts.sort, d: opts.dir || -1 });
  const sorted = st.k ? rows.slice().sort((a, b) => { const c = cols.find(c => c.k === st.k), va = c.v ? c.v(a) : a[st.k], vb = c.v ? c.v(b) : b[st.k];
    return (va > vb ? 1 : va < vb ? -1 : 0) * st.d; }) : rows;
  const shown = sorted.slice(0, opts.max || 600);
  el.innerHTML = '<tr>' + cols.map(c => `<th data-k="${c.k}"${c.n ? ' style="text-align:right"' : ''}>${c.h}${st.k === c.k ? (st.d > 0 ? ' ▲' : ' ▼') : ''}</th>`).join('') + '</tr>' +
    shown.map(r => '<tr>' + cols.map(c => `<td${c.n ? ' class=n' : ''}>${c.f ? c.f(r) : esc(r[c.k])}</td>`).join('') + '</tr>').join('') +
    (sorted.length > shown.length ? `<tr><td colspan=${cols.length} class=muted>…and ${sorted.length - shown.length} more (use search or Export)</td></tr>` : '');
  el.onclick = e => { const th = e.target.closest('th'); if (!th) return; st.d = st.k === th.dataset.k ? -st.d : -1; st.k = th.dataset.k; table(el, cols, rows, opts); };
}
const timeCell = r => fmtT(C.timeOf(S.M, r)) + (C.timeOf(S.M, r) ? '' : ` <span class=muted>(+${fmtDur(r.up)})</span>`);

// ---------- wifi ----------
function rWifi() {
  const ws = C.wifiSummary(S.M);
  const stat = (k, v) => `<div class="card stat"><div class="k">${k}</div><div class="v">${v}</div></div>`;
  const ssids = new Set(ws.map(a => a.ssid).filter(Boolean));
  $('#wStats').innerHTML = stat('Access points', ws.length) + stat('Network names', ssids.size) + stat('Open', ws.filter(a => a.enc === 'open').length) +
    stat('Hidden', ws.filter(a => !a.ssid).length) + stat('WPA3', ws.filter(a => /wpa3/.test(a.enc)).length) + stat('Located', ws.filter(a => a.lat != null).length);
  const ch = Array.from({ length: 14 }, (_, i) => ws.filter(a => a.ch === i + 1).length);
  barChart($('#wChan'), ch.map((_, i) => i + 1), ch, css('--wifi'), { values: true });
  const draw = () => {
    const q = $('#wFind').value.toLowerCase(), open = $('#wOpen').checked;
    const rows = ws.filter(a => (!q || (a.ssid || '').toLowerCase().includes(q) || a.bssid.includes(q)) && (!open || a.enc === 'open'));
    table($('#wTable'), [
      { k: 'ssid', h: 'Network', f: a => (a.ssid ? esc(a.ssid) : '<i class=muted>hidden</i>') + (a.random ? ' <span class=pill>hotspot</span>' : '') },
      { k: 'bssid', h: 'Address', f: a => `<span class=muted>${a.bssid}</span>` },
      { k: 'ch', h: 'Ch', n: 1 }, { k: 'enc', h: 'Security', f: a => a.enc === 'open' ? '<span class="pill bad">open</span>' : esc(a.enc) },
      { k: 'best', h: 'Best dBm', n: 1 }, { k: 'n', h: 'Seen', n: 1 },
      { k: 'first', h: 'First seen', v: a => C.timeOf(S.M, a.first) || a.first.up, f: a => timeCell(a.first) },
      { k: 'lat', h: 'Spot', f: a => a.lat != null ? `<a href="#" data-go="${a.lat},${a.lon}">map</a>` : '' }], rows, { sort: 'best' });
  };
  $('#wFind').oninput = draw; $('#wOpen').onchange = draw; draw();
}

// ---------- bluetooth ----------
function rBle() {
  const bs = C.bleSummary(S.M);
  const fol = bs.filter(d => d.follower);
  $('#bAlert').innerHTML = fol.length ? `<div class="alert"><b>Possibly following you:</b> ${fol.map(d => `${esc(d.name || d.kind || 'unnamed')} <span class=muted>${d.addr}</span> — ${fmtDur(d.span)}, ${fmtDist(d.travelled)}`).join('<br>')}
    <div class=muted style="margin-top:6px">Often this is your own phone, watch or earbuds. If it's a FindMy, Tile or SmartTag you don't own, your phone's tracker-alert feature can help find it.</div></div>` : '';
  const stat = (k, v) => `<div class="card stat"><div class="k">${k}</div><div class="v">${v}</div></div>`;
  const kinds = {}; for (const d of bs) { const k = d.kind || 'Other'; kinds[k] = (kinds[k] || 0) + 1; }
  const kk = Object.entries(kinds).sort((a, b) => b[1] - a[1]).slice(0, 14);
  $('#bStats').innerHTML = stat('Devices', bs.length) + stat('Named', bs.filter(d => d.name).length) + stat('Trackers', bs.filter(d => /FindMy|Tile|SmartTag/.test(d.kind)).length) + stat('Followed you', fol.length);
  barChart($('#bKinds'), kk.map(x => x[0]), kk.map(x => x[1]), css('--ble'), { values: true });
  const draw = () => {
    const q = $('#bFind').value.toLowerCase();
    const rows = bs.filter(d => !q || (d.name || '').toLowerCase().includes(q) || (d.kind || '').toLowerCase().includes(q) || d.addr.includes(q));
    table($('#bTable'), [
      { k: 'name', h: 'Device', f: d => (d.name ? esc(d.name) : '<i class=muted>no name</i>') + (d.follower ? ' <span class="pill bad">followed you</span>' : '') },
      { k: 'kind', h: 'Type' }, { k: 'addr', h: 'Address', f: d => `<span class=muted>${d.addr}</span>${d.atype === 'random' ? ' <span class=pill>random</span>' : ''}` },
      { k: 'best', h: 'Best dBm', n: 1 }, { k: 'windows', h: 'Seen', n: 1 }, { k: 'span', h: 'Around for', n: 1, f: d => d.span ? fmtDur(d.span) : '' },
      { k: 'travelled', h: 'Over', n: 1, f: d => d.travelled ? fmtDist(d.travelled) : '' },
      { k: 'first', h: 'First seen', v: d => C.timeOf(S.M, d.first) || d.first.up, f: d => timeCell(d.first) }], rows, { sort: 'windows' });
  };
  $('#bFind').oninput = draw; draw();
}

// ---------- lora ----------
function rLora() {
  const M = S.M, ns = C.loraNodes(M);
  const stat = (k, v) => `<div class="card stat"><div class="k">${k}</div><div class="v">${v}</div></div>`;
  $('#lStats').innerHTML = stat('Packets', M.lora.length) + stat('Nodes', ns.length) + stat('Decoded', M.lora.filter(p => p.dec).length) +
    stat('With position', ns.filter(n => n.lat != null).length) + stat('Best signal', ns.length ? Math.max(...ns.map(n => n.best)).toFixed(0) + ' dBm' : '—');
  table($('#lNodes'), [
    { k: 'id', h: 'Node', f: n => `<b>${esc(n.longName || '')}</b>${n.shortName ? ` <span class=pill>${esc(n.shortName)}</span>` : ''}<div class=muted>!${n.id}</div>` },
    { k: 'hw', h: 'Hardware', f: n => esc(n.hw || '') }, { k: 'pkts', h: 'Packets', n: 1 }, { k: 'best', h: 'Best dBm', n: 1 }, { k: 'bestSnr', h: 'Best SNR', n: 1 },
    { k: 'direct', h: 'Direct', n: 1, f: n => n.direct ? n.direct : '' },
    { k: 'ports', h: 'Sent', f: n => Object.entries(n.ports).map(([k, v]) => `<span class=pill>${esc(k)} ${v}</span>`).join('') },
    { k: 'lat', h: 'Position', f: n => n.lat != null ? `<a href="#" data-go="${n.lat},${n.lon}">${n.lat.toFixed(4)}, ${n.lon.toFixed(4)}</a>` : '' }], ns, { sort: 'pkts' });
  const names = Object.fromEntries(ns.map(n => [n.id, n.longName || '!' + n.id]));
  const texts = M.lora.filter(p => p.dec && p.dec.text);
  $('#lMsgs').innerHTML = texts.length ? texts.map(p => `<div><b>${esc(names[p.src2])}</b> <span class=muted>${fmtT(C.timeOf(M, p))} · ${esc(p.chan)}${p.dst !== 'ffffffff' ? ' · to ' + esc(names[p.dst] || '!' + p.dst) : ''}</span><br>${esc(p.dec.text)}</div>`).join('') : '<p class=muted>No text messages on channels you have keys for.</p>';
  table($('#lPkts'), [
    { k: 'time', h: 'Time', v: p => C.timeOf(M, p) || p.up, f: p => timeCell(p) },
    { k: 'src2', h: 'From', f: p => esc(names[p.src2] || (p.src2 ? '!' + p.src2 : 'raw')) },
    { k: 'dst', h: 'To', f: p => p.dst === 'ffffffff' ? 'everyone' : esc(names[p.dst] || (p.dst ? '!' + p.dst : '')) },
    { k: 'rssi', h: 'dBm', n: 1 }, { k: 'snr', h: 'SNR', n: 1 }, { k: 'len', h: 'Bytes', n: 1 },
    { k: 'hops', h: 'Hops', n: 1, v: p => p.hopStart - p.hopLimit, f: p => p.hopStart != null && p.hopLimit != null ? (p.hopStart - p.hopLimit) : '' },
    { k: 'port', h: 'Type', v: p => p.dec ? p.dec.portName : '', f: p => p.dec ? esc(p.dec.portName) + (p.dec.text ? ': ' + esc(p.dec.text.slice(0, 40)) : '') : `<span class=muted>encrypted (${esc(p.chanHash)})</span>` }],
    M.lora, { sort: 'time', max: 800 });
}

// ---------- 900 MHz ----------
const MESH_US = [906.875, 913.125, 914.875, 918.875, 920.625, 905.312, 912.812];
function rRf() {
  const rf = C.rfSummary(S.M), hits = S.M.rf.filter(r => r.kind === 'hit');
  const stat = (k, v) => `<div class="card stat"><div class="k">${k}</div><div class="v">${v}</div></div>`;
  const floors = rf.sweeps.map(s => s.floor).filter(v => v != null);
  $('#rStats').innerHTML = stat('Hits', hits.length) + stat('Frequencies', rf.hits.length) + stat('Minute sweeps', rf.sweeps.length) +
    stat('Noise floor', floors.length ? (floors.reduce((a, b) => a + b, 0) / floors.length).toFixed(0) + ' dBm' : '—') +
    stat('Strongest', hits.length ? Math.max(...hits.map(h => h.rssi)).toFixed(0) + ' dBm' : '—');
  // waterfall: one row per minute sweep, newest at the bottom
  const [g, W, H] = canvas2d($('#rWater'));
  if (rf.sweeps.length) {
    const sw = rf.sweeps, n = sw[0].v.length, rowH = Math.max(1, (H - 18) / sw.length), colW = W / n;
    const lo = Math.min(...sw.map(s => s.floor ?? -125)) - 2, hi = lo + 45;
    const color = v => { const f = Math.max(0, Math.min(1, (v - lo) / (hi - lo))); const h = 220 - f * 200; return `hsl(${h},85%,${12 + f * 48}%)`; };
    sw.forEach((s, y) => s.v.forEach((v, x) => { g.fillStyle = color(v); g.fillRect(x * colW, y * rowH, colW + 0.5, rowH + 0.5); }));
    g.fillStyle = css('--mute');
    const f0 = sw[0].start, f1 = f0 + sw[0].step * (n - 1);
    for (let f = Math.ceil(f0 / 2) * 2; f <= f1; f += 2) { const x = (f - f0) / (f1 - f0) * W; g.fillText(f, x - 8, H - 4); }
    $('#rWaterInfo').textContent = `Each row is one minute (max signal), oldest at the top. Dark blue = quiet; green, yellow and red = stronger. ${fmtT(C.timeOf(S.M, sw[0].r))} → ${fmtT(C.timeOf(S.M, sw[sw.length - 1].r))}`;
  } else $('#rWaterInfo').textContent = 'No minute sweeps yet (the board logs one each minute).';
  // histogram over 0.25 MHz bins
  if (rf.hits.length) {
    const f0 = 902, step = 0.25, nb = Math.round((928 - f0) / step) + 1, bins = new Array(nb).fill(0);
    for (const h of rf.hits) { const i = Math.round((h.f - f0) / step); if (i >= 0 && i < nb) bins[i] += h.n; }
    const res = barChart($('#rHist'), bins.map((_, i) => (f0 + i * step) % 2 === 0 ? (f0 + i * step) : ''), bins, css('--rf'));
    if (res) { res.g.fillStyle = css('--lora'); for (const f of MESH_US) { const x = res.P.l + ((f - f0) / step + 0.5) * res.bw; res.g.fillRect(x - 1, res.H - res.P.b + 1, 2, 5); } }
  }
  table($('#rTop'), [{ k: 'f', h: 'MHz', n: 1, f: h => h.f.toFixed(3) }, { k: 'n', h: 'Hits', n: 1 }, { k: 'best', h: 'Strongest dBm', n: 1 },
    { k: 'note', h: '', v: h => MESH_US.some(m => Math.abs(m - h.f) < 0.13), f: h => MESH_US.some(m => Math.abs(m - h.f) < 0.13) ? '<span class=pill>Meshtastic preset</span>' : '' }],
    rf.hits, { sort: 'n', max: 60 });
}

// ---------- map ----------
function rMap() {
  if (!window.L) { $('#mapNote').textContent = 'The map needs an internet connection.'; return; }
  const M = S.M;
  if (!S.map) {
    S.map = L.map('map', { preferCanvas: true });
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 19, attribution: '© OpenStreetMap contributors' }).addTo(S.map);
    S.map.on('click', e => {
      if (!S.picking) return;
      S.picking = false; S.manual[$('#pinSess').value] = { lat: e.latlng.lat, lng: e.latlng.lng, lon: e.latlng.lng };
      $('#pinLat').value = e.latlng.lat.toFixed(6); $('#pinLon').value = e.latlng.lng.toFixed(6);
      $('#mapNote').textContent = 'Pin set for ' + $('#pinSess').value + '.'; rebuild().then(rMap);
    });
  }
  setTimeout(() => S.map.invalidateSize(), 50);
  if (S.layers) S.layers.forEach(l => S.map.removeLayer(l));
  const route = L.layerGroup(), wifi = L.layerGroup(), mesh = L.layerGroup(), fol = L.layerGroup(), rfl = L.layerGroup();
  S.layers = [route, wifi, mesh, fol, rfl];
  const bounds = [];
  // route per session, ordered by time
  for (const s of M.sessions) {
    const pts = [].concat(M.wifi, M.ble, M.lora, M.gps).filter(r => r.s === s.id && r.L && r.L.src !== 'manual' && r.L.src !== 'fixed').sort((a, b) => a.up - b.up);
    const ll = []; let last = null;
    for (const r of pts) if (!last || C.dist(last, r.L) > 8) { ll.push([r.L.lat, r.L.lon]); last = r.L; }
    if (ll.length > 1) { L.polyline(ll, { color: css('--accent'), weight: 4, opacity: .85 }).bindTooltip(s.id).addTo(route); bounds.push(...ll); }
    const est = Object.entries(S.wifiEst).filter(([k]) => k.startsWith(s.id + '|'));
    for (const [, e] of est) L.circle([e.lat, e.lon], { radius: e.acc, color: css('--accent'), weight: 1, fillOpacity: .05 }).addTo(route);
    const pin = S.manual[s.id] || (pts.length ? null : null);
    if (pin) { L.marker([pin.lat, pin.lon]).bindTooltip(s.id + ' (pin)').addTo(route); bounds.push([pin.lat, pin.lon]); }
  }
  for (const a of C.wifiSummary(M)) if (a.lat != null) {
    L.circleMarker([a.lat, a.lon], { radius: 4, color: css('--wifi'), weight: 1, fillOpacity: .7 })
      .bindPopup(`<b>${esc(a.ssid || 'hidden')}</b><br>${a.bssid}<br>ch ${a.ch} · ${esc(a.enc)} · best ${a.best} dBm`).addTo(wifi);
    bounds.push([a.lat, a.lon]);
  }
  for (const n of C.loraNodes(M)) if (n.lat != null) {
    L.circleMarker([n.lat, n.lon], { radius: 8, color: css('--lora'), weight: 2, fillOpacity: .5 })
      .bindPopup(`<b>${esc(n.longName || '!' + n.id)}</b><br>!${n.id} ${esc(n.hw || '')}<br>${n.pkts} packets · best ${n.best} dBm`).addTo(mesh);
    bounds.push([n.lat, n.lon]);
  }
  for (const d of C.bleSummary(M).filter(d => d.follower)) if (d.places.length > 1)
    L.polyline(d.places.map(p => [p.lat, p.lon]), { color: css('--bad'), weight: 3, dashArray: '6 6' }).bindTooltip(esc(d.name || d.kind || d.addr) + ' followed').addTo(fol);
  const strong = M.rf.filter(r => r.kind === 'hit' && r.L && r.rssi > r.floor + 25);
  for (const r of strong.slice(0, 2000)) L.circleMarker([r.L.lat, r.L.lon], { radius: 3, color: css('--rf'), weight: 0, fillOpacity: .6 }).bindTooltip(`${r.freq} MHz ${r.rssi} dBm`).addTo(rfl);
  const names = ['Route', 'Wi-Fi', 'Meshtastic', 'Followers', '900 MHz'];
  S.layerOn = S.layerOn || [true, true, true, true, false];
  S.layers.forEach((l, i) => S.layerOn[i] && l.addTo(S.map));
  $('#mapLayers').innerHTML = names.map((n, i) => `<label><input type=checkbox data-l="${i}" ${S.layerOn[i] ? 'checked' : ''}> ${n}</label>`).join(' &nbsp; ');
  $('#mapLayers').onchange = e => { const i = +e.target.dataset.l; S.layerOn[i] = e.target.checked; e.target.checked ? S.layers[i].addTo(S.map) : S.map.removeLayer(S.layers[i]); };
  if (S.goTo) { S.map.setView(S.goTo, 17); S.goTo = null; }
  else if (bounds.length) S.map.fitBounds(bounds, { padding: [30, 30], maxZoom: 17 });
  else S.map.setView([42.25, -84.4], 10);
  if (!S.picking) $('#mapNote').textContent = bounds.length ? '' : 'Nothing has a location yet. Add a phone track, Wi-Fi estimate or pin on the Get data tab.';
}
document.addEventListener('click', e => { const a = e.target.closest('a[data-go]'); if (!a) return; e.preventDefault(); S.goTo = a.dataset.go.split(',').map(Number); showTab('map'); });

// ---------- export ----------
function save(name, text, type) {
  const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([text], { type: type || 'text/csv' })); a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 4000);
}
$$('[data-x]').forEach(b => b.onclick = () => {
  const x = b.dataset.x, stamp = new Date().toISOString().slice(0, 10);
  if (x === 'wigle') save(`scout-wigle-${stamp}.csv`, C.wigleCsv(S.M));
  else if (x === 'geojson') save(`scout-${stamp}.geojson`, C.geojson(S.M), 'application/geo+json');
  else save(`scout-${x}-${stamp}.csv`, C.mergedCsv(S.M, x));
});

window.addEventListener('resize', () => { if (S.M && /overview|wifi|ble|rf/.test(S.tab)) renderTab(S.tab); });
})();
