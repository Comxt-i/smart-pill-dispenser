from pathlib import Path
q=Path('output/restart/qa/diagrams.py');s=q.read_text().replace(";arr(d,(1255,475),(1255,400))",'');q.write_text(s)
q=Path('output/restart/qa/build_report.py');s=q.read_text().replace('line_spacing=1.18','line_spacing=1.12').replace('r.font.size=Pt(9)\n q.paragraph_format.space_after=Pt(4)','r.font.size=Pt(8.5)\n q.paragraph_format.space_after=Pt(4)');q.write_text(s)
