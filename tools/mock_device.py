#!/usr/bin/env python3
"""Serves web/device.html with fake /api answers so the board's page can be tried on a PC: python3 tools/mock_device.py, then open http://localhost:8080"""
import http.server, json, random, time, pathlib, urllib.parse
ROOT = pathlib.Path(__file__).resolve().parent.parent
T0 = time.time()
def status():
    now=[int(-121+random.gauss(0,2)+(28 if random.random()<.05 else 0)) for _ in range(105)]
    return {"board":"Heltec V3","fw":"1.0.0","session":7,"dir":"/scout/s0007","store":"microSD","total":31_900_000_000,"used":2_400_000_000,
      "uptime":int(time.time()-T0)+600,"utc":int(time.time()),"timeSrc":"phone","batt":3.98,"ap":"Scout-4F2A","clients":1,"sta":"0.0.0.0",
      "loc":{"ok":False,"lat":0,"lon":0,"src":"","sats":0,"hdop":0,"gpsSeen":False},
      "counts":{"wifi":812,"ble":240,"lora":31,"rf":512,"gps":0,"wifiNow":17,"wifiUnique":44,"bleNow":12,"bleUnique":61,"loraPkts":31,"loraNodes":3,"loraBad":2,"rfHits":512,"sweeps":204},
      "wifi":[{"ssid":n,"bssid":"a4:2b:b0:%02x:11:22"%i,"rssi":-45-i*4,"ch":[1,6,11][i%3],"open":n=="xfinitywifi"} for i,n in enumerate(["NETGEAR42","Spectrum-5G","xfinitywifi","","Smith Family"])],
      "ble":[{"addr":"7c:aa:11:22:33:4%d"%i,"name":n,"kind":k,"rssi":-50-i*6,"hits":9-i} for i,(n,k) in enumerate([("","FindMy"),("Living Room TV","Samsung"),("Galaxy Buds","Samsung"),("","Microsoft")])],
      "lora":[{"src":"a1b2c3d4","dst":"ffffffff","rssi":-104.5,"snr":3.2,"len":55,"ago":12},{"src":"0badcafe","dst":"ffffffff","rssi":-112,"snr":-6.5,"len":33,"ago":80}],
      "rf":{"start":902.0,"step":0.25,"floor":-121.5,"busiest":914.5,"now":now,"peak":[v+random.randint(0,8) for v in now]},
      "cfg":{"apName":"Scout-4F2A","staSsid":"","scrollSec":6,"sleepMin":0,"loraFreq":906.875,"loraBw":250,"loraSf":11,"loraCr":5,"loraSync":43,
        "sweepStart":902,"sweepStop":928,"sweepStep":0.25,"sweepThresh":12,"wifiEvery":20,"gpsOn":True,"gpsRx":39,"gpsTx":38,"gpsBaud":9600,"fixedLat":0,"fixedLon":0}}
class H(http.server.BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = urllib.parse.parse_qs(u.query)
        if u.path == "/": return self._send(200, (ROOT/"web"/"device.html").read_bytes(), "text/html")
        if u.path == "/api/status": return self._send(200, json.dumps(status()))
        if u.path == "/api/ls":
            d = q.get("dir", ["/"])[0]
            if d == "/scout": return self._send(200, json.dumps([{"n":"s%04d"%i,"s":0,"d":True} for i in range(1,8)]))
            return self._send(200, json.dumps([{"n":"scout","s":0,"d":True},{"n":"Music","s":0,"d":True},{"n":"notes.txt","s":1834,"d":False},{"n":"photo.jpg","s":2_400_000,"d":False}]))
        self._send(404, "")
    def do_POST(self): self._send(200, "ok", "text/plain")
    def log_message(self, *a): pass
if __name__ == "__main__":
    print("Mock Scout at http://localhost:8080"); http.server.ThreadingHTTPServer(("", 8080), H).serve_forever()
