"""Click real renderer links with deck and pinned editor-slot identities."""
import argparse
import functools
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import sys
import threading

from test_heading_links import Handler
try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print('SKIP: playwright and Chromium are required')
    sys.exit(77)

ROOT = Path(__file__).resolve().parents[2]
NOTE_URL = 'file:///notes/Remediation%20%23%25/Source.md'
LINKS = [('Dashboard%20Deliberation.md', 'file:///notes/Remediation%20%23%25/Dashboard%20Deliberation.md'),
         ('Assets/report%20%231.pdf', 'file:///notes/Remediation%20%23%25/Assets/report%20%231.pdf'),
         ('../Other/x.md', 'file:///notes/Other/x.md'),
         ('file:///elsewhere/x.md', 'file:///elsewhere/x.md'),
         ('https://example.com/path', 'https://example.com/path')]
MARKDOWN = '# Relative links\n\n' + '\n\n'.join(f'Before [Link {i}]({href}) after' for i, (href, _) in enumerate(LINKS))


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
            for host in ('deck', 'pinned'):
                for mode in ('ir-focused', 'ir-rendered', 'wysiwyg', 'preview'):
                    page = browser.new_page(viewport={'width': 582, 'height': 620})
                    page.route('**/editor.js', lambda route: route.abort())
                    page.goto(origin + '/src/shell/qml/editor.html?note=0')
                    page.evaluate('''({markdown,url,host})=>{
                        window.fixtures=[markdown];window.notes={collapse(){}};
                        window.fan={active:0,states:[{id:host,url}],linkBase:'file:///notes/',
                            changed(){window.changes++},remember(){},
                            onReady(i,e){window.testEditor=e;window.ready=true}};
                        window.changes=0;window.opened=[];
                        window.open=url=>{opened.push(new URL(url,location.href).href);return null};
                        document.addEventListener('click',event=>{
                            const a=event.target.closest('a[href]');
                            if(a&&!event.defaultPrevented){opened.push(a.href);event.preventDefault()}
                        });
                    }''', {'markdown': MARKDOWN, 'url': NOTE_URL, 'host': host})
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
                    before = page.evaluate('testEditor.getValue()')
                    for i, (_, expected) in enumerate(LINKS):
                        if mode == 'ir-focused':
                            page.locator(root + ' h1').click()
                        elif mode == 'ir-rendered':
                            page.evaluate('document.activeElement.blur()')
                        selector = ' .vditor-ir__link' if mode.startswith('ir') else ' a'
                        page.locator(root + selector).nth(i).click()
                        actual = page.evaluate('opened.splice(0)')
                        results.append(dict(host=host, mode=mode, href=LINKS[i][0], expected=expected,
                                            actual=actual, passed=actual == [expected]))
                    if mode == 'ir-focused':
                        page.locator(root + ' p').first.click(position={'x': 2, 'y': 5})
                        page.keyboard.press('Home')
                        for _ in range(9):
                            page.keyboard.press('ArrowRight')
                        page.wait_for_function('document.querySelector(\'[data-type="a"].vditor-ir__node--expand\')')
                        page.evaluate('opened.splice(0)')
                        page.locator(root + ' .vditor-ir__link').first.click()
                        assert page.evaluate('opened.splice(0)') == [], 'editing a link must not open it'
                    for href in ('javascript:alert(1)', 'data:text/html,unsafe'):
                        page.locator(root).evaluate('''(root,href)=>{
                            const a=document.createElement('a');a.href=href;a.textContent='Unsafe';
                            root.append(a);
                        }''', href)
                        page.locator(root + ' a').last.click()
                        assert page.evaluate('opened.splice(0)') == []
                        page.locator(root + ' a').last.evaluate('a=>a.remove()')
                    assert page.evaluate('testEditor.getValue()') == before
                    assert page.evaluate('changes') == 0
                    page.close()
            page = browser.new_page()
            page.route('**/editor.js', lambda route: route.abort())
            page.goto(origin + '/src/shell/qml/editor.html?note=0')
            page.evaluate('''()=>{
                window.fixtures=[''];window.notes={collapse(){}};
                window.fan={active:0,states:[{}],changed(){},remember(){},
                    onReady(i,e){window.ready=true}};
            }''')
            page.add_script_tag(url=origin + '/src/shell/qml/editor.js?failed-load')
            page.wait_for_function('window.ready', timeout=3000)
            page.close()
            browser.close()
    finally:
        server.shutdown();server.server_close()
    (args.evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    failures = [r for r in results if not r['passed']]
    for result in failures:
        print(json.dumps(result))
    print(f'{len(results)} link clicks, {len(failures)} failed')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
