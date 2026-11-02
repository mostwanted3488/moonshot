import numpy as np, cv2, math, random
from PIL import Image, ImageDraw
from scipy import ndimage as ndi
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

U=80            # px per skeleton unit
H=10            # cap height in units
EM=1000; CAP=720
SC=CAP/(H*U)    # px -> font units
rnd=random.Random(7); np.random.seed(7)

def arc(cx,cy,rx,ry,a0,a1,n=14):
    return [(cx+rx*math.cos(math.radians(a0+(a1-a0)*i/n)), cy+ry*math.sin(math.radians(a0+(a1-a0)*i/n))) for i in range(n+1)]
# skeletons: y=0 top, y=10 bottom (image coords). width W units.
def G(W,*strokes): return (W,[list(s) for s in strokes])
S={}
S['A']=G(6,[(0,10),(3,0),(6,10)],[(1.2,6.6),(4.9,6.6)])
S['B']=G(6,[(0.6,0),(0.6,10)],[(0.6,0.4),(4,0.4)]+arc(4,2.7,1.9,2.3,-90,90)[1:-1]+[(0.8,5)],[(0.8,5),(4.3,5)]+arc(4.3,7.5,1.9,2.4,-90,90)[1:-1]+[(0.8,9.8)])
S['C']=G(6,arc(3.2,5,2.9,4.7,-40,-320,24))
S['D']=G(6,[(0.6,0),(0.6,10)],[(0.6,0.4),(2.4,0.4)]+arc(2.4,5,3.1,4.6,-90,90,16)[1:-1]+[(0.6,9.6)])
S['E']=G(5,[(0.6,0),(0.6,10)],[(0.6,0.5),(4.6,0.3)],[(0.6,5),(3.8,5)],[(0.6,9.6),(4.8,9.8)])
S['F']=G(5,[(0.6,0),(0.6,10)],[(0.6,0.5),(4.6,0.3)],[(0.6,5),(3.8,5)])
S['G']=G(6,arc(3.2,5,2.9,4.7,-40,-320,24)[:20]+[(3.4,5.4)],[(3.4,5.4),(5.6,5.4),(5.6,8.6)])
S['H']=G(6,[(0.6,0),(0.6,10)],[(5.2,0),(5.2,10)],[(0.6,5),(5.2,5)])
S['I']=G(3,[(1.5,0),(1.5,10)],[(0.2,0.2),(2.8,0.2)],[(0.2,9.8),(2.8,9.8)])
S['J']=G(5,[(4,0),(4,7)]+arc(2.4,7,1.6,2.8,0,160,8)[1:])
S['K']=G(6,[(0.6,0),(0.6,10)],[(5.2,0),(0.8,5.4)],[(1.6,4.6),(5.4,10)])
S['L']=G(5,[(0.6,0),(0.6,10),(4.8,9.8)])
S['M']=G(8,[(0.6,10),(0.8,0),(4,6),(7.2,0),(7.4,10)])
S['N']=G(6.5,[(0.6,10),(0.6,0),(5.6,10),(5.6,0)])
S['O']=G(6,arc(3,5,2.6,4.7,0,360,28))
S['P']=G(6,[(0.6,0),(0.6,10)],[(0.6,0.4),(3.6,0.4)]+arc(3.6,2.9,2,2.5,-90,90)[1:-1]+[(0.8,5.4)])
S['Q']=G(6,arc(3,5,2.6,4.5,0,360,28),[(3.4,7.2),(5.6,10.3)])
S['R']=G(6,[(0.6,0),(0.6,10)],[(0.6,0.4),(3.6,0.4)]+arc(3.6,2.9,2,2.5,-90,90)[1:-1]+[(0.8,5.4)],[(2.4,5.4),(5.4,10)])
S['S']=G(6,arc(3.1,2.6,2.4,2.4,-20,-270,14)+arc(3.1,7.5,2.5,2.5,-90,160,14))
S['T']=G(6,[(0,0.4),(6,0.2)],[(3,0.4),(3,10)])
S['U']=G(6,[(0.6,0),(0.6,6.4)]+arc(3,6.4,2.4,3.4,180,0,14)[1:]+[(5.4,0)])
S['V']=G(6,[(0,0),(3,10),(6,0)])
S['W']=G(9,[(0,0),(2.2,10),(4.5,2.6),(6.8,10),(9,0)])
S['X']=G(6,[(0.2,0),(5.8,10)],[(5.8,0),(0.2,10)])
S['Y']=G(6,[(0,0),(3,5.2),(6,0)],[(3,5.2),(3,10)])
S['Z']=G(6,[(0.4,0.4),(5.6,0.2),(0.4,9.8),(5.8,9.8)])
S['0']=G(6,arc(3,5,2.5,4.7,0,360,28),[(1.4,8),(4.6,2)])
S['1']=G(4,[(0.6,2.4),(2.6,0),(2.6,10)],[(0.8,9.8),(4.4,9.8)])
S['2']=G(6,arc(3,2.9,2.5,2.7,180,380,12)+[(0.5,10),(5.8,9.8)])
S['3']=G(6,arc(3,2.6,2.4,2.5,-150,90,12)+arc(3,7.4,2.6,2.6,-90,150,12))
S['4']=G(6,[(4.4,10),(4.4,0),(0,6.6),(5.8,6.6)])
S['5']=G(6,[(5,0.4),(1,0.4),(0.8,4.4)]+arc(3,7,2.6,2.8,-120,150,14))
S['6']=G(6,[(4.6,0.2),(1.4,3.4)]+arc(3,6.8,2.4,3.2,200,-160,22)[0:0]+arc(3,7,2.5,2.9,0,360,24))
S['7']=G(6,[(0.2,0.4),(5.6,0.2),(2.2,10)])
S['8']=G(6,arc(3,2.6,2.2,2.5,0,360,20),arc(3,7.4,2.6,2.6,0,360,22))
S['9']=G(6,arc(3,3,2.5,2.9,0,360,24),[(5.5,3),(4.6,10)])
S['%']=G(8,[(6.4,0),(1.2,10)],arc(1.8,2.1,1.1,1.5,0,360,12),arc(6.2,7.9,1.1,1.5,0,360,12))
S['.']=G(2.4,arc(1.2,8.8,.8,.8,0,360,10))
S['-']=G(4,[(0.4,5.2),(3.6,5.0)])
S['/']=G(5,[(4.4,0),(0.6,10)])
S['©']=G(8,arc(4,5,3.6,4.6,0,360,28),arc(4.1,5,1.4,1.7,-30,-330,12))
S[' ']=G(4)
# lowercase maps to uppercase glyph shapes (grunge small-caps look comes from per-glyph jitter)

