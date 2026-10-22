# Note-relative links

An editor slot carries the native note's fully encoded `file:` URL from `store.load`.
The shared editor resolves authored relative links with the browser's `URL` implementation
before forwarding them through its existing new-window request. This preserves `../`, encoded
spaces and literal `#`/`%` in file names without treating the application assets directory as
the library. The renderer receives the same note directory as its asset base. Imports still live under the
library's `Assets/`; the store supplies the relative path back to that root so new file,
recording and icon links inserted into nested notes use `../Assets/...` (or the required
number of parent directories). Each filename segment is URL-encoded separately.
The CSP and
native navigation locks remain unchanged; script and data links are not forwarded.

The deck opens a note internally only when the decoded absolute path matches a document in
the current deck. Other notes, including sibling-folder links, go to the desktop with an
absolute file URL, as do assets. A same-named file elsewhere must not select a local note.
This deliberately does not add cross-note fragment navigation.

## Regression checks

- `test_relative_links.py`: real renderer clicks in focused and rendered IR, WYSIWYG and
  preview, with deck/pinned slot fixtures. Checks note, asset, parent-folder and absolute URLs,
  plus unchanged editor bytes. This replaces the native bridge only, not the renderer.
- `test_note_link_routing.js`: executes the actual deck function, checking exact path identity,
  same-name sibling collisions, asset forwarding and blocked schemes.
- `loadReportsEncodedNoteUrl` in `tst_externalediting`: native engine/store contract, including
  encoded space, hash and percent characters in a nested folder.
- `test_heading_links.py`: existing same-note fragment navigation stays in the editor.

The browser checks do not prove native WebChannel startup or desktop handoff. Before release,
run the native suite and click the extracted package's deck and pinned windows in an isolated
compositor, with disposable notes and a private desktop URL handler. Never use the real library.

Browser tests accept `FANFOLD_TEST_CHROMIUM` for an already-installed test browser; otherwise
Playwright uses its normal browser discovery.
