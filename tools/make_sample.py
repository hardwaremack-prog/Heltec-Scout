#!/usr/bin/env python3
"""Makes a realistic fake session (sample.scout) for testing Scout Studio without a board."""
import random, struct, math, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
random.seed(4)
KEY = bytes([0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01])
def vint(n):
    o=b''
    while True:
        b=n&0x7f; n>>=7
        o+=bytes([b|(0x80 if n else 0)])
        if not n: return o
def fld(num,wt,val):
    t=vint(num<<3|wt)
    if wt==0: return t+vint(val)
    if wt==2: return t+vint(len(val))+val
    if wt==5: return t+val
def mesh(src,dst,pid,port,payload,hops=3,start=3):
    data=fld(1,0,port)+fld(2,2,payload)
    nonce=struct.pack('<QII',pid,src,0)
    enc=Cipher(algorithms.AES(KEY),modes.CTR(nonce)).encryptor().update(data)
    flags=(hops&7)|((start&7)<<5)
    hdr=struct.pack('<III',dst,src,pid)+bytes([flags,0x08,0,0])
    return hdr+enc
base_lat, base_lon = 42.2459, -84.4013     # somewhere near Jackson, MI
T0 = 1793826000
out = ["##SCOUT 1 s0007"]
files = {k: [] for k in ['wifi','ble','lora','rf900','gps','events']}
files['wifi'].append("utc,uptime_s,lat,lon,loc_src,bssid,ssid,channel,rssi,enc")
files['ble'].append("utc,uptime_s,lat,lon,loc_src,addr,addr_type,name,rssi_max,hits,mfg_id,mfg_data,svc_uuid,kind")
files['lora'].append("utc,uptime_s,lat,lon,loc_src,freq_mhz,rssi,snr,len,dest,src,pkt_id,hop_limit,hop_start,want_ack,via_mqtt,chan_hash,next_hop,relay,payload_hex")
files['rf900'].append("utc,uptime_s,lat,lon,loc_src,kind,freq_mhz,rssi,floor,detail")
files['gps'].append("utc,uptime_s,lat,lon,loc_src,sats,hdop,alt_m,speed_kmh,course")
files['events'].append("utc,uptime_s,lat,lon,loc_src,event,detail")
files['events'].append(",1,,,,boot,Heltec V3 fw 1.0.0 reset=power_on_or_rst_button")
aps = [("a4:2b:b0:%02x:%02x:%02x"%(i,i*3%256,i*7%256), random.choice(["NETGEAR42","Spectrum-5G","xfinitywifi","Smith Family","DIRECT-HP","",
       "TP-Link_88","Consumers Energy Guest","ATT9a8b","Pixel_77"]), random.choice([1,6,11,3,9]), random.choice(["wpa2","wpa2","open","wpa2/wpa3","wpa3"]),
       base_lat+random.uniform(-0.01,0.01), base_lon+random.uniform(-0.015,0.015)) for i in range(40)]
def pos(up):  # a drive: ~1.5 km east over the session
    return base_lat + 0.002*math.sin(up/900), base_lon + up/2400*0.018
clock_at = 300
for up in range(5, 2400, 20):
    lat, lon = pos(up)
    utc = str(T0+up) if up >= clock_at else ""
    for a in aps:
        d = math.hypot((a[4]-lat)*111000, (a[5]-lon)*82000)
        rssi = int(-40 - 22*math.log10(max(d,5)/5) + random.gauss(0,3))
        if rssi > -92: files['wifi'].append(f"{utc},{up},,,,{a[0]},{a[1]},{a[2]},{rssi},{a[3]}")
for up in range(15, 2400, 15):
    utc = str(T0+up) if up >= clock_at else ""
    files['ble'].append(f"{utc},{up},,,,7c:aa:11:22:33:44,random,,-{random.randint(48,60)},{random.randint(3,9)},004c,1219a1b2c3,,FindMy")   # follows us
    if up < 600: files['ble'].append(f"{utc},{up},,,,d0:01:02:03:04:05,public,Living Room TV,-{random.randint(60,75)},4,0075,0142,,Samsung")
    if random.random()<0.3: files['ble'].append(f"{utc},{up},,,,{random.randint(0x40,0x7f):02x}:{random.randint(0,255):02x}:9a:bc:de:f0,random,,-{random.randint(70,95)},1,0006,0109,,Microsoft")
nodes = {0xa1b2c3d4:("Mack Base","MKB",43,42.249,-84.402),0x0badcafe:("Jackson Hill","JH",110,42.2601,-84.3803)}
pid = 1000
for up in range(40, 2400, 95):
    utc = str(T0+up) if up >= clock_at else ""
    for nid,(ln,sn,hw,la,lo) in nodes.items():
        pid += 7
        if random.random()<0.5:
            user=fld(1,2,("!%08x"%nid).encode())+fld(2,2,ln.encode())+fld(3,2,sn.encode())+fld(5,0,hw)
            pkt=mesh(nid,0xffffffff,pid,4,user)
        else:
            p=fld(1,5,struct.pack('<i',int(la*1e7)))+fld(2,5,struct.pack('<i',int(lo*1e7)))+fld(3,0,280)
            pkt=mesh(nid,0xffffffff,pid,3,p,hops=2)
        if pid % 5 == 0: pkt = mesh(nid,0xffffffff,pid,1,b"Anyone on the Jackson mesh tonight?")
        fl=pkt[12]
        files['lora'].append(f"{utc},{up},,,,906.875,{random.uniform(-118,-95):.1f},{random.uniform(-12,6):.2f},{len(pkt)},ffffffff,{nid:08x},{pid:08x},{fl&7},{(fl>>5)&7},0,0,08,00,00,{pkt.hex()}")
for up in range(3, 2400, 3):
    utc = str(T0+up) if up >= clock_at else ""
    for f in [random.uniform(902.3,927.7) for _ in range(random.randint(0,2))]:   # meter-style hopping
        files['rf900'].append(f"{utc},{up},,,,hit,{round(f*4)/4:.3f},{random.uniform(-100,-80):.1f},-121.5,")
    if up % 60 < 3:
        vals = ";".join(str(int(-121+random.gauss(0,1.5)+(25 if random.random()<0.06 else 0))) for _ in range(105))
        files['rf900'].append(f"{utc},{up},,,,sweep,,,-121.5,902.000:0.250:{vals}")
files['events'].append(f"{T0+clock_at},{clock_at},,,,time_set,{T0+clock_at} phone")
out.append("##FILE session.txt 60\nsession=7\nboard=Heltec V3\nfirmware=1.0.0\nmac=aa:bb:cc:dd:ee:ff\nstorage=microSD\n")
for k,v in files.items():
    body="\n".join(v)+"\n"
    out.append(f"##FILE {k}.csv {len(body)}\n{body}")
open(sys.argv[1] if len(sys.argv)>1 else "sample.scout","w").write("\n".join(out))
# a GPX track from a phone covering part of the drive (to test track matching)
g=['<?xml version="1.0"?><gpx version="1.1" creator="test"><trk><trkseg>']
for up in range(300, 1500, 10):
    la,lo=pos(up); g.append(f'<trkpt lat="{la:.6f}" lon="{lo:.6f}"><time>{__import__("datetime").datetime.utcfromtimestamp(T0+up).isoformat()}Z</time></trkpt>')
g.append('</trkseg></trk></gpx>')
open("sample-track.gpx","w").write("\n".join(g))
print("ok")
