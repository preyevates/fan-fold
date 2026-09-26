// Exercise the actual Main.qml navigation handlers against delayed WebChannel acknowledgments.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.resolve(__dirname, '../../src/shell/qml/Main.qml'), 'utf8');
function body(name) {
    const match = source.match(new RegExp('function ' + name + '\\([^\\n]*?\\) \\{([\\s\\S]*?)\\n    \\}'));
    assert.ok(match, `missing ${name}`);
    return match[1];
}
function setup() {
    let deckAck, pinAck, flushes = 0, switches = 0, scoped = 0;
    const deck = {enabled: true, runJavaScript(_script, cb) {deckAck = cb;}};
    const pin = {editorEnabled: true, appCloseReady(cb) {pinAck = cb;}};
    const dialog = {openFolder: 'Work', searchActive: false, selected: 1, saveStatus: 'Saved',
        expanded: true, order: ['old'], collapse() {this.expanded = false;},
        applyManifest(result) {this.openFolder = result.openFolder;},
        clearSearch() {throw Error('search state changed before gate');}};
    const collection = {lastError: 'Recovery write failed', flushPendingSaves() {flushes++; return true;}};
    const notesStore = {openFolder(folder) {
        if (!collection.flushPendingSaves()) return {ok: false, error: collection.lastError};
        scoped++; return {ok: true, openFolder: folder};
    }, load() {return {openFolder: ''};}};
    const shellControl = {openFolder() {switches++; return '';}};
    const loader = {item: deck};
    const pinnedWindows = {count: 1, objectAt: () => pin};
    const alignment = {restart() {}};
    const context = vm.createContext({dialog, notesStore, collection, shellControl, loader, pinnedWindows, alignment,
        applyManifest: result => dialog.applyManifest(result)});
    dialog.closeGateTimer = {start() {}, stop() {}, fire() {if (dialog.closeGateAbort) dialog.closeGateAbort();}};
    dialog.checkEditorsForClose = vm.runInContext(`(function checkEditorsForClose(excludedId, done) {${body('checkEditorsForClose')}})`, context);
    dialog.scopeToFolder = vm.runInContext(`(function scopeToFolder(folder, done) {${body('scopeToFolder')}})`, context);
    dialog.switchRootFolder = vm.runInContext(`(function switchRootFolder(folder) {${body('switchRootFolder')}})`, context);
    return {dialog, deck, pin, collection, shellControl, get deckAck(){return deckAck}, get pinAck(){return pinAck},
        get flushes(){return flushes}, get switches(){return switches}, get scoped(){return scoped}};
}
function assertFrozen(s) {assert.equal(s.deck.enabled, false); assert.equal(s.pin.editorEnabled, false);}
function assertReleased(s) {assert.equal(s.deck.enabled, true); assert.equal(s.pin.editorEnabled, true);}
function folder() {
    let s = setup(), result;
    s.dialog.scopeToFolder('Other', ok => {result = ok;});
    assertFrozen(s);
    assert.equal(s.scoped, 0); assert.equal(s.dialog.openFolder, 'Work');
    s.dialog.scopeToFolder('Third', () => {throw Error('overlapping folder accepted');});
    s.dialog.switchRootFolder('new');
    assert.equal(s.switches, 0);
    s.deckAck(true); s.pinAck(true);
    assert.equal(result, true); assert.equal(s.scoped, 1); assert.equal(s.flushes, 1);
    assert.equal(s.dialog.openFolder, 'Other'); assertReleased(s);
    s = setup(); s.dialog.scopeToFolder('Other', () => {}); s.deckAck(false);
    assert.equal(s.scoped, 0); assert.equal(s.flushes, 0); assert.equal(s.dialog.openFolder, 'Work'); assertReleased(s);
    s = setup(); s.dialog.scopeToFolder('Other', () => {}); s.deckAck(true); s.pinAck(false);
    assert.equal(s.scoped, 0); assert.equal(s.dialog.openFolder, 'Work'); assertReleased(s);
    s = setup(); s.dialog.scopeToFolder('Other', () => {}); s.dialog.closeGateTimer.fire();
    s.deckAck(true); assert.equal(s.scoped, 0); assertReleased(s);
    s = setup(); s.collection.flushPendingSaves = () => {s.collection.lastError = 'Recovery write failed'; return false;};
    s.dialog.scopeToFolder('Other', () => {}); s.deckAck(true); s.pinAck(true);
    assert.equal(s.scoped, 0); assert.equal(s.dialog.openFolder, 'Work'); assert.equal(s.dialog.expanded, true);
    assert.match(s.dialog.saveStatus, /Recovery write failed/); assertReleased(s);
    s = setup(); s.collection.flushPendingSaves = () => {throw Error('native error');};
    s.dialog.scopeToFolder('Other', () => {}); s.deckAck(true); s.pinAck(true);
    assert.equal(s.scoped, 0); assert.equal(s.dialog.openFolder, 'Work'); assertReleased(s);
    assert.match(s.dialog.saveStatus, /native error/);
}
function rootLifecycle() {
    let deckAck, pinAck, switchCount = 0, flushCount = 0;
    const oldDeck = {enabled: true, runJavaScript(_script, cb) {deckAck = cb;}};
    const oldPin = {editorEnabled: true, appCloseReady(cb) {pinAck = cb;}};
    const records = {oldPin: {pinned: true}, newPin: {pinned: true}};
    const events = [];
    const dialog = {
        navigationPending: false, loaded: true, expanded: true, pinnedIds: ['oldPin'],
        ids: ['oldCard'], order: ['oldCard'], selected: 0, openFolder: 'Old',
        saveStatus: 'Saved', libraryOpen: true, paletteOpen: true,
        applyManifest(value) {events.push('manifest'); this.ids = value.ids; this.order = value.ids; this.openFolder = value.openFolder;},
        restorePersistedPins: null,
        collapse() {this.expanded = false;},
    };
    const loader = {get item() {return dialog.loaded ? oldDeck : null;}};
    const pinnedWindows = {
        get count() {return dialog.pinnedIds.length;},
        objectAt(i) {return dialog.pinnedIds[i] === 'oldPin' ? oldPin : null;},
    };
    const collection = {
        lastError: 'Flush failed',
        flushPendingSaves() {flushCount++; return true;},
        catalogIds() {return switchCount ? ['newPin'] : ['oldPin'];},
        documentObject(id) {return records[id];},
    };
    const notesStore = {load() {return {ok: true, ids: switchCount ? ['newCard'] : ['oldCard'], openFolder: ''};}};
    const shellControl = {openFolder() {
        events.push('native');
        if (!collection.flushPendingSaves()) return collection.lastError;
        switchCount++;
        // The native root change can synchronously notify the manifest observer
        // before this call returns. It must not rebind old windows to new IDs.
        dialog.libraryChanged();
        return '';
    }};
    const alignment = {restart() {}};
    const context = vm.createContext({dialog, collection, notesStore, shellControl, loader, pinnedWindows, alignment,
        applyManifest: result => dialog.applyManifest(result)});
    dialog.closeGateTimer = {start() {}, stop() {}, fire() {if (dialog.closeGateAbort) dialog.closeGateAbort();}};
    const observer = source.split('property Connections libraryWatch:')[1].match(/function onChanged\(\) \{([\s\S]*?)\n        \}/);
    assert.ok(observer, 'missing native manifest observer');
    dialog.libraryChanged = vm.runInContext(`(function onChanged() {${observer[1]}})`, context);
    for (const name of ['checkEditorsForClose', 'restorePersistedPins', 'switchRootFolder']) {
        dialog[name] = vm.runInContext(`(function ${name}(${name === 'checkEditorsForClose' ? 'excludedId, done' : name === 'switchRootFolder' ? 'folder' : ''}) {${body(name)}})`, context);
    }
    const snapshot = () => ({loaded: dialog.loaded, expanded: dialog.expanded,
        pins: [...dialog.pinnedIds], ids: [...dialog.ids], folder: dialog.openFolder,
        deckEnabled: oldDeck.enabled, pinEnabled: oldPin.editorEnabled});
    return {dialog, oldDeck, oldPin, collection, shellControl, events, snapshot,
        get deckAck() {return deckAck;}, get pinAck() {return pinAck;},
        get switchCount() {return switchCount;}, get flushCount() {return flushCount;}};
}
function rootSwitchRetiresOldEditors() {
    const s = rootLifecycle();
    s.dialog.switchRootFolder('new');
    assert.equal(s.switchCount, 0);
    s.deckAck(true); s.pinAck(true);
    assert.equal(s.switchCount, 1);
    assert.equal(s.flushCount, 1);
    assert.deepEqual(s.snapshot().pins, ['newPin']);
    assert.deepEqual(s.snapshot().ids, ['newCard']);
    assert.equal(s.snapshot().loaded, false, 'old deck WebEngine must be destroyed, not reused with old IDs');
    assert.equal(s.snapshot().expanded, false);
    assert.deepEqual(s.events, ['native', 'manifest'],
        'native notification must not rebind old editors before the commit retires them');
    // A stale editor callback after completion cannot revive the old root or its windows.
    s.deckAck(false); s.pinAck(false); s.dialog.closeGateTimer.fire();
    assert.deepEqual(s.snapshot().pins, ['newPin']);
    assert.deepEqual(s.snapshot().ids, ['newCard']);
    for (const failure of ['deck', 'pin', 'timeout', 'flush']) {
        const r = rootLifecycle();
        const before = r.snapshot();
        if (failure === 'flush') r.collection.flushPendingSaves = () => false;
        r.dialog.switchRootFolder('new');
        if (failure === 'timeout') r.dialog.closeGateTimer.fire();
        else if (failure === 'deck') r.deckAck(false);
        else {r.deckAck(true); r.pinAck(failure !== 'pin');}
        assert.equal(r.switchCount, 0, failure);
        assert.deepEqual(r.snapshot(), before, failure + ' must leave old windows/buffers intact');
        if (failure !== 'flush') {
            r.deckAck(true);
            if (r.pinAck) r.pinAck(true);
            assert.deepEqual(r.snapshot(), before, failure + ' late ack');
        }
    }
}
function root() {
    let s = setup();
    s.dialog.switchRootFolder('new'); assertFrozen(s);
    assert.equal(s.switches, 0); s.deckAck(true);
    assert.equal(s.switches, 0, 'pinned editor must acknowledge first');
    s.pinAck(true); assert.equal(s.switches, 1); assertFrozen(s);
    assert.equal(s.dialog.loaded, false, 'successful root switch retires the old deck');
    s = setup(); s.dialog.switchRootFolder('new'); s.deckAck(true); s.pinAck(false);
    assert.equal(s.switches, 0); assertReleased(s);
    s = setup(); s.dialog.switchRootFolder('new'); s.dialog.closeGateTimer.fire(); s.deckAck(true);
    assert.equal(s.switches, 0); assertReleased(s);
    s = setup(); s.shellControl.openFolder = () => 'Root refused';
    s.dialog.switchRootFolder('new'); s.deckAck(true); s.pinAck(true);
    assert.match(s.dialog.saveStatus, /Root refused/); assertReleased(s);
    assert.equal(s.dialog.openFolder, 'Work'); assert.equal(s.dialog.expanded, true);
    s = setup(); s.shellControl.openFolder = () => {throw Error('native error');};
    s.dialog.switchRootFolder('new'); s.deckAck(true); s.pinAck(true);
    assert.match(s.dialog.saveStatus, /native error/); assertReleased(s);
    assert.equal(s.dialog.openFolder, 'Work');
    assert.match(source, /onAccepted: \{\s*dialog\.switchRootFolder\(selectedFolder\)/,
        'chooser must use guarded root route');
}
folder(); root(); rootSwitchRetiresOldEditors(); console.log('navigation gate: PASS');
