/** Parent page for ONE pinned note's window.
 *
 * `editor.js` talks to its parent page through a fixed contract:
 * `parent.fixtures[index]` for the initial text,
 * `parent.fan.changed/onReady/save/remember/suspend/active`, and `parent.notes.collapse`.
 * `app.js` implements that contract for a deck of N notes inside one page.
 *
 * This file implements the SAME contract for exactly one note, so a pinned window reuses
 * the identical editor, stylesheet and Markdown pipeline rather than a second, divergent
 * editor implementation. `index` is always 0 here.
 *
 * Deliberately NOT duplicated: the fan, the manifest, the palette and the appearance
 * panel. A pinned window is one note in a plain window; the card's chrome belongs to the
 * dock.
 */
"use strict";
const noteId = new URLSearchParams(location.search).get("id") || "";
let pinned;
const fan = window.fan = {editors: [], frames: [], active: 0, state: null};
window.fixtures = [];

fan.call = (method, ...args) => new Promise(resolve => pinned[method](...args, resolve));

/** Mirror of app.js's two-word routine status, on the one note this window holds. */
fan.status = () => {
    const s = fan.state;
    return s.dirty ? (s.external ? "CONFLICT · external file changed; edits kept" : "Unsaved")
                   : (s.external ? "External change · reopen in the fan to reload" : "Saved");
};
fan.publish = () => pinned.status(fan.state.status, !!fan.state.dirty);

/** Every keystroke reaches the engine, which owns the 250 ms debounce and the recovery
 *  journal. Identical to the fan's autosave seam: a pinned note must not have weaker
 *  save guarantees than the same note in the deck.
 *
 *  `pushed` records what was actually handed over, for the same reason as in app.js:
 *  re-baselining to the live editor value on commit would mark text typed since the push
 *  as saved when the file does not contain it. */
fan.changed = () => {
    const s = fan.state;
    if (!s.loaded) return;
    const text = fan.editors[0].getValue();
    s.dirty = text !== s.baseline;
    s.status = fan.status();
    fan.publish();
    if (s.dirty && !s.busy) { s.pushed = text; pinned.noteEdited(noteId, text); }
};

fan.save = async () => {
    const s = fan.state;
    if (!s.loaded || s.busy) return;
    fan.changed();
    if (!s.dirty) return;
    const expected = s.revision, text = fan.editors[0].getValue();
    s.busy = true; s.status = "Unsaved"; fan.publish();
    const r = await fan.call("saveNote", noteId, text, expected);
    s.busy = false;
    if (r.ok) {
        s.revision = r.revision; s.baseline = text; s.external = false;
        s.dirty = fan.editors[0].getValue() !== text; s.status = fan.status();
    } else { s.status = r.error; s.dirty = true; }
    fan.publish();
    return r;
};

/** Reconciliation, on the same engine-authoritative terms as the deck: `committed` means
 *  our own autosave landed and its revision should be adopted, not read as a stranger. */
fan.poll = async () => {
    const s = fan.state;
    if (!s || !s.loaded || s.busy) return;
    const r = await fan.call("probeNote", noteId);
    if (!r.ok) { s.status = r.error; fan.publish(); return; }
    if (r.conflict) { s.external = true; s.status = fan.status(); fan.publish(); return; }
    if (r.committed) {
        // Re-baseline to what was COMMITTED, not to the live buffer; then re-check, so a
        // tail typed during the write is pushed rather than declared clean. See app.js.
        s.revision = r.revision; s.external = false;
        s.baseline = (s.pushed !== undefined) ? s.pushed : fan.editors[0].getValue();
        s.dirty = false;
        s.status = fan.status();
        fan.publish();
        fan.changed();
        return;
    }
    if (r.revision !== s.revision) { s.external = true; }
    s.status = fan.status();
    fan.publish();
};

fan.remember = () => {};
fan.suspend = () => { fan.frames[0]?.contentDocument.activeElement?.blur(); };
fan.select = () => { fan.frames[0]?.contentWindow.focus(); fan.editors[0]?.focus(); };
fan.formattingVisible = () =>
    fan.frames[0].contentDocument.documentElement.classList.contains("formatting");
fan.toggleFormatting = () => {
    fan.frames[0].contentDocument.documentElement.classList.toggle("formatting");
    fan.select();
};
fan.onReady = (index, editor) => {
    fan.editors[0] = editor;
    const s = fan.state;
    if (s.revision) { s.baseline = editor.getValue(); s.loaded = true; s.dirty = false; s.status = fan.status(); }
    else fan.frames[0].contentDocument.getElementById("editor").inert = true;
    fan.publish();
    // Release the construction promise. editor.js calls this from Vditor's `after` hook,
    // i.e. once the editor exists and has taken up parent.fixtures[0], which is the only
    // moment at which this window is genuinely usable.
    if (fan.resolveReady) { const done = fan.resolveReady; fan.resolveReady = null; done(); }
};

/** editor.js calls parent.notes.collapse() on Escape. In a pinned window the equivalent
 *  gesture is closing the window, which returns the note to the fan and never deletes. */
window.notes = {collapse: () => pinned.closeWindow()};

/** Paint the note's own paper and ink, and the global typography, exactly as the deck
 *  does — the same CSS custom properties editor-theme.css already reads. */
fan.applyColours = record => {
    const style = fan.frames[0]?.contentDocument.documentElement.style;
    if (!style || !record) return;
    Object.entries(record).forEach(([k, v]) => style.setProperty("--" + k, v));
};

new QWebChannel(qt.webChannelTransport, async channel => {
    pinned = window.pinnedBridge = channel.objects.pinned;
    // Progress is reported through the BRIDGE, not console.log: WebEngine console output
    // from this window does not reach the application log, so a failure inside this
    // callback is otherwise completely silent.
    pinned.status("loading", false);
    const r = await fan.call("loadNote", noteId);
    if (!r || !r.ok) { pinned.status("load failed: " + (r && r.error ? r.error : "no result"), false); return; }
    pinned.status("loaded " + (r.text ? r.text.length : 0) + " chars", false);
    fan.state = {loaded: false, dirty: false, busy: false, external: false,
                 revision: r.ok ? r.revision : "", baseline: "",
                 status: r.ok ? "Saved" : r.error};
    fixtures[0] = r.ok ? r.text : "";
    await new Promise(resolve => {
        // Resolve on Vditor's OWN ready callback (editor.js calls parent.fan.onReady),
        // never on the iframe's `load` event. `load` fires when the document is parsed,
        // which is BEFORE the editor is constructed and before it has read
        // parent.fixtures[0]; continuing there leaves the window painted, titled and
        // footered around a permanently EMPTY body while the note's text sits on disk.
        fan.resolveReady = resolve;
        const frame = document.createElement("iframe");
        fan.frames[0] = frame;
        frame.setAttribute("aria-label", r.filename || noteId);
        frame.src = "editor.html?note=0";
        document.body.append(frame);
    });
    fan.applyColours(await fan.call("colours", noteId));
    fan.select();
    setInterval(fan.poll, 1000);
});

/** Re-read this note's colours and the global typography from the bridge and repaint.
 *  The deck calls this (through QML) when appearance settings or this note's own colour
 *  change while the window is open; without it a pinned window holds stale styling until
 *  it is closed and reopened. Appearance only — text, caret and undo are untouched. */
fan.recolour = async () => {
    if (!pinned) return;
    fan.applyColours(await fan.call("colours", noteId));
};
