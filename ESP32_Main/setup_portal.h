#pragma once

// Entire portal is served locally; no CDN, cloud request, or Wi-Fi password in a URL.
static const char SETUP_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="th"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ตั้งค่ากล่องยา</title>
<style>body{font:16px system-ui,sans-serif;background:#f2f6f1;color:#142116;max-width:480px;margin:24px auto;padding:16px}h1{font-size:23px}label{display:block;margin:18px 0 6px}input,select,button{font:inherit;box-sizing:border-box;width:100%;padding:12px;border:1px solid #c9d7c5;border-radius:12px}button{background:#567d63;color:white;margin-top:18px;cursor:pointer}small{display:block;line-height:1.6;margin-top:10px}#status,#timeStatus{padding:12px;background:white;border-radius:12px;white-space:pre-wrap}h2{font-size:19px;margin-top:28px}.card{background:white;border-radius:12px;padding:12px 16px}#clock{font-size:22px;font-weight:600;margin:6px 0}.warn{color:#a3361f}.secondary{background:white;color:#142116}button:disabled{opacity:.5}</style>
<h1>ตั้งค่ากล่องยา</h1>
<h2>เวลาในกล่อง</h2><div class="card"><div id="clock">กำลังอ่าน...</div></div>
<button id="setTime" type="button">ตั้งเวลาตามมือถือ</button><p id="timeStatus" role="status" hidden></p>
<h2>Wi-Fi</h2><p>ใช้รหัสตั้งค่าที่ได้หลังล็อกอินจาก QR บนกล่อง</p>
<form id="setup"><input type="hidden" name="nonce" value="__NONCE__">
<label for="networks">เครือข่าย Wi-Fi ใกล้เคียง</label><select id="networks"><option value="">กำลังค้นหา...</option></select>
<label for="ssid">ชื่อ Wi-Fi บ้าน (2.4 GHz)</label><input id="ssid" name="ssid" maxlength="32" required autocomplete="off">
<label for="password">รหัสผ่าน Wi-Fi บ้าน</label><input id="password" name="password" type="password" maxlength="63" autocomplete="new-password"><small>เว้นว่างเฉพาะเครือข่ายที่ไม่มีรหัสผ่าน</small>
<p id="token-help">รหัสใช้ได้ 5 นาที พิมพ์ตัวเล็กหรือใหญ่ได้ เว้นขีดได้</p><label for="token">รหัสตั้งค่าจากเว็บไซต์ (8 ตัว)</label><input id="token" name="token" aria-describedby="token-help" required minlength="8" maxlength="40" placeholder="ABCD-EFGH" autocomplete="off" autocapitalize="characters" spellcheck="false">
<button id="connect" type="submit">เชื่อมต่อและจับคู่</button></form>
<p id="status" role="status">รหัสผ่าน Wi-Fi จะบันทึกในกล่องยา ไม่ส่งขึ้นเว็บไซต์</p>
<button id="close" type="button" class="secondary">เสร็จแล้ว กลับไปทำงานปกติ</button>
<script>
const form=document.getElementById('setup'),statusBox=document.getElementById('status'),button=document.getElementById('connect');
let stopped=false,autoSynced=false;
const nonce=form.elements.nonce.value,clockBox=document.getElementById('clock'),timeStatus=document.getElementById('timeStatus'),setTime=document.getElementById('setTime'),closeButton=document.getElementById('close');
// กล่องเก็บเวลาท้องถิ่นเป็นวินาที จึงส่งเวลามือถือที่บวกเขตเวลาแล้ว และแสดงผลด้วย getUTC*
function phoneLocalEpoch(){const d=new Date();return Math.floor(d.getTime()/1000)-d.getTimezoneOffset()*60}
function fmt(e){const d=new Date(e*1000),p=n=>String(n).padStart(2,'0');return p(d.getUTCDate())+'/'+p(d.getUTCMonth()+1)+'/'+d.getUTCFullYear()+' '+p(d.getUTCHours())+':'+p(d.getUTCMinutes())+':'+p(d.getUTCSeconds())}
function showClock(d){if(!d.clock){clockBox.textContent='ยังไม่ได้ตั้งเวลา';clockBox.className='warn';}else{const off=Math.abs(d.clock-phoneLocalEpoch())>60;clockBox.textContent=fmt(d.clock)+(off?' (ไม่ตรงกับมือถือ)':'');clockBox.className=off?'warn':'';}if(!autoSynced&&(!d.clock||Math.abs(d.clock-phoneLocalEpoch())>60)){autoSynced=true;post('/setup/time',{local_epoch:phoneLocalEpoch(),auto:'1'}).then(([,x])=>{timeStatus.hidden=false;timeStatus.textContent=x.message}).catch(()=>{autoSynced=false});}}
async function post(path,extra){const r=await fetch(path,{method:'POST',body:new URLSearchParams({nonce,...extra})});return [r,await r.json()]}
setTime.onclick=async()=>{setTime.disabled=true;timeStatus.hidden=false;try{const [,d]=await post('/setup/time',{local_epoch:phoneLocalEpoch()});timeStatus.textContent=d.message;}catch{timeStatus.textContent='ติดต่อกล่องไม่ได้ ตรวจสอบว่ายังเชื่อม Wi-Fi ของกล่องอยู่';}setTime.disabled=false;};
closeButton.onclick=async()=>{try{const [r,d]=await post('/setup/close',{});statusBox.textContent=d.message;if(r.ok){stopped=true;closeButton.disabled=setTime.disabled=button.disabled=true;}}catch{statusBox.textContent='ติดต่อกล่องไม่ได้';}};
async function scan(){try{const r=await fetch('/setup/scan');const d=await r.json();if(d.scanning){setTimeout(scan,1200);return}const list=document.getElementById('networks');list.replaceChildren(new Option('เลือกเครือข่าย หรือกรอกชื่อเอง',''));for(const s of d.networks)list.add(new Option(s,s));}catch{document.getElementById('networks').replaceChildren(new Option('กรอกชื่อ Wi-Fi ด้านล่าง',''));}}
document.getElementById('networks').onchange=e=>{document.getElementById('ssid').value=e.target.value};scan();
form.onsubmit=async e=>{e.preventDefault();button.disabled=true;try{const r=await fetch('/setup/connect',{method:'POST',body:new URLSearchParams(new FormData(form))});const d=await r.json();statusBox.textContent=d.message;if(!r.ok)button.disabled=false;}catch{statusBox.textContent='ติดต่อกล่องไม่ได้ ตรวจสอบว่ายังเชื่อม Wi-Fi Setup อยู่';button.disabled=false;}};
async function poll(){if(stopped)return;try{const r=await fetch('/setup/status');const d=await r.json();statusBox.textContent=d.message;showClock(d);button.disabled=d.busy||d.done;if(d.done){stopped=true;form.reset();statusBox.textContent+='\nกลับไปเชื่อม Wi-Fi บ้านหรืออินเทอร์เน็ตมือถือ แล้วเปิดแท็บเว็บไซต์เดิม';}}catch{}if(!stopped)setTimeout(poll,2000)}poll();
</script></html>
)HTML";
