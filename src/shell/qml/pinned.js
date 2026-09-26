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
    s.status = s.failedPush ? "Recovery write failed: latest edits may exist only in memory; keep this window open" : fan.status();
    fan.publish();
    // Undo can clean the UI before native autosave fires; the prior edit is still
    // pending there, so forward the reversion too.
    if (!s.busy && (s.dirty || (s.pushed !== undefined && s.pushed !== text))) {
        s.pushed = text;
        s.pendingPushes = (s.pendingPushes || 0) + 1;
        if (!s.pushPromises) s.pushPromises = new Set();
        const push = fan.call("noteEdited", noteId, text).then(ok => {
            s.pendingPushes--;
            if (ok === true) {
                s.lastAcknowledgedPush = text;
                if (s.pushed === text) {
                    s.failedPush = false;
                    s.status = fan.status();
                    fan.publish();
                }
            }
            if (ok === false && s.pushed === text) {
                s.failedPush = true;
                s.status = "Recovery write failed: latest edits may exist only in memory; keep this window open";
                s.dirty = true;
                fan.publish();
            }
            return ok;
        });
        s.pushPromises.add(push);
        push.then(() => s.pushPromises.delete(push));
        s.pendingPush = push;
    }
};

fan.save = async (force = false) => {
    const s = fan.state;
    if (!s.loaded) return {ok: false, error: "Note not ready"};
    if (s.busy) return s.busySave;
    fan.changed();
    if (!s.dirty && !force) return {ok: true};
    const expected = s.revision, text = fan.editors[0].getValue();
    s.busy = true; s.status = "Unsaved"; fan.publish();
    s.busySave = fan.call("saveNote", noteId, text, expected);
    const r = await s.busySave;
    s.busy = false; s.busySave = null;
    if (r.ok) {
        s.failedPush = false;
        s.revision = r.revision; s.baseline = text; s.external = false;
        // A successful explicit save reasserts this value after older push acknowledgments.
        s.lastAcknowledgedPush = text;
        s.dirty = fan.editors[0].getValue() !== text; s.status = fan.status();
        if (s.dirty) fan.changed(); // tail typed while the bridge was busy
    } else { s.status = r.error; s.dirty = true; }
    fan.publish();
    return r;
};

fan.closeReady = () => {
    const s = fan.state;
    if (!s || !s.loaded || s.busy || s.pendingPushes > 0 || s.failedPush || s.status?.includes("failed")) return false;
    const text = fan.editors[0]?.getValue();
    if (text === undefined) return false;
    if (text !== s.pushed && text !== s.baseline) { fan.changed(); return false; }
    if (s.lastAcknowledgedPush !== undefined && s.lastAcknowledgedPush !== text) {
        s.pushed = null;
        fan.changed();
        return false;
    }
    return true;
};

/** Do not return a pinned window to the fan until the *latest* editor value is
 * persisted. Lock input before yielding to WebChannel; on failure leave it open. */
fan.closeSafely = async (generation = fan.closeGeneration) => {
    const s = fan.state;
    if (!s || !s.loaded) return false;
    const current = () => generation === fan.closeGeneration;
    const editorElement = fan.frames[0]?.contentDocument.getElementById("editor");
    const refuse = () => { if (current() && editorElement) editorElement.inert = false; return false; };
    if (editorElement) editorElement.inert = true;
    if (s.busy) await s.busySave;
    if (!current()) return false;
    fan.changed();
    if (s.dirty) {
        const r = await fan.save();
        if (!current()) return false;
        if (!r || !r.ok) return refuse();
    }
    const pending = [...(s.pushPromises || [])];
    const acknowledgments = await Promise.all(pending);
    if (!current()) return false;
    // WebChannel calls can complete out of order. Even if the newest push
    // succeeded, a late older push may now be the native autosave buffer.
    if (acknowledgments.includes(false)
        || (s.lastAcknowledgedPush !== undefined
            && s.lastAcknowledgedPush !== fan.editors[0].getValue())) {
        const r = await fan.save(true);
        if (!current()) return false;
        if (!r || !r.ok) return refuse();
    }
    if (!fan.closeReady() || fan.editors[0].getValue() !== s.baseline || s.dirty)
        return refuse();
    return true;
};

// WebEngineView.runJavaScript cannot return a Promise. Start the asynchronous bridge
// write in the page and expose only a primitive acknowledgment for QML to poll.
fan.closeResult = 0;
fan.closeGeneration = 0;
fan.beginClose = () => {
    const generation = ++fan.closeGeneration;
    fan.closeResult = 0;
    fan.closeSafely(generation).then(ok => {
        if (generation === fan.closeGeneration) fan.closeResult = ok === true ? 1 : -1;
    }, () => {
        if (generation === fan.closeGeneration) { fan.cancelClose(); fan.closeResult = -1; }
    });
    return true;
};
fan.cancelClose = () => {
    ++fan.closeGeneration;
    fan.closeResult = -1;
    const editorElement = fan.frames[0]?.contentDocument.getElementById("editor");
    if (editorElement) editorElement.inert = false;
};

/** Reconciliation, on the same engine-authoritative terms as the deck: `committed` means
 *  our own autosave landed and its revision should be adopted, not read as a stranger. */
fan.poll = async () => {
    const s = fan.state;
    if (!s || !s.loaded || s.busy) return;
    const pushedAtProbe = s.pushed;
    const r = await fan.call("probeNote", noteId);
    if (!r.ok) { s.status = r.error; fan.publish(); return; }
    if (r.saveError && r.saveError.includes("Recovery write failed")) {
        s.status = r.saveError; s.dirty = true; fan.publish(); return;
    }
    if (s.failedPush) {
        s.status = "Recovery write failed: latest edits may exist only in memory; keep this window open";
        s.dirty = true; fan.publish(); return;
    }
    if (r.conflict) { s.external = true; s.status = fan.status(); fan.publish(); return; }
    if (r.committed) {
        // Re-baseline to what was COMMITTED, not to the live buffer; then re-check, so a
        // tail typed during the write is pushed rather than declared clean. See app.js.
        s.revision = r.revision; s.external = false;
        s.baseline = (pushedAtProbe !== undefined) ? pushedAtProbe : fan.editors[0].getValue();
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
