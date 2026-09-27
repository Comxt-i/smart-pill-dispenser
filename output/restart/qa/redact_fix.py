from PIL import Image,ImageDraw
from pathlib import Path
im=Image.open('output/restart/source/image13.jpeg').convert('RGB');dr=ImageDraw.Draw(im)
for b in [(315,1220,1320,1305),(315,1420,1320,1495),(315,1650,1320,1760)]:dr.rectangle(b,fill='black')
im.crop((180,220,1400,1940)).save('output/restart/qa/actual-portal.png')
