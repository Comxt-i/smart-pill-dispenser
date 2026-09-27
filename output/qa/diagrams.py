from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
D=Path('output/qa');font='/System/Library/Fonts/Supplemental/Arial.ttf'
def canvas(name,w,h):
 im=Image.new('RGB',(w,h),'white');return im,ImageDraw.Draw(im)
def box(d,xy,text,size=28):
 d.rectangle(xy,outline='black',width=3);f=ImageFont.truetype(font,size);b=d.multiline_textbbox((0,0),text,font=f,spacing=9,align='center');x=(xy[0]+xy[2]-(b[2]-b[0]))/2;y=(xy[1]+xy[3]-(b[3]-b[1]))/2-b[1];d.multiline_text((x,y),text,font=f,fill='black',spacing=9,align='center')
def arrow(d,a,b):
 import math
 d.line([a,b],fill='black',width=3);t=math.atan2(b[1]-a[1],b[0]-a[0]);L=15;d.polygon([b,(b[0]-L*math.cos(t-.5),b[1]-L*math.sin(t-.5)),(b[0]-L*math.cos(t+.5),b[1]-L*math.sin(t+.5))],fill='black')
im,d=canvas('architecture',1400,600)
box(d,(20,40,280,155),'User / caregiver\nPhone browser');box(d,(370,40,680,155),'React website\nAccount + schedules');box(d,(800,40,1120,155),'NestJS API\nAuthentication');box(d,(1160,40,1380,155),'PostgreSQL',24)
for a,b in [((280,98),(370,98)),((680,98),(800,98)),((1120,98),(1160,98))]:arrow(d,a,b)
box(d,(520,260,970,370),'ESP32 firmware\nClock / schedule / command journal');arrow(d,(900,155),(900,260));arrow(d,(815,260),(815,155))
box(d,(10,470,400,570),'RTC + 2 LCDs\nShared I2C bus');box(d,(500,470,900,570),'3 dispensing mechanisms\nServo / vibration / sensor input');box(d,(1010,470,1390,570),'3 buttons / status LEDs\nBuzzer')
for b in [(205,470),(700,470),(1200,470)]:arrow(d,(745,370),b)
im.save(D/'architecture.png')
im,d=canvas('power',1400,670)
box(d,(20,20,400,120),'5 V adapter (original BOM)');box(d,(500,20,850,120),'Fuse + main switch');box(d,(950,20,1380,120),'+5 V distribution bus');arrow(d,(400,70),(500,70));arrow(d,(850,70),(950,70))
box(d,(20,230,360,360),'ESP32 3.3 V logic\nVIN only if board supports 5 V');box(d,(510,230,890,360),'Bidirectional I2C shifter\nLV 3.3 V / HV 5 V');box(d,(1040,230,1380,360),'RTC + LCD modules\nCheck actual module ratings');arrow(d,(360,290),(510,290));arrow(d,(890,290),(1040,290))
box(d,(40,500,650,610),'Servo supply / DRV8833 motor supply\nExternal power; verify motor voltage');box(d,(810,500,1380,610),'Sensor receiver output -> GPIO34/35/36\nType and output level need verification');arrow(d,(1160,120),(1210,230));arrow(d,(950,95),(350,500));
d.line((20,650,1380,650),fill='black',width=4);d.text((450,615),'COMMON GND',font=ImageFont.truetype(font,27),fill='black');im.save(D/'power.png')
im,d=canvas('mechanism',1400,540)
for x,txt in [(20,'Idle\nRest position'),(305,'WaitingStart\nStagger start'),(590,'Releasing\nSelected hole'),(875,'Returning\nRest position')]:box(d,(x,30,x+240,160),txt)
for x in [260,545,830]:arrow(d,(x,95),(x+45,95))
box(d,(970,310,1370,435),'Shaking\nRead sensor count');arrow(d,(1115,95),(1240,95));arrow(d,(1240,95),(1240,310))
box(d,(30,310,770,435),'Enough pills -> finish / detach servo\nNo pill -> retry or next eligible hole\nCancelled / exhausted attempts -> failed result');arrow(d,(970,370),(770,370));im.save(D/'mechanism.png')
im,d=canvas('round',1400,570)
box(d,(450,15,950,105),'Valid clock + applicable schedule');box(d,(450,160,950,245),'Due round -> LCD / buzzer alert');arrow(d,(700,105),(700,160))
for x,txt in [(10,'YELLOW\nSnooze 5 min (max 3)'),(480,'GREEN\nQueue due channels'),(950,'RED / timeout\nSKIPPED / MISSED')]:box(d,(x,320,x+430,415),txt);arrow(d,(700,245),(x+215,320))
box(d,(380,485,1020,565),'Dispense up to 2 channels -> report result');arrow(d,(695,415),(695,485));im.save(D/'round.png')
im,d=canvas('exchange',1400,620)
box(d,(10,20,420,100),'ESP32');box(d,(970,20,1390,100),'Server / database')
f=ImageFont.truetype(font,28)
for y,txt,direction in [(160,'GET /sync : time, schedules, commands','r'),(260,'GET /wait : state change or timeout','r'),(360,'Reserve command ID in NVS, then execute','local'),(460,'POST /events : event ID + result','r'),(560,'accepted event IDs -> remove from queue','l')]:
 if direction=='r':arrow(d,(215,y),(1175,y))
 elif direction=='l':arrow(d,(1175,y),(215,y))
 d.text((440 if direction!='local' else 35,y-45),txt,font=f,fill='black')
im.save(D/'exchange.png')
im,d=canvas('setup',1400,790)
steps=['ADMIN registers device + provisions API key','Website creates QR label with setup URL + station MAC','User scans QR -> login -> temporary setup code (5 min)','Phone joins device AP -> opens 192.168.4.1','ESP32 joins home Wi-Fi -> saves Wi-Fi -> verifies setup code','Pairing succeeds + NVS write succeeds -> close AP after 10 s','New sync -> website confirms -> configure slots and schedules']
for i,t in enumerate(steps):
 y=10+i*110;box(d,(65,y,1335,y+78),t,28)
 if i<6:arrow(d,(700,y+78),(700,y+110))
im.save(D/'setup.png')
im,d=canvas('lcd',1000,310);box(d,(5,5,995,305),'1 MedicineA x2\n2 MedicineB x1\n3 MedicineC x1\nNext 08:00  3 items',39);im.save(D/'lcd.png')
# Verbatim excerpt from captured test output, not a hardware screenshot.
log=Path('output/evidence/test-run.txt').read_text();excerpt='26 Sep 2026 | macOS 14.5 arm64 | Python 3.12.14\nStart 16:50:35 +07:00 | End 16:51:04 +07:00\n\n'+log[log.index('----------------------------------------------------------------------'):].strip()
im,d=canvas('test',1400,350);d.multiline_text((30,30),excerpt,font=ImageFont.truetype('/System/Library/Fonts/Menlo.ttc',30),fill='black',spacing=14);im.save(D/'test-evidence.png')
