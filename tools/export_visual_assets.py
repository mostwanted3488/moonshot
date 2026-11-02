import asyncio, base64, json, sys
from playwright.async_api import async_playwright
OUT='/home/claude/assets_export/'
def save(name,b64):
    open(OUT+name,'wb').write(base64.b64decode(b64.split(',',1)[1]))
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch(); pg=await b.new_page(viewport={'width':1000,'height':640})
        errs=[]; pg.on('pageerror',lambda e: errs.append(str(e)))
        await pg.goto('file:///home/claude/moonshot_y2k.html'); await pg.wait_for_timeout(900)
        # ---- background (black + lunar ground), 2x
        bg=await pg.evaluate('''()=>{const c=document.createElement('canvas');c.width=1784;c.height=1064;const x=c.getContext('2d');
          x.fillStyle='#000';x.fillRect(0,0,1784,1064);ctx=x;ctx.setTransform(2,0,0,2,0,0);ctx.lineJoin='round';ctx.lineCap='round';drawMoon();ctx=mainCtx;
          return c.toDataURL('image/jpeg',0.93)}''')
        save('bg.jpg',bg)
        # ---- ball sprite
        ball=await pg.evaluate('''()=>{const c=document.createElement('canvas');c.width=160;c.height=160;const x=c.getContext('2d');
          ctx=x;ctx.setTransform(2,0,0,2,0,0);ctx.lineJoin='round';ctx.lineCap='round';S.state='idle';drawBall(40,40,30,0,0);ctx=mainCtx;return c.toDataURL('image/png')}''')
        save('ball.png',ball)
        # ---- player frames at 1.5x
        n=201
        for i in range(n):
            v=i/(n-1)
            d=await pg.evaluate('''(v)=>{const sc=1.5,c=document.createElement('canvas');c.width=Math.round(W*sc);c.height=Math.round(H*sc);const x=c.getContext('2d');
              ctx=x;ctx.setTransform(sc,0,0,sc,0,0);ctx.lineJoin='round';ctx.lineCap='round';
              S.dv=v;S.state='idle';S.noShadow=false;
              playerOutline(0);ctx.save();ctx.translate(OX,OY);ctx.rotate(-.025);ctx.translate(-OX,-OY);drawPlayer(0,S.dv);ctx.restore();
              ctx=mainCtx;S.dv=0;return c.toDataURL('image/png')}''',v)
            save(f'full_{i:03d}.png',d)
        print('errs',errs); await b.close()
asyncio.run(main())
