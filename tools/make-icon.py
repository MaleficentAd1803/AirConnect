"""Generate the checked-in application icon from the native display-mark geometry."""
from pathlib import Path
from PIL import Image, ImageDraw

scale=4
image=Image.new("RGBA",(256*scale,256*scale),(0,0,0,0))
d=ImageDraw.Draw(image)
def box(v): return tuple(int(x*scale) for x in v)
d.rounded_rectangle(box((6,6,250,250)),radius=44*scale,fill="#246bc5")
d.rounded_rectangle(box((45,59,211,178)),radius=10*scale,outline="white",width=6*scale)
d.line(box((106,178,106,204)),fill="white",width=6*scale)
d.line(box((150,178,150,204)),fill="white",width=6*scale)
d.line(box((86,205,170,205)),fill="white",width=6*scale)
d.line(box((109,118,128,99,147,118)),fill="white",width=6*scale,joint="curve")
d.line(box((128,102,128,149)),fill="white",width=6*scale)
image=image.resize((256,256),Image.Resampling.LANCZOS)
image.save(Path(__file__).resolve().parents[1]/"src"/"airconnect.ico",sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
