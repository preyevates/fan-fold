"""Browser layout regression; run with a Python that has playwright + Chromium.

This exercises the shipped editor CSS and renderer, not desktop acceptance.
"""
import argparse
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading
import sys

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print('SKIP: install playwright and its Chromium browser to run table layout tests')
    sys.exit(77)

ROOT = Path(__file__).resolve().parents[2]
DESCRIPTION = (
    "This station keeps a careful record of every scheduled inspection and repair. "
    "The engineer checks the incoming reports before the morning shift begins, "
    "then records the results for the next person on duty. Any equipment that "
    "needs attention stays on the list until the replacement has arrived and "
    "the final safety checks are complete."
)
TABLES = {
    "long": f"| Name | Time | Count | Description |\n| --- | --- | --- | --- |\n| Alpha | 9.8 h | 1,506 | {DESCRIPTION} |\n| Beta | 2.1 h | 304 | {DESCRIPTION} |\n",
    "short": "| Name | Time | Count | State |\n| --- | --- | --- | --- |\n| Alpha | 9.8 h | 1,506 | Ready |\n| Beta | 2.1 h | 304 | Done |\n",
    "wide": "| " + " | ".join(["UnbreakableIdentifier" * 3] * 4) + " |\n| --- | --- | --- | --- |\n| a | b | c | d |\n",
}


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--baseline-css", type=Path)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    theme = (ROOT / "src/shell/qml/editor-theme.css").read_text()
    baseline = args.baseline_css.read_text() if args.baseline_css else (
        theme + '\n.vditor-reset table td,.vditor-reset table th{white-space:nowrap}'
    )
    server = ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=str(ROOT)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    origin = f"http://127.0.0.1:{server.server_port}"
    results, failures = [], []
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            page = browser.new_page()
            for width in (582, 422):  # NoteCard editor width = card (640 / 480) - 58.
                for mode in ("ir", "wysiwyg", "preview"):
                    for case, markdown in TABLES.items():
                        page.set_viewport_size({"width": width, "height": 700})
                        page.goto(origin + "/README.md")
                        # Load the real vendor assets, but not the native WebChannel bridge.
                        page.set_content('<html><head></head><body><div id=editor></div></body></html>')
                        page.add_style_tag(url=origin + '/third_party/vditor-3.11.2/dist/index.css')
                        page.add_style_tag(content=theme)
                        page.add_script_tag(url=origin + '/third_party/vditor-3.11.2/dist/index.min.js')
                        page.evaluate("""async ({origin, mode, markdown}) => {
                            window.ready = false;
                            const cdn = origin + '/third_party/vditor-3.11.2';
                            if (mode === 'preview') {
                                document.querySelector('#editor').className = 'vditor-reset';
                                await Vditor.preview(document.querySelector('#editor'), markdown,
                                    {cdn, lang:'en_US', hljs: {enable: false}});
                                window.ready = true;
                            } else {
                                window.probeEditor = new Vditor('editor', {cdn, mode,
                                    lang:'en_US', height:'100%', value:markdown,
                                    cache:{enable:false}, toolbar:[],
                                    preview:{hljs:{enable:false}},
                                    after:()=>{window.ready=true;}});
                            }
                        }""", {"origin": origin, "mode": mode, "markdown": markdown})
                        page.wait_for_function("window.ready && document.querySelector('table td')")
                        page.evaluate("document.fonts.ready")
                        # Record geometry and paint with the old and new CSS on the same DOM.
                        old = page.add_style_tag(content=baseline)
                        before = measure(page)
                        page.screenshot(path=str(args.evidence / f"before-{width}-{mode}-{case}.png"))
                        old.evaluate("element => element.remove()")
                        after = measure(page)
                        page.screenshot(path=str(args.evidence / f"after-{width}-{mode}-{case}.png"))
                        checks = {"paint": before["paint"] == after["paint"]}
                        if case == "long":
                            checks.update(fits=after["scrollWidth"] <= after["clientWidth"] + 1,
                                          wraps=after["lines"][3] > 1,
                                          numbers=after["lines"][1:3] == [1, 1])
                        elif case == "short":
                            checks["unchanged"] = before == after
                        else:
                            checks.update(overflows=after["scrollWidth"] > after["clientWidth"],
                                          scrollable=after["scrollLeft"] > 0)
                        result = dict(width=width, mode=mode, case=case,
                                      before=before, after=after, checks=checks)
                        results.append(result)
                        if not all(checks.values()):
                            failures.append(f"{width}/{mode}/{case}: {checks}")
            browser.close()
    finally:
        server.shutdown()
        server.server_close()
    (args.evidence / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(f"{len(results)} layout cases; {len(failures)} failures")
    if failures:
        raise SystemExit("\n".join(failures))


def measure(page):
    return page.evaluate("""() => {
        const t = document.querySelector('table');
        const cells = [...t.querySelectorAll('tbody tr:first-child td')];
        const lineCount = cell => {
            const r = document.createRange(); r.selectNodeContents(cell);
            return new Set([...r.getClientRects()].filter(r=>r.width>0).map(r=>Math.round(r.top))).size;
        };
        t.scrollLeft = 10000;
        const scrollLeft = t.scrollLeft; t.scrollLeft = 0;
        return {clientWidth:t.clientWidth, scrollWidth:t.scrollWidth, scrollLeft,
            lines:cells.map(lineCount),
            widths:cells.map(c=>c.getBoundingClientRect().width),
            paint:[...t.querySelectorAll('tr,td,th')].map(e=>{
                const s=getComputedStyle(e);return [s.color,s.backgroundColor,s.borderColor,s.borderWidth];
            })};
    }""")


if __name__ == "__main__":
    main()
