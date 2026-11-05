// Scout Studio - data core (no page code here, so it can be tested on its own)
// Reads Heltec Scout logs, fills in times and locations, decodes Meshtastic packets on known channels.
(function (root) {
'use strict';
const KINDS = ['wifi', 'ble', 'lora', 'rf900', 'gps', 'events'];

// ---------- loading ----------
// A .scout bundle is "##SCOUT 1 s0042" then "##FILE wifi.csv 1234" sections.
function parseBundle(text, fallbackName) {
  const out = {};
  let sess = null, cur = null;
  for (const line of text.split('\n')) {
    if (line.startsWith('##SCOUT ')) { sess = line.split(' ')[2].trim(); out[sess] = out[sess] || {}; continue; }
    if (line.startsWith('##FILE ')) {
      const name = line.split(' ')[1].trim().split('/').pop();
      if (!sess) { sess = fallbackName || 'imported'; out[sess] = out[sess] || {}; }
      cur = name; out[sess][cur] = []; continue;
    }
    if (cur && line.length) out[sess][cur].push(line.replace(/\r$/, ''));
  }
  for (const s in out) for (const f in out[s]) out[s][f] = out[s][f].join('\n');
  return out;
}

function splitCsv(text) {
  const lines = text.split(/\r?\n/).filter(l => l.length);
  if (!lines.length) return [];
  const head = lines[0].split(',');
  const rows = [];
  for (let i = 1; i < lines.length; i++) {
    const c = lines[i].split(',');
    if (c.length < head.length - 1) continue;          // half-written last line after a power cut
    const o = {};
    for (let j = 0; j < head.length; j++) o[head[j]] = c[j] ?? '';
    if (head.length && c.length > head.length) o[head[head.length - 1]] = c.slice(head.length - 1).join(',');
    rows.push(o);
  }
  return rows;
}

const num = v => (v === '' || v == null) ? null : Number(v);

// files: { "wifi.csv": text, ... } per session
function buildModel(sessionsFiles) {
  const M = { sessions: [], wifi: [], ble: [], lora: [], rf: [], gps: [], events: [] };
  for (const sid of Object.keys(sessionsFiles).sort()) {
    const F = sessionsFiles[sid];
    const S = { id: sid, info: {}, offset: null, manualStart: null, counts: {} };
    if (F['session.txt']) for (const l of F['session.txt'].split('\n')) { const [k, v] = l.split('='); if (k && v) S.info[k.trim()] = v.trim(); }
    const add = (file, key, fn) => {
      if (!F[file]) return;
      const rows = splitCsv(F[file]);
      for (const r of rows) {
        const base = { s: sid, up: num(r.uptime_s) || 0, utc: num(r.utc), lat: num(r.lat), lon: num(r.lon), src: r.loc_src || '' };
        const o = fn ? fn(r, base) : null;
        if (o) M[key].push(Object.assign(base, o));
      }
      S.counts[key] = rows.length;
    };
    add('wifi.csv', 'wifi', r => ({ bssid: r.bssid, ssid: r.ssid, ch: num(r.channel), rssi: num(r.rssi), enc: r.enc }));
    add('ble.csv', 'ble', r => ({ addr: r.addr, atype: r.addr_type, name: r.name, rssi: num(r.rssi_max), hits: num(r.hits), mfg: r.mfg_id, mfgData: r.mfg_data, svc: r.svc_uuid, kind: r.kind }));
    add('lora.csv', 'lora', r => ({ freq: num(r.freq_mhz), rssi: num(r.rssi), snr: num(r.snr), len: num(r.len), dst: r.dest, src2: r.src, id: r.pkt_id,
      hopLimit: num(r.hop_limit), hopStart: num(r.hop_start), wantAck: r.want_ack === '1', viaMqtt: r.via_mqtt === '1', chanHash: r.chan_hash,
      nextHop: r.next_hop, relay: r.relay, hex: r.payload_hex }));
    add('rf900.csv', 'rf', r => ({ kind: r.kind, freq: num(r.freq_mhz), rssi: num(r.rssi), floor: num(r.floor), detail: r.detail }));
    add('gps.csv', 'gps', r => ({ sats: num(r.sats), hdop: num(r.hdop), alt: num(r.alt_m), speed: num(r.speed_kmh) }));
    add('events.csv', 'events', r => ({ ev: r.event, detail: r.detail }));
    // clock: any row with utc, or a time_set event, pins utc = offset + uptime for the whole session
    const anchors = [];
    for (const k of ['events', 'wifi', 'ble', 'lora', 'rf', 'gps']) for (const r of M[k]) if (r.s === sid && r.utc) { anchors.push(r.utc - r.up); if (anchors.length > 50) break; }
    for (const e of M.events) if (e.s === sid && e.ev === 'time_set') { const ep = Number(e.detail.split(' ')[0]); if (ep > 1.7e9) anchors.push(ep - e.up); }
    if (anchors.length) { anchors.sort((a, b) => a - b); S.offset = anchors[Math.floor(anchors.length / 2)]; }
    M.sessions.push(S);
  }
  return M;
}

function sessionOf(M, sid) { return M.sessions.find(s => s.s === sid || s.id === sid); }
// utc seconds for a row, or null when the board never learned the time in that session
function timeOf(M, r) {
  if (r.utc) return r.utc;
  const S = sessionOf(M, r.s);
  if (!S) return null;
  if (S.offset != null) return S.offset + r.up;
  if (S.manualStart != null) return S.manualStart + r.up;
  return null;
}

// ---------- location ----------
// Each row ends up with r.L = {lat, lon, src, acc}. Order of trust: board GPS > GPX track > Wi-Fi estimate > board fixed spot > manual pin.
function interp(track, t) {          // track sorted [{t,lat,lon}]
  if (!track.length || t == null) return null;
  let lo = 0, hi = track.length - 1;
  if (t < track[0].t - 120 || t > track[hi].t + 120) return null;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (track[m].t <= t) lo = m; else hi = m; }
  const a = track[lo], b = track[hi];
  if (t <= a.t) return a; if (t >= b.t) return b;
  if (b.t - a.t > 600) return (t - a.t < b.t - t) ? (t - a.t < 120 ? a : null) : (b.t - t < 120 ? b : null);
  const f = (t - a.t) / (b.t - a.t);
  return { lat: a.lat + (b.lat - a.lat) * f, lon: a.lon + (b.lon - a.lon) * f };
}

function parseTrack(text) {        // GPX, KML gx:Track, or CSV with time,lat,lon
  const pts = [];
  if (/<trkpt|<wpt/i.test(text)) {
    const re = /<(?:trkpt|wpt)[^>]*?lat="([-\d.]+)"[^>]*?lon="([-\d.]+)"[^>]*>([\s\S]*?)<\/(?:trkpt|wpt)>/gi;
    const re2 = /<(?:trkpt|wpt)[^>]*?lon="([-\d.]+)"[^>]*?lat="([-\d.]+)"[^>]*>([\s\S]*?)<\/(?:trkpt|wpt)>/gi;
    let m;
    while ((m = re.exec(text))) { const t = /<time>([^<]+)<\/time>/.exec(m[3]); if (t) pts.push({ t: Date.parse(t[1]) / 1000, lat: +m[1], lon: +m[2] }); }
    if (!pts.length) while ((m = re2.exec(text))) { const t = /<time>([^<]+)<\/time>/.exec(m[3]); if (t) pts.push({ t: Date.parse(t[1]) / 1000, lat: +m[2], lon: +m[1] }); }
  } else if (/<gx:coord>/.test(text)) {
    const w = [...text.matchAll(/<when>([^<]+)<\/when>/g)].map(x => Date.parse(x[1]) / 1000);
    const c = [...text.matchAll(/<gx:coord>([^<]+)<\/gx:coord>/g)].map(x => x[1].trim().split(/\s+/).map(Number));
    for (let i = 0; i < Math.min(w.length, c.length); i++) pts.push({ t: w[i], lon: c[i][0], lat: c[i][1] });
  } else {
    const rows = splitCsv(text.toLowerCase());
    for (const r of rows) {
      const t = r.time || r.timestamp || r.utc || r.date, lat = r.lat || r.latitude, lon = r.lon || r.lng || r.longitude;
      if (t && lat && lon) { const tt = /^\d+(\.\d+)?$/.test(t) ? (+t > 1e12 ? +t / 1000 : +t) : Date.parse(t) / 1000; pts.push({ t: tt, lat: +lat, lon: +lon }); }
    }
  }
  return pts.filter(p => isFinite(p.t) && isFinite(p.lat) && isFinite(p.lon)).sort((a, b) => a.t - b.t);
}

function allRows(M) { return [].concat(M.wifi, M.ble, M.lora, M.rf, M.gps, M.events); }

// Wi-Fi scans grouped by session + time, for the Wi-Fi location estimate
function wifiScans(M) {
  const g = new Map();
  for (const r of M.wifi) { const k = r.s + '|' + r.up; if (!g.has(k)) g.set(k, { s: r.s, up: r.up, aps: [] }); g.get(k).aps.push(r); }
  return [...g.values()].sort((a, b) => a.s < b.s ? -1 : a.s > b.s ? 1 : a.up - b.up);
}

function locate(M, opts) {
  opts = opts || {};
  const track = opts.track || [];
  const est = opts.wifiEst || {};            // "s|up" -> {lat,lon,acc}
  const estList = {};                          // per session sorted list of {up,lat,lon,acc}
  for (const k in est) { const [s, up] = k.split('|'); (estList[s] = estList[s] || []).push(Object.assign({ up: +up }, est[k])); }
  for (const s in estList) estList[s].sort((a, b) => a.up - b.up);
  const gpsList = {};
  for (const r of M.gps) if (r.lat != null && r.src.startsWith('gps')) (gpsList[r.s] = gpsList[r.s] || []).push({ t: r.up, lat: r.lat, lon: r.lon });
  const nearest = (list, up, win) => {
    if (!list || !list.length) return null;
    let lo = 0, hi = list.length - 1;
    while (hi - lo > 1) { const m = (lo + hi) >> 1; if (list[m].up <= up) lo = m; else hi = m; }
    const c = Math.abs(list[lo].up - up) <= Math.abs(list[hi].up - up) ? list[lo] : list[hi];
    return Math.abs(c.up - up) <= win ? c : null;
  };
  let n = { gps: 0, track: 0, wifi: 0, fixed: 0, manual: 0, none: 0 };
  for (const r of allRows(M)) {
    let L = null;
    if (r.lat != null && r.src === 'gps') L = { lat: r.lat, lon: r.lon, src: 'gps', acc: 10 };
    if (!L && gpsList[r.s]) { const p = interp(gpsList[r.s], r.up); if (p) L = { lat: p.lat, lon: p.lon, src: 'gps', acc: 25 }; }
    if (!L && track.length) { const t = timeOf(M, r); const p = interp(track, t); if (p) L = { lat: p.lat, lon: p.lon, src: 'track', acc: 20 }; }
    if (!L && estList[r.s]) { const e = nearest(estList[r.s], r.up, opts.estWindow || 90); if (e) L = { lat: e.lat, lon: e.lon, src: 'wifi-est', acc: e.acc || 100 }; }
    if (!L && r.lat != null && (r.src === 'fixed' || r.src === 'gps-last')) L = { lat: r.lat, lon: r.lon, src: r.src === 'fixed' ? 'fixed' : 'gps', acc: r.src === 'fixed' ? 50 : 200 };
    if (!L && opts.manual && opts.manual[r.s]) { const m = opts.manual[r.s]; L = { lat: m.lat, lon: m.lon, src: 'manual', acc: 100 }; }
    r.L = L;
    n[L ? (L.src === 'gps-last' ? 'gps' : L.src === 'track' ? 'track' : L.src) : 'none']++;
  }
  return n;
}

// MLS-style request body for one Wi-Fi scan (works with beaconDB and Google's geolocation API)
function geolocateBody(scan) {
  const aps = scan.aps
    .filter(a => !/^[0-9a-f]([26ae]):/i.test(a.bssid))      // skip randomized (phone hotspot) addresses
    .filter(a => !/_nomap$/i.test(a.ssid))
    .sort((a, b) => b.rssi - a.rssi).slice(0, 20)
    .map(a => ({ macAddress: a.bssid, signalStrength: a.rssi, channel: a.ch }));
  return aps.length >= 2 ? { considerIp: false, wifiAccessPoints: aps } : null;
}

// ---------- summaries ----------
function wifiSummary(M) {
  const by = new Map();
  for (const r of M.wifi) {
    let a = by.get(r.bssid);
    if (!a) by.set(r.bssid, a = { bssid: r.bssid, ssid: r.ssid, ch: r.ch, enc: r.enc, best: -999, n: 0, first: r, last: r, w: 0, la: 0, lo: 0, sessions: new Set() });
    a.n++; a.sessions.add(r.s);
    if (r.ssid) a.ssid = r.ssid;
    if (r.rssi > a.best) { a.best = r.rssi; a.ch = r.ch; }
    a.last = r;
    if (r.L) { const w = Math.pow(10, (r.rssi + 100) / 20); a.w += w; a.la += r.L.lat * w; a.lo += r.L.lon * w; }
  }
  return [...by.values()].map(a => Object.assign(a, a.w ? { lat: a.la / a.w, lon: a.lo / a.w } : {}, {
    random: /^[0-9a-f][26ae]:/i.test(a.bssid) })).sort((a, b) => b.best - a.best);
}

function dist(a, b) {            // meters
  const R = 6371000, r = Math.PI / 180, dl = (b.lat - a.lat) * r, dn = (b.lon - a.lon) * r;
  const h = Math.sin(dl / 2) ** 2 + Math.cos(a.lat * r) * Math.cos(b.lat * r) * Math.sin(dn / 2) ** 2;
  return 2 * R * Math.asin(Math.sqrt(h));
}

const TRACKER_KINDS = new Set(['FindMy', 'Tile', 'SmartTag', 'AirTag']);
function bleSummary(M) {
  const by = new Map();
  for (const r of M.ble) {
    let d = by.get(r.addr);
    if (!d) by.set(r.addr, d = { addr: r.addr, atype: r.atype, name: r.name, kind: r.kind, mfg: r.mfg, best: -999, windows: 0, hits: 0, first: r, last: r, places: [], sessions: new Set() });
    d.windows++; d.hits += r.hits || 0; d.sessions.add(r.s);
    if (r.name) d.name = r.name; if (r.kind) d.kind = r.kind;
    if (r.rssi > d.best) d.best = r.rssi;
    d.last = r;
    if (r.L && (!d.places.length || dist(d.places[d.places.length - 1], r.L) > 50)) d.places.push({ lat: r.L.lat, lon: r.L.lon });
  }
  const out = [...by.values()];
  for (const d of out) {
    const t0 = timeOf(M, d.first) ?? d.first.up, t1 = timeOf(M, d.last) ?? d.last.up;
    d.span = d.first.s === d.last.s || (timeOf(M, d.first) && timeOf(M, d.last)) ? Math.max(0, t1 - t0) : 0;
    let far = 0; for (const p of d.places) far = Math.max(far, dist(d.places[0], p));
    d.travelled = far;
    // something that keeps showing up near you while you move around
    d.follower = (d.span > 20 * 60 && d.windows >= 6 && d.travelled > 400) || (TRACKER_KINDS.has(d.kind) && d.span > 30 * 60 && d.travelled > 250);
  }
  return out.sort((a, b) => (b.follower - a.follower) || (b.windows - a.windows));
}

// ---------- Meshtastic ----------
const DEFAULT_KEY = [0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01];
const PORTS = { 0: 'Unknown', 1: 'Text', 2: 'Remote HW', 3: 'Position', 4: 'Node info', 5: 'Routing', 6: 'Admin', 7: 'Text (compressed)', 8: 'Waypoint',
  9: 'Audio', 10: 'Detection', 11: 'Alert', 32: 'Reply', 33: 'IP tunnel', 34: 'Paxcounter', 64: 'Serial', 65: 'Store & forward', 66: 'Range test',
  67: 'Telemetry', 68: 'Zps', 69: 'Simulator', 70: 'Traceroute', 71: 'Neighbor info', 72: 'ATAK', 73: 'Map report', 74: 'Power stress', 256: 'Private', 257: 'ATAK forwarder' };
const HW = { 0: 'Unset', 4: 'T-Beam', 9: 'RAK4631', 10: 'Heltec V2.1', 11: 'Heltec V1', 25: 'Station G1', 39: 'Heltec WSL V3', 43: 'Heltec V3', 44: 'Heltec WSL V3',
  47: 'RPi Pico', 48: 'Heltec Tracker', 49: 'Heltec Paper', 50: 'T-Deck', 51: 'T-Watch S3', 58: 'Heltec HT62', 64: 'Heltec Capsule', 65: 'Heltec Vision Master T190',
  66: 'Heltec Vision Master E213', 67: 'Heltec Vision Master E290', 68: 'Heltec Mesh Node T114', 71: 'Tracker T1000-E', 110: 'Heltec V4' };

function hexBytes(h) { const b = new Uint8Array(h.length >> 1); for (let i = 0; i < b.length; i++) b[i] = parseInt(h.substr(i * 2, 2), 16); return b; }
function keyFromPsk(b64) {
  if (!b64) return null;
  const raw = Uint8Array.from(atob(b64), c => c.charCodeAt(0));
  if (raw.length === 1) { if (raw[0] === 0) return null; const k = DEFAULT_KEY.slice(); k[15] = (k[15] + raw[0] - 1) & 0xff; return new Uint8Array(k); }
  return raw;
}
function channelHash(name, key) { let h = 0; for (const c of new TextEncoder().encode(name)) h ^= c; for (const b of key) h ^= b; return h & 0xff; }

async function aesCtr(key, nonce, data) {
  const subtle = (root.crypto || globalThis.crypto).subtle;
  const k = await subtle.importKey('raw', key, 'AES-CTR', false, ['decrypt']);
  return new Uint8Array(await subtle.decrypt({ name: 'AES-CTR', counter: nonce, length: 32 }, k, data));
}

function readPb(buf) {          // tiny protobuf reader: {field: [values]}
  const f = {}; let i = 0;
  const varint = () => { let r = 0, s = 0, b; do { if (i >= buf.length) throw 0; b = buf[i++]; r += (b & 0x7f) * 2 ** s; s += 7; } while (b & 0x80 && s < 64); return r; };
  while (i < buf.length) {
    const tag = varint(), fn = Math.floor(tag / 8), wt = tag & 7;
    if (fn === 0) throw 0;
    let v;
    if (wt === 0) v = varint();
    else if (wt === 2) { const l = varint(); if (i + l > buf.length) throw 0; v = buf.subarray(i, i + l); i += l; }
    else if (wt === 5) { if (i + 4 > buf.length) throw 0; v = new DataView(buf.buffer, buf.byteOffset + i, 4); i += 4; }
    else if (wt === 1) { if (i + 8 > buf.length) throw 0; v = new DataView(buf.buffer, buf.byteOffset + i, 8); i += 8; }
    else throw 0;
    (f[fn] = f[fn] || []).push({ wt, v });
  }
  return f;
}
const str = v => new TextDecoder().decode(v);

function decodeData(plain) {
  const d = readPb(plain);
  if (!d[1] || d[1][0].wt !== 0) return null;
  const port = d[1][0].v, payload = d[2] ? d[2][0].v : new Uint8Array(0);
  const out = { port, portName: PORTS[port] || ('Port ' + port) };
  try {
    if (port === 1) out.text = str(payload);
    else if (port === 3) {
      const p = readPb(payload);
      if (p[1] && p[2]) { out.lat = p[1][0].v.getInt32(0, true) / 1e7; out.lon = p[2][0].v.getInt32(0, true) / 1e7; }
      if (p[3]) out.alt = p[3][0].v | 0;
    } else if (port === 4) {
      const u = readPb(payload);
      if (u[1]) out.userId = str(u[1][0].v);
      if (u[2]) out.longName = str(u[2][0].v);
      if (u[3]) out.shortName = str(u[3][0].v);
      if (u[5]) out.hw = HW[u[5][0].v] || ('hw ' + u[5][0].v);
    } else if (port === 67) {
      const t = readPb(payload);
      if (t[2]) { const dm = readPb(t[2][0].v); if (dm[1]) out.battery = dm[1][0].v; if (dm[2]) out.voltage = +dm[2][0].v.getFloat32(0, true).toFixed(2); }
    } else if (port === 73) {
      const m = readPb(payload);
      if (m[1]) out.longName = str(m[1][0].v); if (m[2]) out.shortName = str(m[2][0].v);
      if (m[9] && m[10]) { out.lat = m[9][0].v.getInt32(0, true) / 1e7; out.lon = m[10][0].v.getInt32(0, true) / 1e7; }
    }
  } catch (e) { /* partial payload */ }
  return out;
}

// channels: [{name, psk(base64)}]; LongFast with the public key is always tried
async function decodeLora(M, channels) {
  const keys = [{ name: 'LongFast', key: new Uint8Array(DEFAULT_KEY) }];
  for (const c of channels || []) { try { const k = keyFromPsk(c.psk); if (k && (k.length === 16 || k.length === 32)) keys.push({ name: c.name, key: k }); } catch (e) {} }
  for (const k of keys) k.hash = channelHash(k.name, k.key);
  let ok = 0;
  for (const p of M.lora) {
    p.dec = null; p.chan = null;
    if (!p.hex || p.hex.length < 34 || p.src2 === '') continue;
    const bytes = hexBytes(p.hex), hash = parseInt(p.chanHash, 16);
    const enc = bytes.subarray(16);
    const nonce = new Uint8Array(16);
    const id = parseInt(p.id, 16) >>> 0, from = parseInt(p.src2, 16) >>> 0;
    new DataView(nonce.buffer).setUint32(0, id, true);
    new DataView(nonce.buffer).setUint32(8, from, true);
    for (const k of keys) {
      if (k.hash !== hash) continue;
      try {
        if (k.key.length === 32) continue;   // AES-256 PSKs: WebCrypto handles them too, but keep CTR at 128 for now
        const plain = await aesCtr(k.key, nonce, enc);
        const d = decodeData(plain);
        if (d) { p.dec = d; p.chan = k.name; ok++; break; }
      } catch (e) { /* wrong key */ }
    }
  }
  return ok;
}

function loraNodes(M) {
  const by = new Map();
  const node = id => { let n = by.get(id); if (!n) by.set(id, n = { id, pkts: 0, best: -999, bestSnr: -99, first: null, last: null, ports: {}, direct: 0 }); return n; };
  for (const p of M.lora) {
    if (!p.src2) continue;
    const n = node(p.src2);
    n.pkts++; if (p.rssi > n.best) n.best = p.rssi; if (p.snr > n.bestSnr) n.bestSnr = p.snr;
    n.first = n.first || p; n.last = p;
    if (p.hopStart != null && p.hopLimit != null && p.hopStart === p.hopLimit) n.direct++;
    if (p.dec) {
      n.ports[p.dec.portName] = (n.ports[p.dec.portName] || 0) + 1;
      if (p.dec.longName) n.longName = p.dec.longName;
      if (p.dec.shortName) n.shortName = p.dec.shortName;
      if (p.dec.hw) n.hw = p.dec.hw;
      if (p.dec.lat != null && Math.abs(p.dec.lat) > 0.0001) { n.lat = p.dec.lat; n.lon = p.dec.lon; }
      if (p.dec.battery != null) n.battery = p.dec.battery;
    }
  }
  return [...by.values()].sort((a, b) => b.pkts - a.pkts);
}

// ---------- 900 MHz ----------
function rfSummary(M) {
  const sweeps = [], hits = new Map();
  for (const r of M.rf) {
    if (r.kind === 'sweep' && r.detail) {
      const [a, s, vals] = r.detail.split(':');
      sweeps.push({ r, start: +a, step: +s, v: vals.split(';').map(Number), floor: r.floor });
    } else if (r.kind === 'hit') {
      const f = Math.round(r.freq * 1000) / 1000;
      const h = hits.get(f) || { f, n: 0, best: -999 };
      h.n++; if (r.rssi > h.best) h.best = r.rssi;
      hits.set(f, h);
    }
  }
  return { sweeps, hits: [...hits.values()].sort((a, b) => a.f - b.f) };
}

// ---------- exports ----------
function iso(t) { return t ? new Date(t * 1000).toISOString().replace('.000', '') : ''; }
function wigleCsv(M) {
  const lines = ['WigleWifi-1.4,appRelease=HeltecScout,model=Heltec,release=1,device=HeltecScout,display=,board=ESP32-S3,brand=Heltec',
    'MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type'];
  const auth = e => ({ open: '[ESS]', wep: '[WEP][ESS]', wpa: '[WPA-PSK-CCMP][ESS]', wpa2: '[WPA2-PSK-CCMP][ESS]', 'wpa/wpa2': '[WPA-PSK-CCMP][WPA2-PSK-CCMP][ESS]',
    'wpa2-ent': '[WPA2-EAP-CCMP][ESS]', wpa3: '[WPA3-SAE-CCMP][ESS]', 'wpa2/wpa3': '[WPA2-PSK-CCMP][WPA3-SAE-CCMP][ESS]' }[e] || '[ESS]');
  for (const r of M.wifi) {
    const t = timeOf(M, r);
    if (!r.L || !t) continue;
    const ts = new Date(t * 1000).toISOString().replace('T', ' ').slice(0, 19);
    lines.push([r.bssid, '"' + (r.ssid || '').replace(/"/g, '') + '"', auth(r.enc), ts, r.ch, r.rssi, r.L.lat.toFixed(7), r.L.lon.toFixed(7), 0, r.L.acc, 'WIFI'].join(','));
  }
  for (const r of M.ble) {
    const t = timeOf(M, r);
    if (!r.L || !t) continue;
    const ts = new Date(t * 1000).toISOString().replace('T', ' ').slice(0, 19);
    lines.push([r.addr, '"' + (r.name || '').replace(/"/g, '') + '"', 'Misc [LE]', ts, 0, r.rssi, r.L.lat.toFixed(7), r.L.lon.toFixed(7), 0, r.L.acc, 'BLE'].join(','));
  }
  return lines.join('\n');
}

function mergedCsv(M, key) {
  const rows = M[key]; if (!rows.length) return '';
  const skip = new Set(['L', 'dec', 'chan', 'utc', 'lat', 'lon', 'src']);
  const cols = Object.keys(rows[0]).filter(k => !skip.has(k));
  const out = [['time_utc', 'lat', 'lon', 'loc_src', 'loc_acc_m'].concat(cols, key === 'lora' ? ['channel', 'port', 'decoded'] : []).join(',')];
  for (const r of rows) {
    const t = timeOf(M, r);
    const extra = key === 'lora' ? [r.chan || '', r.dec ? r.dec.portName : '', r.dec ? JSON.stringify(Object.assign({}, r.dec, { port: undefined, portName: undefined })).replace(/,/g, ';') : ''] : [];
    out.push([iso(t), r.L ? r.L.lat.toFixed(6) : '', r.L ? r.L.lon.toFixed(6) : '', r.L ? r.L.src : '', r.L ? r.L.acc : ''].concat(cols.map(c => r[c] ?? ''), extra).join(','));
  }
  return out.join('\n');
}

function geojson(M) {
  const feats = [];
  const pt = (lat, lon, props) => feats.push({ type: 'Feature', geometry: { type: 'Point', coordinates: [+lon.toFixed(7), +lat.toFixed(7)] }, properties: props });
  for (const a of wifiSummary(M)) if (a.lat != null) pt(a.lat, a.lon, { type: 'wifi', ssid: a.ssid, bssid: a.bssid, channel: a.ch, enc: a.enc, best_rssi: a.best });
  for (const n of loraNodes(M)) if (n.lat != null) pt(n.lat, n.lon, { type: 'meshtastic', id: '!' + n.id, name: n.longName || '', packets: n.pkts });
  const path = allRows(M).filter(r => r.L && r.L.src !== 'manual').map(r => ({ t: timeOf(M, r) ?? r.up, r })).sort((a, b) => a.t - b.t);
  if (path.length > 1) feats.push({ type: 'Feature', geometry: { type: 'LineString', coordinates: path.map(p => [p.r.L.lon, p.r.L.lat]) }, properties: { type: 'route' } });
  return JSON.stringify({ type: 'FeatureCollection', features: feats });
}

root.ScoutCore = { KINDS, parseBundle, splitCsv, buildModel, timeOf, parseTrack, interp, wifiScans, locate, geolocateBody, wifiSummary, bleSummary,
  decodeLora, loraNodes, rfSummary, wigleCsv, mergedCsv, geojson, iso, dist, channelHash, keyFromPsk, PORTS, DEFAULT_KEY };
})(typeof window !== 'undefined' ? window : globalThis);
