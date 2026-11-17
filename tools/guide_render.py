# Renders guide pages (.dc.html copies of the guide artifact's pages, with <img src> pointing at local
# JPGs, e.g. pulled from the current PDF with `pdfimages -j`) to one-page PDFs, using static Fredoka and
# Nunito instances in fonts/ (from google/fonts, cut with `fonttools varLib.instancer --static`; variable
# fonts come out as bulky Type 3). Join pages with qpdf: firmware/main/guide/pocket-pony-guide.pdf.
# Usage: python3 guide_render.py Care Extras   (run where the pages, images and fonts/ are)
import re, sys
from playwright.sync_api import sync_playwright
head = '''<!doctype html><html><head><meta charset="utf-8"><style>
@font-face{font-family:'Fredoka';src:url(fonts/Fredoka-600.ttf);font-weight:500 600}
@font-face{font-family:'Fredoka';src:url(fonts/Fredoka-700.ttf);font-weight:700}
@font-face{font-family:'Nunito';src:url(fonts/Nunito-400.ttf);font-weight:400}
@font-face{font-family:'Nunito';src:url(fonts/Nunito-700.ttf);font-weight:600 700}
@font-face{font-family:'Nunito';src:url(fonts/Nunito-800.ttf);font-weight:800 900}
@page{size:816px 1056px;margin:0} html,body{margin:0} a{color:#9E2B23}</style></head><body>'''
with sync_playwright() as p:
    b = p.chromium.launch(executable_path='/usr/bin/google-chrome')
    pg = b.new_page(viewport={'width':816,'height':1056})
    for name in sys.argv[1:]:
        body = re.search(r'<x-dc>(.*)</x-dc>', open(name+'.dc.html').read(), re.S).group(1)
        open(name+'.html','w').write(head+body+'</body></html>')
        pg.goto('file://'+__import__('os').path.abspath(name+'.html')); pg.wait_for_timeout(500)
        pg.evaluate("document.fonts.ready")
        print(name, 'overflow' if pg.evaluate("document.body.firstElementChild.scrollHeight") > 1056 else 'fits')
        pg.pdf(path=name+'.pdf', width='816px', height='1056px', print_background=True)
        pg.screenshot(path=name+'.png')
    b.close()
