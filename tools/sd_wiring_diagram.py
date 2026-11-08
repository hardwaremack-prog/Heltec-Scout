# SD wiring diagram, CS 4 / MISO 6 / CLK 47 / MOSI 48 wiring (Shelf Lite, The Shelf V4, Heltec MIDI, Heltec Scout)
import sys
TITLE=sys.argv[1] if len(sys.argv)>1 else 'The Shelf on Heltec V4'
SUB=sys.argv[2] if len(sys.argv)>2 else 'Same wiring as Heltec Shelf Lite · works on the V4 and V4-R8'
BOARD=sys.argv[3] if len(sys.argv)>3 else 'Heltec WiFi LoRa 32 V4'
OUT=sys.argv[4] if len(sys.argv)>4 else 'sd-wiring-shelf-v4.svg'
J3=['GND','3V3','3V3','37','46','45','42','41','40','39','38','1','2','3','4','5','6','7']
J2=['GND','5V','Ve','Ve','44','43','RST','0','36','35','34','33','47','48','26','21','20','19']
# (module label, header, index, colour)
SD=[('3v3','J3',1,'#ff5a5a'),('CS','J3',14,'#c77dff'),('MOSI','J2',13,'#4cd964'),('CLK','J2',12,'#3aa0ff'),('MISO','J3',16,'#ffa53a'),('GND','J3',0,'#9aa4ae')]
W,H=1200,1010
bx,by,bw,bh=170,430,860,330
px0=bx+70; pstep=(bw-140)/17
pinx=lambda i: px0+i*pstep
topy=by+28; boty=by+bh-28
sx,sy,sw,sh=360,60,480,170
sdx=lambda k: sx+70+k*68
o=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI,Roboto,Helvetica,Arial,sans-serif">',f'<rect width="{W}" height="{H}" fill="#0b0f14"/>',
 f'<text x="40" y="44" fill="#fff" font-size="28" font-weight="700">microSD card → {TITLE}</text>',
 f'<text x="40" y="74" fill="#7f93a8" font-size="16">{SUB}</text>']
o.append(f'<rect x="{sx}" y="{sy+40}" width="{sw}" height="{sh}" rx="14" fill="#1d4f8f" stroke="#3a78c2" stroke-width="3"/>')
o.append(f'<text x="{sx+16}" y="{sy+66}" fill="#a9c7ee" font-size="14">3.3 V SPI module (pin order as printed on it)</text>')
o.append(f'<rect x="{sx+120}" y="{sy+70}" width="240" height="110" rx="8" fill="#c9ced6" stroke="#8e96a2" stroke-width="3"/>')
o.append(f'<rect x="{sx+150}" y="{sy+90}" width="180" height="70" rx="4" fill="#2a2f36"/>')
o.append(f'<text x="{sx+240}" y="{sy+132}" fill="#e6edf5" font-size="18" font-weight="700" text-anchor="middle">microSD</text>')
for k,(lab,_,_,_) in enumerate(SD):
    x=sdx(k); y=sy+sh+40
    o.append(f'<rect x="{x-9}" y="{y-6}" width="18" height="30" rx="{0 if k==0 else 3}" fill="#d4af37" stroke="{"#fff" if k==0 else "#8a6d1d"}" stroke-width="{2 if k==0 else 1}"/>')
    o.append(f'<text x="{x}" y="{y-14}" fill="#fff" font-size="15" font-weight="700" text-anchor="middle">{lab}</text>')
o.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="18" fill="#141a22" stroke="#3a4a5c" stroke-width="3"/>')
o.append(f'<rect x="{bx-34}" y="{by+bh/2-38}" width="46" height="76" rx="10" fill="#9aa4ae" stroke="#5f6b78" stroke-width="3"/>')
o.append(f'<text x="{bx-11}" y="{by+bh/2+60}" fill="#7f93a8" font-size="14" text-anchor="middle">USB-C</text>')
o.append(f'<rect x="{bx+250}" y="{by+95}" width="300" height="150" rx="6" fill="#05070a" stroke="#2b3a4b" stroke-width="3"/>')
o.append(f'<text x="{bx+400}" y="{by+165}" fill="#7fe3ff" font-size="20" font-weight="700" text-anchor="middle">OLED</text>')
o.append(f'<text x="{bx+400}" y="{by+192}" fill="#7f93a8" font-size="15" text-anchor="middle">{BOARD}</text>')
o.append(f'<text x="{bx+30}" y="{topy+46}" fill="#7f93a8" font-size="13">J3</text>')
o.append(f'<text x="{bx+30}" y="{boty-34}" fill="#7f93a8" font-size="13">J2</text>')
used={(h,i):c for _,h,i,c in SD}
for hdr,names,y,ty in [('J3',J3,topy,topy+32),('J2',J2,boty,boty-18)]:
    for i,name in enumerate(names):
        x=pinx(i); c=used.get((hdr,i))
        o.append(f'<circle cx="{x}" cy="{y}" r="{11 if c else 8}" fill="{c or "#0b0f14"}" stroke="{c or "#6b7d90"}" stroke-width="3"/>')
        o.append(f'<text x="{x}" y="{ty}" fill="{"#fff" if c else "#6b7d90"}" font-size="{14 if c else 12}" font-weight="{700 if c else 400}" text-anchor="middle">{name}</text>')
off=0
for k,(lab,hdr,idx,col) in enumerate(SD):
    x1=sdx(k); y1=sy+sh+64; x2=pinx(idx)
    if hdr=='J3':
        y2=topy-12
        o.append(f'<path d="M{x1},{y1} C{x1},{y1+90} {x2},{y2-110} {x2},{y2}" fill="none" stroke="{col}" stroke-width="6" stroke-linecap="round"/>')
    else:   # around the right edge and up into the J2 row from below
        rx=bx+bw+40+off*26; ly=by+bh+40+off*26; mid=y1+40+off*14
        o.append(f'<path d="M{x1},{y1} L{x1},{mid} L{rx},{mid} L{rx},{ly} L{x2},{ly} L{x2},{boty+12}" fill="none" stroke="{col}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>')
        off+=1
ly=by+bh+130
x=40
for lab,hdr,idx,col in SD:
    pin=(J3 if hdr=='J3' else J2)[idx]; t=f'{lab} → {("GPIO "+pin) if pin.isdigit() else pin}'
    o.append(f'<rect x="{x}" y="{ly-14}" width="22" height="8" rx="4" fill="{col}"/>')
    o.append(f'<text x="{x+30}" y="{ly-6}" fill="#d7e3ee" font-size="16" font-weight="600">{t}</text>')
    x+=30+len(t)*8.8+26
for n,l in enumerate(['CS and MISO go to the J3 row (GPIO 4, 6). CLK and MOSI go to the J2 row on the other edge (GPIO 47, 48).',
                      'Module pins in silkscreen order: 3v3 · CS · MOSI · CLK · MISO · GND (square pad = 3v3). CLK is the SPI clock (SCK).']):
    o.append(f'<text x="40" y="{ly+28+n*20}" fill="#7f93a8" font-size="14">{l}</text>')
o.append('</svg>')
open(OUT,'w').write('\n'.join(o))