def render(ch):
    W,strokes=S[ch]
    pw=int(W*U+4*U); ph=int(H*U+4*U)
    ox,oy=2*U,2*U
    base=Image.new('L',(pw,ph),0); d=ImageDraw.Draw(base)
    sw=U*0.72
    for st in strokes:
        pts=[(ox+x*U,oy+y*U) for x,y in st]
        # resample
        res=[]
        for a,b in zip(pts[:-1],pts[1:]):
            L=max(1,int(math.hypot(b[0]-a[0],b[1]-a[1])/3))
            for i in range(L): res.append((a[0]+(b[0]-a[0])*i/L,a[1]+(b[1]-a[1])*i/L))
        res.append(pts[-1])
        ph0=rnd.random()*6
        for i,(x,y) in enumerate(res):
            wob=math.sin(i*.07+ph0)*U*.05+math.sin(i*.19+ph0*2)*U*.03
            r=sw*(.78+.28*math.sin(i*.045+ph0*1.7)+.12*rnd.random())
            d.ellipse([x+wob-r,y-r,x+wob+r,y+r],fill=255)
    a=np.array(base).astype(float)/255
    # rough, eaten edges + drips
    a=ndi.gaussian_filter(a,2.2)
    n1=ndi.gaussian_filter(np.random.randn(*a.shape),5)*3.2
    n2=ndi.gaussian_filter(np.random.randn(*a.shape),1.6)*.9
    m=(a+(n1+n2)*0.30)>0.5
    # a couple of drips
    ys,xs=np.where(m)
    if len(xs):
        for _ in range(2):
            i=rnd.randrange(len(xs))
            if ys[i]>oy+H*U*0.6:
                x0=xs[i]; y0=ys[i]; ln=rnd.randint(14,40)
                m[y0:y0+ln,x0-2:x0+3]=True
    m=ndi.binary_opening(m,iterations=1)
    # drop specks
    lab,n=ndi.label(m)
    if n:
        sizes=ndi.sum(m,lab,range(1,n+1))
        for k,sz in enumerate(sizes,1):
            if sz<260: m[lab==k]=False
    # per-glyph tilt / drop for the uneven, hand-made feel
    ang=rnd.uniform(-3.5,3.5); dy=int(rnd.uniform(-10,12))
    M=cv2.getRotationMatrix2D((pw/2,ph/2),ang,1.0); M[1,2]+=dy
    m=cv2.warpAffine(m.astype(np.uint8)*255,M,(pw,ph),flags=cv2.INTER_LINEAR)>127
    return m,ox,oy,W

