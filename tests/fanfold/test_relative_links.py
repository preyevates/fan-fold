"""Click real renderer links with deck and pinned editor-slot identities."""
import argparse
import functools
from http.server import ThreadingHTTPServer
import json
import io
import wave
from itertools import product
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
    legacy_results = []
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
                            call(method,id,href){return Promise.resolve(new URL(href,url).href)},
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
                        page.wait_for_function('opened.length>0')
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
            for host, legacy_mode in product(('deck', 'pinned'), ('ir', 'wysiwyg', 'sv')):
                page = browser.new_page()
                page.route('**/editor.js', lambda route: route.abort())
                page.route('**/notes/Assets/*.svg', lambda route: route.fulfill(
                    content_type='image/svg+xml', body='<svg xmlns="http://www.w3.org/2000/svg" width="17" height="13"/>'))
                page.route('**/notes/Nested/Assets/**', lambda route: route.fulfill(status=404, body=''))
                buffer = io.BytesIO()
                with wave.open(buffer, 'wb') as audio:
                    audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(8000)
                    audio.writeframes(b'\0\0' * 800)
                page.route('**/notes/Assets/old.wav', lambda route: route.fulfill(
                    content_type='audio/wav', body=buffer.getvalue()))
                page.goto(origin + '/src/shell/qml/editor.html?note=0')
                page.evaluate('''({origin,host,mode})=>{
                    const Original=Vditor;window.Vditor=function(id,options){return new Original(id,{...options,mode})};
                    window.fixtures=['![Legacy](Assets/old.svg)\\n\\n[Legacy file](Assets/old.svg)\\n\\n<audio controls src="Assets/old.wav"></audio>\\n\\n<audio controls><source src="Assets/old.wav"></audio>\\n\\n<video controls src="Assets/old.wav"></video>'];
                    window.notes={collapse(){}};window.opened=[];window.changes=0;
                    window.open=url=>{opened.push(url);return null};
                    window.fan={active:0,states:[{id:host,url:origin+'/notes/Nested/Source.md'}],
                        changed(){changes++},remember(){},onReady(i,e){window.testEditor=e;window.ready=true},
                        call(method,id,href){
                            if(method!=='resolveAsset'||id!==host)throw Error('wrong bridge call');
                            return Promise.resolve(origin+'/notes/Assets/'+(href.endsWith('.wav')?'old.wav':'old.svg'));
                        }};
                }''', {'origin': origin, 'host': host, 'mode': legacy_mode})
                page.add_script_tag(url=origin + '/src/shell/qml/editor.js?legacy')
                page.wait_for_function('window.ready')
                before = page.evaluate('testEditor.getValue()')
                root = '.vditor-ir' if legacy_mode == 'ir' else '.vditor-wysiwyg' if legacy_mode == 'wysiwyg' else '.vditor-preview'
                page.wait_for_function('''root=>{const image=document.querySelector(root+' img');
                    return image && image.complete && image.naturalWidth===17}''', arg=root, timeout=3000)
                page.wait_for_function('''root=>{const media=[...document.querySelectorAll(root+' audio,'+root+' video')];
                    return media.length===3 && media.every(m=>m.readyState>=1)}''', arg=root, timeout=3000)
                page.locator(root + (' .vditor-ir__link' if legacy_mode == 'ir' else ' a')).click()
                page.wait_for_function('opened.length===1')
                assert page.evaluate('opened.splice(0)') == [origin + '/notes/Assets/old.svg']
                assert page.evaluate('changes') == 0
                assert page.evaluate('testEditor.getValue()') == before
                page.locator('.vditor-ir .vditor-reset' if legacy_mode == 'ir' else '.vditor-wysiwyg .vditor-reset' if legacy_mode == 'wysiwyg' else '.vditor-sv').click(position={'x': 2, 'y': 5})
                page.keyboard.press('Control+End')
                page.keyboard.type(' sentinel')
                assert 'Assets/old.svg' in page.evaluate('testEditor.getValue()')
                assert '/notes/Assets/' not in page.evaluate('testEditor.getValue()')
                assert 'Assets/old.wav' in page.evaluate('testEditor.getValue()')
                authored = page.evaluate('testEditor.getValue()')
                for mode, key in [('wysiwyg', '7'), ('ir', '8'), ('sv', '9')]:
                    page.keyboard.press('Control+Alt+' + key)
                    page.wait_for_function('mode=>testEditor.vditor.currentMode===mode', arg=mode, timeout=3000)
                    actual = page.evaluate('testEditor.getValue()')
                    assert actual.rstrip('\n') == authored.rstrip('\n'), ('mode switch must not rewrite legacy paths', legacy_mode, mode, authored, actual)
                root = '.vditor-preview'
                page.route('**/notes/Nested/new.svg', lambda route: route.fulfill(
                    content_type='image/svg+xml', body='<svg xmlns="http://www.w3.org/2000/svg" width="23" height="13"/>'))
                page.locator(root+' img').first.evaluate('(i,url)=>i.src=url', origin+'/notes/Nested/new.svg')
                page.wait_for_function('root=>document.querySelector(root+" img").naturalWidth===23', arg=root, timeout=3000)
                legacy_results.append(dict(host=host, mode=legacy_mode, media_loaded=3, paths_preserved=True, changed_image_loaded=True))
                page.close()
            page = browser.new_page()
            page.route('**/editor.js', lambda route: route.abort())
            page.goto(origin + '/src/shell/qml/editor.html?note=0')
            page.evaluate('''()=>{
                window.fixtures=[''];window.notes={collapse(){}};
                window.fan={active:0,states:[{}],changed(){},remember(){},
                    onReady(i,e){window.ready=true;window.testEditor=e}};
            }''')
            page.add_script_tag(url=origin + '/src/shell/qml/editor.js?failed-load')
            page.wait_for_function('window.ready', timeout=3000)
            reload_source = (ROOT / 'src/shell/qml/app.js').read_text().split('fan.reload=async i=>{', 1)[1].split('/** Once-a-second', 1)[0]
            page.evaluate('()=>{fan.reload=async i=>{' + reload_source + '}')
            recovered_url = origin + '/notes/Nested/Recovered.md'
            page.route('**/notes/Nested/recovered.svg', lambda route: route.fulfill(
                content_type='image/svg+xml', body='<svg xmlns="http://www.w3.org/2000/svg" width="29" height="13"/>'))
            page.evaluate('''url=>{
                fan.frames=[{contentDocument:document,contentWindow:window}];fan.editors=[testEditor];fan.ranges=[];
                fan.states[0]={id:'recovered',loaded:false,dirty:false};fan.publish=()=>{};fan.status=()=>"Saved";
                fan.call=()=>Promise.resolve({ok:true,url,assetPrefix:'../',filename:'Recovered.md',revision:'r',
                    text:'[Recovered](Sibling.md)\\n\\n![Recovered](recovered.svg)'});
                window.opened=[];window.open=url=>{opened.push(url);return null};
            }''', recovered_url)
            page.evaluate('fan.reload(0)')
            assert page.evaluate('fan.states[0].url') == recovered_url, 'recovered note must receive native URL'
            assert page.evaluate('fan.states[0].assetPrefix') == '../'
            page.wait_for_function('document.querySelector(".vditor-ir img").naturalWidth===29', timeout=3000)
            page.locator('.vditor-ir__link').click()
            assert page.evaluate('opened.splice(0)') == [origin + '/notes/Nested/Sibling.md']
            page.close()
            browser.close()
    finally:
        server.shutdown();server.server_close()
    (args.evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    (args.evidence / 'legacy-results.json').write_text(json.dumps(dict(contexts=legacy_results, failed_load_recovery=True), indent=2) + '\n')
    failures = [r for r in results if not r['passed']]
    for result in failures:
        print(json.dumps(result))
    print(f'{len(results)} link clicks, {len(failures)} failed')
    print(f'{len(legacy_results)} legacy media/edit/mode-switch contexts passed; failed-load recovery passed')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
