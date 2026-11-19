"""Draw glyph sheets from our public fixtures and generate our manually annotated shape index.

Pillow is only a development tool. The application uses the generated C++ data.
Characters below were read visually from build/hid-glyphs/{model}.png, not copied
from another project's alphabets. Ambiguous or unread characters are omitted.
"""
from pathlib import Path
from PIL import Image,ImageDraw
import json
out=Path('build/hid-glyphs');out.mkdir(exist_ok=True)
letters = {
 '650': ['H','y','/','(',' ','0','s','e','c',')','.','1',
         '2','3','4','5','6','7','8','9','a','A','j','o',
         'u','t','z','n','i','v','é','r','b','à','l',"'",
         'd','g','f','m','Q','C','x','U','S','B','h','I',
         'p','M','P','ê','ç','T','W','è','L','E','D','R',
         'F','q','N','\\','O','X','V','-','?','É','w','k',
         'Y','#','G','+','K','!',None,None,'"','ù',':',None],
 '525': ['H','y','O','F','a','T','e','r','m','i','n','t',
         ' ','E','U','S','B','C','N','D','p','g','d','u',
         'c','s','0',':','o','2','f','I','L','.','w','G',
         'W','b','l','z','k','R','P','h','A','M','q','\\',
         'v','V','-','X','3','6','1','?','Y','9','x','+',
         '/','4','#','5','Z','*'],
 '880': ['H','y','1','2',':','3','4','5','6','7','8','9',
         '0','A','M','P','O','F','a','B','t','e','r',' ',
         'l','v','o','w','!','c','h','m','i','n','g','s',
         'd','b','.','"','-','>','U','S','C','N','E','T',
         'D','G','W','u','p','I','z','L','k','R','f','q',
         '\\','V','x','?','Y','<','Z','/',"'",'J'],
}
shapes = set()
for model,slot in [('650',7),('525',7),('880',7)]:
 raw=Path('tests/data/binaryhid/'+model+'.EZHex').read_bytes();i=raw.index(b'</INFORMATION>')+14
 while raw[i] in (10,13):i+=1
 b=raw[i:];base=int.from_bytes(b[4:8],'little')-(len(b)-4);u=lambda p,n:int.from_bytes(b[p:p+n],'little');s=u(12+4*slot,3)-base;glyphs={}
 for f in range(u(s,2)):
  font=u(s+2+3*f,3)-base;first,count=b[font+1:font+3]
  for g in range(count):
   ptr=u(font+3+3*g,3)
   if not ptr:continue
   cur=ptr-base;w=b[cur];cur+=1;rows=[];row=[]
   for limit in range(2000):
    c=b[cur];cur+=1
    if c==0:
     if row:rows.append(row)
     break
    if model=='525':
     end=cur+(c&31);row=[]
     while cur<end:
      op=b[cur];cur+=1;n=(op&15)+1;k=op>>4
      if k==5:
       literal=b[cur:cur+(n+3)//4];cur+=(n+3)//4;row.extend(1 if (literal[j//4]>>(6-2*(j%4)))&3==1 else 0 for j in range(n))
      elif k in (6,10):row.extend([1 if k==10 else 0]*n)
      else:raise ValueError((model,k))
     rows.append(row);row=[]
    elif c==128:rows.append(row);row=[]
    elif c&128:row.extend([0]*(c&127))
    else:row.extend([1]*c);cur+=2*c
   code=first+g
   character = letters[model][code-1] if code <= len(letters[model]) else None
   if character and all(len(row)==w for row in rows):
    fingerprint=1469598103934665603
    for value in [w,len(rows),*(pixel for row in rows for pixel in row)]:
     fingerprint=((fingerprint ^ value)*1099511628211) & ((1<<64)-1)
    shapes.add((fingerprint,character))
   if code not in glyphs or len(rows)>len(glyphs[code]):glyphs[code]=rows
 count=max(glyphs);sheet=Image.new('RGB',(12*90,((count+11)//12)*85),'white');d=ImageDraw.Draw(sheet)
 for code,rows in glyphs.items():
  x=((code-1)%12)*90;y=((code-1)//12)*85;d.text((x+3,y+2),str(code),fill='blue')
  for yy,row in enumerate(rows):
   for xx,pix in enumerate(row):
    if pix:d.rectangle((x+5+xx*3,y+22+yy*3,x+7+xx*3,y+24+yy*3),fill='black')
 sheet.save(out/(model+'.png'));(out/(model+'.json')).write_text(json.dumps(glyphs))
target=Path('core/src/formats/hidGlyphNames.inc')
target.write_text('// Glyph shapes annotated from our 650, 525 and 880 fixtures.\n'
                  'static constexpr GlyphName kGlyphNames[] = {\n'+
                  ''.join(f'    {{0x{fingerprint:016x}ULL, {json.dumps(character,ensure_ascii=False)}}},\n'
                          for fingerprint,character in sorted(shapes))+'};\n',encoding='utf-8')
