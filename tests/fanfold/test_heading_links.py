"""Exercise fragment clicks against the shipped editor and renderer, not a DOM mock."""
import argparse
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import sys
import threading

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print('SKIP: playwright and Chromium are required')
    sys.exit(77)

ROOT = Path(__file__).resolve().parents[2]
HEADINGS = [('Work Done', 'work-done'), ('Hours and Tokens', 'hours-and-tokens'),
            ('Current Focus: Weed Letters', 'current-focus-weed-letters'),
            ('🚀 Launch', '-launch'), ('X', 'x'), ('X', 'x-1'),
            ('X-1', 'x-1-1'), ('Last Section', 'last-section')]
OTHER_LINKS = ['Other.md', 'Other.md#section', 'https://example.com/path', 'mailto:test@example.com', 'Assets/file.pdf']
MARKDOWN = '# Heading links\n\n' + ' · '.join(f'[{text}](#{slug})' for text, slug in HEADINGS) + '\n\n[Missing](#nothing)\n\n' + ' · '.join(f'[Other {i}]({href})' for i, href in enumerate(OTHER_LINKS)) + '\n\n' + '\n\n'.join(
    f'## {text}\n\n' + ('Paragraph of ordinary note content.\n\n' * 12)
    for text, slug in HEADINGS)


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def translate_path(self, path):
        path = path.replace('/src/shell/qml/vendor/package/', '/third_party/vditor-3.11.2/')
        return super().translate_path(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Handler, directory=str(ROOT)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f'http://127.0.0.1:{server.server_port}'
    results = []
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(executable_path=os.environ.get('FANFOLD_TEST_CHROMIUM'))
            for mode in ('ir-focused', 'ir-rendered', 'wysiwyg', 'preview'):
                page = browser.new_page(viewport={'width': 582, 'height': 620})
                page.route('**/editor.js', lambda route: route.abort())
                page.goto(origin + '/src/shell/qml/editor.html?note=0')
                # Only the native bridge is replaced; editor.js is unmodified.
                page.evaluate('''markdown=>{
                    window.fixtures=[markdown]; window.notes={collapse(){}};
                    window.fan={active:0,states:[{url:'file:///notes/Source.md'}],changed(){window.changes++},
                        remember(){},onReady(i,e){window.testEditor=e;window.ready=true}};
                    window.changes=0;
                }''', MARKDOWN)
                if mode == 'wysiwyg':
                    page.evaluate('''()=>{const Original=Vditor;
                        window.Vditor=function(id,options){return new Original(id,{...options,mode:'wysiwyg'})};
                    }''')
                page.add_script_tag(url=origin + '/src/shell/qml/editor.js?test')
                page.wait_for_function('window.ready')
                if mode == 'preview':
                    page.evaluate('''async markdown=>{
                        document.querySelector('#editor').style.display='none';
                        const root=document.createElement('div');root.id='rendered';root.className='vditor-reset';
                        document.body.append(root);
                        await Vditor.preview(root,markdown,{cdn:new URL('vendor/package',location.href).href,
                            lang:'en_US',hljs:{enable:false}});
                    }''', MARKDOWN)
                root = '.vditor-ir .vditor-reset' if mode.startswith('ir') else '.vditor-wysiwyg .vditor-reset' if mode == 'wysiwyg' else '#rendered'
                page.wait_for_selector(root + ' h2')
                before = page.evaluate('testEditor.getValue()')
                page.evaluate('''()=>{window.escaped=[];window.open=url=>escaped.push(url);
                    document.addEventListener('click',e=>{const a=e.target.closest('a');
                        if(a&&!e.defaultPrevented){escaped.push(a.getAttribute('href'));e.preventDefault()} });}''')
                checks=[]
                for i, (_, slug) in enumerate(HEADINGS):
                    if mode == 'ir-focused':
                        page.locator(root + ' p').last.click()
                    elif mode == 'ir-rendered':
                        page.evaluate('document.activeElement.blur()')
                    focused = page.locator(root).evaluate('e=>e === document.activeElement')
                    if mode.startswith('ir'):
                        assert focused == (mode == 'ir-focused')
                    link = page.locator(root + (' .vditor-ir__link' if mode.startswith('ir') else ' a')).nth(i)
                    link.scroll_into_view_if_needed()
                    page.screenshot(path=str(args.evidence / f'{mode}-{i}-before.png'))
                    link.click()
                    metric = page.locator(root + ' h2').nth(i).evaluate('e=>({top:e.getBoundingClientRect().top,bottom:e.getBoundingClientRect().bottom})')
                    page.screenshot(path=str(args.evidence / f'{mode}-{i}-after.png'))
                    checks.append({'slug': slug, 'focusedBeforeClick': focused, **metric, 'visible': -1 <= metric['top'] < 620})
                missing=page.locator(root + (' .vditor-ir__link' if mode.startswith('ir') else ' a')).nth(8)
                missing.scroll_into_view_if_needed()
                position=missing.bounding_box()
                missing.click()
                unchanged=page.evaluate('testEditor.getValue()') == before
                escaped=page.evaluate('escaped')
                missing_stayed = missing.bounding_box() == position
                for i in range(len(OTHER_LINKS)):
                    page.locator(root + (' .vditor-ir__link' if mode.startswith('ir') else ' a')).nth(9+i).click()
                forwarded = page.evaluate('escaped').copy()
                del forwarded[:len(escaped)]
                expected = [
                    # Preserve the renderer's existing scheme handling as well as paths.
                    ('file:///notes/' + href if '://' not in href and not href.startswith('mailto:') else href)
                    if mode != 'wysiwyg' else ('file:///notes/' + href if '://' not in href else href)
                    for href in OTHER_LINKS]
                result=dict(mode=mode,checks=checks,unchanged=unchanged,escaped=escaped,
                            changes=page.evaluate('changes'),missingStayed=missing_stayed,
                            otherLinksForwarded=set(forwarded)==set(expected),forwarded=forwarded)
                results.append(result)
                page.close()
            browser.close()
    finally:
        server.shutdown();server.server_close()
    (args.evidence/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    failures=[r for r in results if not (all(c['visible'] for c in r['checks']) and r['unchanged'] and not r['escaped'] and r['missingStayed'] and r['otherLinksForwarded'] and r['changes'] == 0)]
    print(f'{len(results)} modes, {len(failures)} failed; see {args.evidence}/results.json')
    sys.exit(bool(failures))


if __name__ == '__main__':
    main()
