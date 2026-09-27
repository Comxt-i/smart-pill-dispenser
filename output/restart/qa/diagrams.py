from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
import math
Q=Path('output/restart/qa');F='/System/Library/Fonts/Supplemental/Arial.ttf'
def sheet(w,h):
 im=Image.new('RGB',(w,h),'white');return im,ImageDraw.Draw(im)
def box(dr,r,t,size=30):
 f=ImageFont.truetype(F,size)
 while dr.multiline_textbbox((0,0),t,font=f,spacing=8)[2]>r[2]-r[0]-28:
  size-=1;f=ImageFont.truetype(F,size)
 b=dr.multiline_textbbox((0,0),t,font=f,spacing=8,align='center');dr.rounded_rectangle(r,radius=10,outline='#333333',width=3,fill='#f3f5f7');dr.multiline_text(((r[0]+r[2]-b[2])/2,(r[1]+r[3]-(b[3]-b[1]))/2-b[1]),t,font=f,fill='black',spacing=8,align='center')
def arr(d,a,b):
 d.line((a,b),fill='#333333',width=3);an=math.atan2(b[1]-a[1],b[0]-a[0]);d.polygon([b,(b[0]-16*math.cos(an-.5),b[1]-16*math.sin(an-.5)),(b[0]-16*math.cos(an+.5),b[1]-16*math.sin(an+.5))],fill='#333333')
def save(im,n):im.save(Q/(n+'.png'))
im,d=sheet(1500,690)
for r,t in [((30,30,330,150),'User / caregiver\nPhone browser'),((410,30,710,150),'React website\nAccount + schedules'),((790,30,1090,150),'NestJS API\nAuthentication'),((1170,30,1470,150),'PostgreSQL\nPersistent records')]:box(d,r,t)
for x in [330,710,1090]:arr(d,(x,90),(x+80,90))
box(d,(560,260,1120,380),'ESP32 firmware\nClock / schedules / command journal');arr(d,(900,150),(900,260));arr(d,(990,260),(990,150))
for r,t in [((30,510,460,650),'RTC + two LCDs\nShared I2C bus'),((535,510,965,650),'Three mechanisms\nServo / vibration / sensor'),((1040,510,1470,650),'Buttons / LEDs / buzzer\nUser interface')]:box(d,r,t)
d.line([(840,380),(840,445),(245,445)],fill='#333333',width=3);d.line([(840,445),(1255,445)],fill='#333333',width=3)
for x in [245,750,1255]:arr(d,(x,445),(x,510))
save(im,'architecture')
im,d=sheet(1500,740)
box(d,(40,30,500,135),'5 V supply from original BOM\nFuse / switching: verify installation');box(d,(920,30,1460,135),'Power to actuators + modules\nVerify each device voltage rating');arr(d,(500,82),(920,82))
box(d,(40,230,450,370),'ESP32 control\n3.3 V GPIO logic');box(d,(545,230,955,370),'I2C level shifter\nLV 3.3 V / HV 5 V');box(d,(1050,230,1460,370),'DS1307 + LCD modules\nCheck pull-ups and supply');arr(d,(450,300),(545,300));arr(d,(955,300),(1050,300))
box(d,(40,475,690,620),'Servo signals / DRV8833 control\nExternal actuator power; do not power from GPIO');box(d,(810,475,1460,620),'Sensor outputs -> GPIO34 / 35 / 36\nConfirm sensor type and safe input voltage');arr(d,(240,370),(240,475));
d.line((40,685,1460,685),fill='#333333',width=4);d.text((555,635),'COMMON GROUND',font=ImageFont.truetype(F,31),fill='black');save(im,'power')
im,d=sheet(1500,520)
for r,t in [((25,35,350,170),'Idle\nRest position'),((410,35,735,170),'WaitingStart\nStaggered start'),((795,35,1120,170),'Releasing\nSelected hole'),((795,325,1120,460),'Returning\nBack to rest'),((410,325,735,460),'Shaking\nRead sensor count'),((25,325,350,460),'Complete / retry / fail\nStop or select next hole')]:box(d,r,t)
for a,b in [((350,100),(410,100)),((735,100),(795,100)),((960,170),(960,325)),((795,390),(735,390)),((410,390),(350,390))]:arr(d,a,b)
save(im,'mechanism')
im,d=sheet(1500,520);box(d,(500,20,1000,115),'Valid clock + applicable schedule');box(d,(500,170,1000,255),'Due round -> LCD + buzzer');arr(d,(750,115),(750,170))
for r,t in [((25,345,455,480),'YELLOW\nSnooze 5 min, maximum 3'),((535,345,965,480),'GREEN\nReserve dose -> queue channels\nRun up to two mechanisms'),((1045,345,1475,480),'RED / grace timeout\nSKIPPED / MISSED')]:box(d,r,t)
d.line([(750,255),(750,305),(240,305)],fill='#333333',width=3);d.line([(750,305),(1260,305)],fill='#333333',width=3)
for x in [240,750,1260]:arr(d,(x,305),(x,345))
save(im,'round')
im,d=sheet(1500,640);box(d,(30,20,400,100),'ESP32');box(d,(1100,20,1470,100),'Server / database')
for y,t,rev in [(180,'GET /sync: time, schedules, commands',False),(280,'GET /wait: change notification or timeout',False),(455,'POST /events: event ID + result',False),(575,'Accepted event IDs -> remove from queue',True)]:
 d.text((445,y-48),t,font=ImageFont.truetype(F,29),fill='black');arr(d,(1285 if rev else 215,y),(215 if rev else 1285,y))
box(d,(30,330,850,390),'Reserve command ID in NVS before execution',27);save(im,'exchange')
im,d=sheet(1500,790)
steps=['Admin registers station MAC and provisions device API key','Website generates QR label containing device setup URL','User scans QR -> login -> temporary setup code (5 minutes)','Phone joins device AP -> opens 192.168.4.1','ESP32 joins home Wi-Fi -> saves Wi-Fi -> validates setup code','Pairing and NVS save succeed -> close AP after 10 seconds','Sync -> website confirms -> user configures slots and schedules']
for i,t in enumerate(steps):
 y=15+i*110;box(d,(40,y,1460,y+80),t,30)
 if i<6:arr(d,(750,y+80),(750,y+110))
save(im,'setup')
im,d=sheet(1100,335);box(d,(10,10,1090,325),'1 MedicineA x2\n2 MedicineB x1\n3 MedicineC x1\nNext 08:00  3 items',40);save(im,'lcd')
log=Path('output/restart/evidence/test-run.txt').read_text();t='26 Sep 2026 | macOS 14.5 arm64 | Python 3.12.14\nStart 23:54:38 +07:00 | End 23:55:09 +07:00\n\n'+log[log.index('----------------------------------------------------------------------'):].strip();im,d=sheet(1500,360);d.multiline_text((30,30),t,font=ImageFont.truetype('/System/Library/Fonts/Menlo.ttc',30),fill='black',spacing=12);save(im,'test-evidence')
