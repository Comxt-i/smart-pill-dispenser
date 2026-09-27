from pathlib import Path
from pypdf import PdfReader
from PIL import Image,ImageDraw
Q=Path('output/restart/qa/final-render');pdf=next(Q.glob('*.pdf'));r=PdfReader(pdf)
for i,p in enumerate(r.pages):
 t=p.extract_text();Path(f'output/restart/qa/page-{i+1}.txt').write_text(t);print(i+1,t[:90].replace('\n',' '), 'END',t[-90:].replace('\n',' '))
ps=sorted(Q.glob('page-*.png'),key=lambda x:int(x.stem.split('-')[1]))
for j in range(0,len(ps),6):
 c=Image.new('RGB',(1200,1690),'#cccccc');dr=ImageDraw.Draw(c)
 for k,p in enumerate(ps[j:j+6]):
  im=Image.open(p);im.thumbnail((390,790));x=(k%3)*400;y=(k//3)*845;c.paste(im,(x,y+25));dr.text((x+10,y+5),p.stem,fill='black')
 c.save(f'output/restart/qa/contact-{j//6}.jpg')
