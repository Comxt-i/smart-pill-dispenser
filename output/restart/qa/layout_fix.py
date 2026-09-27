from pathlib import Path
q=Path('output/restart/qa/build_report.py');s=q.read_text()
start=s.index("p('เก็บสำเนาไฟล์โค้ด");end=s.index("p('การตรวจไวยากรณ์",start);s=s[:start]+s[end:]
s=s.replace("'ภาพที่ 3.2 หน้าเว็บตั้งค่าที่ถ่ายจากอุปกรณ์ โดยปิดบังข้อมูลเครือข่ายและรหัส [7]',6)","'ภาพที่ 3.2 หน้าเว็บตั้งค่าที่ถ่ายจากอุปกรณ์ โดยปิดบังข้อมูลเครือข่ายและรหัส [7]',7)")
s=s.replace("'ภาพที่ 3.12 หน้าเว็บตั้งค่ารุ่นก่อนจากต้นฉบับ ปิดบังรหัสแล้ว [7]',8)","'ภาพที่ 3.12 หน้าเว็บตั้งค่ารุ่นก่อนจากต้นฉบับ ปิดบังรหัสแล้ว [7]',11)")
s=s.replace("listing=figs+tabs","listing=[('สารบัญภาพ','')]+figs+[('สารบัญตาราง','')]+tabs")
s=s.replace("t=re.sub(r' \\[[^]]+\\].*$','',t);toc(q,t,n)","t=re.sub(r' \\[[^]]+\\].*$','',t)\n if n=='':\n  q.add_run(t).bold=True\n else:toc(q,t,n)")
s=s.replace("s.font.color.rgb=RGBColor(0,0,0)","s.font.color.rgb=RGBColor(0,0,0)")
q.write_text(s)