def contours(m):
    cs,hier=cv2.findContours(m.astype(np.uint8),cv2.RETR_CCOMP,cv2.CHAIN_APPROX_NONE)
    out=[]
    for i,c in enumerate(cs):
        if len(c)<8: continue
        ap=cv2.approxPolyDP(c,2.2,True)[:,0,:]
        if len(ap)<3: continue
        hole=hier[0][i][3]!=-1
        out.append((ap,hole))
    return out

chars=list(S.keys())
glyphs={'.notdef':None}
cmap={}
metrics={}
order=['.notdef']
def name(ch): 
    return {'%':'percent','©':'copyright','.':'period','-':'hyphen','/':'slash',' ':'space'}.get(ch,ch if ch.isdigit()==False else {'0':'zero','1':'one','2':'two','3':'three','4':'four','5':'five','6':'six','7':'seven','8':'eight','9':'nine'}[ch])

def make_glyph(cs,ox,oy):
    pen=TTGlyphPen(None)
    for ap,hole in cs:
        pts=[(int((x-ox)*SC+30),int((H*U+0 -(y-oy))*SC)) for x,y in ap]  # flip y; baseline at y=oy+H*U
        # orientation: outer clockwise, holes counter-clockwise in y-up
        area=0
        for (x1,y1),(x2,y2) in zip(pts,pts[1:]+pts[:1]): area+=x1*y2-x2*y1
        cw=area<0
        if hole==cw: pts=pts[::-1]
        pen.moveTo(pts[0])
        for p in pts[1:]: pen.lineTo(p)
        pen.closePath()
    return pen.glyph()

# .notdef box
pen=TTGlyphPen(None);pen.moveTo((60,0));pen.lineTo((60,720));pen.lineTo((440,720));pen.lineTo((440,0));pen.closePath()
glyphs['.notdef']=pen.glyph();metrics['.notdef']=(500,60)
for ch in chars:
    nm=name(ch)
    if ch==' ':
        pen=TTGlyphPen(None);glyphs[nm]=pen.glyph();metrics[nm]=(int(4*U*SC*.55),0);cmap[32]=nm;order.append(nm);continue
    m,ox,oy,W=render(ch)
    cs=contours(m)
    g=make_glyph(cs,ox,oy)
    glyphs[nm]=g
    adv=int(W*U*SC)+80
    metrics[nm]=(adv,0)
    cmap[ord(ch)]=nm
    if ch.isalpha(): cmap[ord(ch.lower())]=nm
    order.append(nm)
fb=FontBuilder(EM,isTTF=True)
fb.setupGlyphOrder(order)
fb.setupCharacterMap(cmap)
fb.setupGlyf(glyphs)
# fix lsb metrics from glyphs
hm={}
for nm in order:
    adv=metrics[nm][0]; g=glyphs[nm]
    g.recalcBounds(fb.font['glyf']) if hasattr(g,'recalcBounds') else None
    lsb=getattr(g,'xMin',0) if g.numberOfContours else 0
    hm[nm]=(adv,lsb)
fb.setupHorizontalMetrics(hm)
fb.setupHorizontalHeader(ascent=860,descent=-200)
fb.setupNameTable({'familyName':'MoonGrunge','styleName':'Regular'})
fb.setupOS2(sTypoAscender=860,sTypoDescender=-200,usWinAscent=900,usWinDescent=220,sCapHeight=CAP,sxHeight=CAP)
fb.setupPost()
fb.save('MoonGrunge.ttf')
import os;print('saved',os.path.getsize('MoonGrunge.ttf'),'bytes;',len(order),'glyphs')
