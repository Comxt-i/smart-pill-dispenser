const {createCanvas,GlobalFonts}=require('/Users/i-comxt/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/@napi-rs/canvas');
const fs=require('fs');
GlobalFonts.registerFromPath('/System/Library/Fonts/Supplemental/Thonburi.ttc','Thai');
const canvas=createCanvas(1800,1450),c=canvas.getContext('2d');
const ink='#20333f',green='#2b6e57',pale='#ebf5ef';
c.fillStyle='white';c.fillRect(0,0,1800,1450);
function box(x,y,w,h,color){c.fillStyle=color;c.beginPath();c.roundRect(x,y,w,h,16);c.fill();}
function text(s,x,y,w,size=30,bold=false){c.fillStyle=ink;c.font=`${bold?'bold ':''}${size}px Thai`;c.textAlign='center';c.textBaseline='top';s.split('\n').forEach((t,i)=>c.fillText(t,x+w/2,y+i*(size+10)));}
function line(x1,y1,x2,y2,color=green,width=4){c.strokeStyle=color;c.lineWidth=width;c.beginPath();c.moveTo(x1,y1);c.lineTo(x2,y2);c.stroke();}
function arrow(from,to,y,label){line(from,y,to,y);let d=to>from?-1:1;line(to,y,to+d*18,y-10);line(to,y,to+d*18,y+10);const l=Math.min(from,to),w=Math.abs(to-from);box(l+12,y-78,w-24,69,'white');text(label,l+10,y-78,w-20,27);}
text('การสื่อสารผ่านเครือข่ายและ API',100,32,1600,44,true);
text('ตัวอย่างลำดับเมื่อผู้ใช้สั่งจ่ายยาจากเว็บไซต์',100,103,1600,28);
for(const [x,title,sub] of [[260,'เว็บไซต์','บัญชีผู้ใช้ + JWT'],[900,'เซิร์ฟเวอร์และฐานข้อมูล','ตรวจสอบและบันทึกข้อมูล'],[1540,'กล่องยา ESP32','API Key เฉพาะเครื่อง']]){
box(x-220,178,440,116,pale);text(title,x-210,194,420,32,true);text(sub,x-210,249,420,25);
for(let y=314;y<1260;y+=22)line(x,y,x,y+11,'#bdc9c3',2);
}
arrow(260,900,380,'1. ส่งคำสั่งจ่ายยา\nเซิร์ฟเวอร์เก็บคำสั่งรอดำเนินการ');
arrow(1540,900,490,'2. รอแจ้งการเปลี่ยนแปลง\nGET /api/device/wait');
arrow(900,1540,595,'3. แจ้งว่ามีข้อมูลเปลี่ยน\nหรือมีคำสั่งใหม่รออยู่');
arrow(1540,900,700,'4. ขอข้อมูลล่าสุด\nGET /api/device/sync?schema=2');
arrow(900,1540,805,'5. ส่งเวลา ตารางยา\nและคำสั่งที่รอดำเนินการ');
box(1260,850,500,107,pale);text('6. ตรวจคำสั่งและควบคุมกลไก\nประเมินผลการจ่ายยา',1270,865,480,29,true);
arrow(1540,900,1040,'7. ส่งผลพร้อมรหัสอ้างอิงคำสั่ง\nPOST /api/device/events');
arrow(900,1540,1150,'8. ตอบผลการรับบันทึก\nESP32 จัดการรายการในคิว');
arrow(900,260,1250,'9. เว็บไซต์ดึงผลที่บันทึกแล้ว\nมาแสดงให้ผู้ใช้ตรวจสอบ');
box(110,1310,1580,96,pale);
text('รับคำสั่งแล้ว ≠ จ่ายยาเสร็จแล้ว  |  ผลการจ่ายต้องอ้างอิงเหตุการณ์ที่ ESP32 ส่งกลับ',125,1340,1550,29,true);
fs.writeFileSync('/Users/i-comxt/Desktop/smart-pill-dispenser/output/network-api-sequence.png',canvas.toBuffer('image/png'));
