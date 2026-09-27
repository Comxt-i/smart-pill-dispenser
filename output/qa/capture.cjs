const {chromium}=require('/Users/i-comxt/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const fs=require('fs'),path=require('path');
(async()=>{const browser=await chromium.launch({headless:true,executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'});const page=await browser.newPage({viewport:{width:1080,height:1050},deviceScaleFactor:1.5});
const base='/Users/i-comxt/Desktop/embedded_project/frontend/dist';let completed=false;
await page.route('**/*',async route=>{const u=new URL(route.request().url());let data;
if(u.pathname.includes('/api/')){
if(u.pathname.endsWith('/setup-label'))data={id:'demo',name:'กล่องตัวอย่างสำหรับรายงาน',setup_path:'/setup/02:00:00:00:00:01',ssid:'PillBox_DEMO',password:'DEMO ONLY'};
else if(u.pathname.endsWith('/setup'))data={id:'demo',name:'กล่องตัวอย่างสำหรับรายงาน',mac_address:'02:00:00:00:00:01',setup_ssid:'PillBox_DEMO',paired:false};
else if(u.pathname.endsWith('/setup-session'))data={token:'DEMO2345',session_id:'demo-session',expires_at:new Date(Date.now()+300000).toISOString()};
else if(u.pathname.includes('/setup-session/'))data={completed};else data=[];
return route.fulfill({json:data});}
if(u.pathname==='/portal'){let html=fs.readFileSync('ESP32_Main/setup_portal.h','utf8').split('R"HTML(')[1].split(')HTML"')[0].replace('__NONCE__','demo');return route.fulfill({contentType:'text/html',body:html});}
if(u.pathname==='/setup/scan')return route.fulfill({json:{networks:['Report_Demo_2G']}});
if(u.pathname==='/setup/status')return route.fulfill({json:{message:'ข้อมูลตัวอย่างสำหรับรายงาน ไม่มีการเชื่อมต่ออุปกรณ์จริง',busy:false,done:false}});
let p=path.join(base,u.pathname);if(!fs.existsSync(p)||fs.statSync(p).isDirectory())p=path.join(base,'index.html');let ext=path.extname(p);return route.fulfill({body:fs.readFileSync(p),contentType:ext==='.js'?'application/javascript':ext==='.css'?'text/css':'text/html'});
});
await page.addInitScript(()=>{localStorage.setItem('access_token','report-demo-not-valid');localStorage.setItem('user_profile',JSON.stringify({id:'demo',name:'ผู้ใช้ตัวอย่าง',email:'demo@example.invalid',role:location.pathname.includes('admin')?'ADMIN':'PATIENT'}));});
await page.goto('http://report.local/admin/devices/demo/label');await page.waitForTimeout(700);await page.locator('.pillbox-label-page').screenshot({path:'output/qa/web-label.png'});
await page.goto('http://report.local/setup/demo');await page.getByRole('button',{name:'สร้างรหัสตั้งค่า',exact:true}).click();await page.waitForTimeout(300);await page.locator('main > div').screenshot({path:'output/qa/web-setup.png'});
completed=true;await page.waitForTimeout(5100);await page.locator('div[role=status]').screenshot({path:'output/qa/web-done.png'});
await page.setViewportSize({width:600,height:1000});await page.goto('http://report.local/portal');await page.waitForTimeout(400);await page.locator('body').screenshot({path:'output/qa/portal.png'});
await browser.close();})();
