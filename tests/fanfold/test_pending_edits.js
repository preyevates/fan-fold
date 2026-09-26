// Run the real browser script functions with a controlled asynchronous bridge.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const qml = path.resolve(__dirname, '../../src/shell/qml');
function deferred() { let resolve; const promise = new Promise(r => { resolve = r; }); return {promise, resolve}; }
function load(name) {
    const source = fs.readFileSync(path.join(qml, name), 'utf8').split('new QWebChannel(')[0];
    const context = vm.createContext({window: {}, location: {search: '?id=n1'}, URLSearchParams, setTimeout, clearTimeout});
    vm.runInContext(source, context, {filename: name});
    return {fan: context.window.fan, context};
}
async function pinnedSaveTail() {
    const {fan, context} = load('pinned.js');
    const firstSave = deferred();
    let value = 'initial', saves = [], pushes = [];
    vm.runInContext('pinned={status(){}}', context);
    fan.editors = [{getValue: () => value}];
    fan.frames = [{contentDocument: {getElementById: () => ({inert: false})}}];
    fan.state = {loaded: true, dirty: false, busy: false, external: false,
                 revision: 'r1', baseline: 'initial', status: 'Saved'};
    fan.call = (method, id, text) => {
        if (method === 'noteEdited') { pushes.push(text); return Promise.resolve(true); }
        if (method === 'saveNote') {
            saves.push(text);
            return saves.length === 1 ? firstSave.promise : Promise.resolve({ok: true, revision: 'r3'});
        }
        throw Error(method);
    };
    value = 'first';
    const saving = fan.save();
    value = 'second';
    fan.changed();
    firstSave.resolve({ok: true, revision: 'r2'});
    await saving;
    await Promise.resolve();
    assert.equal(fan.state.dirty, true);
    assert.equal(fan.state.baseline, 'first');
    assert.equal(pushes.at(-1), 'second', 'tail must reach the native engine');
    assert.equal(await fan.closeSafely(), true);
    assert.equal(saves.at(-1), 'second', 'close waits for latest save');
    assert.equal(fan.state.dirty, false);
}
async function pinnedRefusesFailedRecovery() {
    const {fan, context} = load('pinned.js');
    vm.runInContext('pinned={status(){}}', context);
    let value = 'new';
    const element = {inert: false};
    fan.frames = [{contentDocument: {getElementById: () => element}}];
    fan.editors = [{getValue: () => value}];
    fan.state = {loaded: true, dirty: true, busy: false, external: false,
                 revision: 'r1', baseline: 'old', status: 'Unsaved'};
    fan.call = method => method === 'saveNote'
        ? Promise.resolve({ok: false, error: 'Recovery write failed'}) : Promise.resolve(false);
    assert.equal(await fan.closeSafely(), false);
    assert.equal(element.inert, false);
    assert.match(fan.state.status, /Recovery write failed/);
}
async function pendingReloadTail() {
    const {fan, context} = load('app.js');
    const loadNote = deferred();
    let value = 'old', pushed = [];
    vm.runInContext('notes={status(){}}', context);
    fan.editors = [{getValue: () => value, setValue: v => {value = v;}}];
    fan.states = [{id: 'n1', loaded: true, dirty: false, busy: false,
                   baseline: 'old', revision: 'r1', status: 'Saved'}];
    fan.call = (method, id, text) => {
        if (method === 'loadNote') return loadNote.promise;
        if (method === 'noteEdited') { pushed.push(text); return Promise.resolve(true); }
        throw Error(method);
    };
    const loading = fan.reload(0);
    value = 'typed during load';
    fan.changed(0);
    assert.deepEqual(pushed, []);
    loadNote.resolve({ok: true, text: 'disk version', revision: 'r2'});
    await loading;
    await Promise.resolve();
    assert.equal(value, 'typed during load', 'must not replace edited buffer');
    assert.equal(pushed.at(-1), value, 'must flush pending edit after load');
}
async function deckUndoToBaseline() {
    const {fan, context} = load('app.js');
    let value = 'A', native = 'A';
    const pushes = [];
    vm.runInContext('notes={status(){}}', context);
    fan.editors = [{getValue: () => value}];
    fan.states = [{id: 'n1', loaded: true, dirty: false, busy: false,
                   revision: 'rA', baseline: 'A', status: 'Saved'}];
    fan.call = (method, id, text) => {
        if (method === 'noteEdited') { pushes.push(text); native = text; return Promise.resolve(true); }
        if (method === 'probeNote') return Promise.resolve({ok: true, committed: true, revision: native === 'A' ? 'rA' : 'rB'});
        throw Error(method);
    };
    value = 'B'; fan.changed(0);
    value = 'A'; fan.changed(0);
    assert.equal(fan.states[0].dirty, false, 'undo makes the UI clean');
    assert.deepEqual(pushes, ['B', 'A'], 'undo must replace the native autosave buffer');
    assert.equal(native, 'A');
    await fan.poll();
    assert.equal(fan.states[0].baseline, 'A', 'reconciliation keeps the committed baseline');
    assert.equal(fan.states[0].dirty, false);
}
async function deckQuitWaitsForNativeReversion() {
    const {fan, context} = load('app.js');
    const revert = deferred();
    let value = 'A', native = 'A', closeDirty = false;
    context.recordStatus = (text, dirty) => { closeDirty = dirty; };
    vm.runInContext('notes={status: recordStatus}', context);
    fan.editors = [{getValue: () => value}];
    fan.states = [{id: 'n1', loaded: true, dirty: false, busy: false,
                   revision: 'rA', baseline: 'A', status: 'Saved'}];
    fan.call = (method, id, text) => {
        assert.equal(method, 'noteEdited');
        if (text === 'A') return revert.promise.then(ok => { if (ok) native = text; return ok; });
        native = text; return Promise.resolve(true);
    };
    const canQuit = () => !closeDirty; // Main.qml requestClose/onClosing reads dialog.dirty
    value = 'B'; fan.changed(0);
    await Promise.resolve(); // B was acknowledged; only the delayed A reversion remains
    value = 'A'; fan.changed(0);
    assert.equal(fan.states[0].dirty, false, 'the editor matches its baseline');
    assert.equal(native, 'B', 'native still holds the earlier edit');
    assert.equal(canQuit(), false, 'quit must wait for the reversion bridge acknowledgment');
    revert.resolve(true);
    await revert.promise;
    await Promise.resolve();
    assert.equal(canQuit(), true, 'quit is safe after native accepts the reversion');
    assert.equal(native, 'A');
}
async function cleanQuitRefusesFailedNativeFlush() {
    // Execute the real Main.qml close handlers, not a parallel test implementation.
    const source = fs.readFileSync(path.join(qml, 'Main.qml'), 'utf8');
    const request = source.match(/function requestClose\(\) \{([\s\S]*?)\n    \}/);
    const gate = source.match(/function checkEditorsForClose\(excludedId, done\) \{([\s\S]*?)\n    \}/);
    const closing = source.match(/onClosing: function\(close\) \{([^\n]*)\}/);
    assert.ok(request && gate && closing, 'close handlers must remain testable');
    let exits = 0, flushes = 0;
    const dialog = {dirty: false, allowDiscard: false, closeRequested: false,
                    saveStatus: 'Saved', openNote() {}, selected: 0};
    const collection = {lastError: 'Atomic autosave staging failed: Permission denied',
                        flushPendingSaves() { flushes++; return false; }};
    const context = vm.createContext({dialog, collection, loader: {item: null},
                                      pinnedWindows: {count: 0}, Qt: {quit() { exits++; }}});
    dialog.closeGateTimer = dialog.closeGateTimer || {start(){},stop(){}};
    dialog.checkEditorsForClose = vm.runInContext(`(function checkEditorsForClose(excludedId, done) {${gate[1]}})`, context);
    dialog.requestClose = vm.runInContext(`(function requestClose() {${request[1]}})`, context);
    const onClosing = vm.runInContext(`(function(close) {${closing[1]}})`, context);
    dialog.requestClose(); // footer/tray route: WebChannel has acknowledged the clean reversion
    assert.equal(exits, 0, 'a clean UI cannot quit with uncommitted native Markdown');
    assert.match(dialog.saveStatus, /Atomic autosave staging failed/);
    const event = {accepted: true};
    onClosing(event); // window-manager route must also reject the close
    assert.equal(event.accepted, false);
    assert.equal(exits, 0);
    assert.ok(flushes >= 1);
    const trayQuit = source.match(/function onQuitRequested\(\) \{([^\n]*)\}/);
    assert.ok(trayQuit, 'tray quit must route through the same close gate');
    vm.runInContext(`(function() {${trayQuit[1]}})`, context)();
    assert.equal(exits, 0, 'tray quit must not bypass the failed native flush');
    assert.ok(flushes >= 3, 'footer, window manager and tray must each attempt the flush');
    const bridgeStatus = source.match(/function status\(text,dirty,self\) \{([^\n]*)\}/);
    assert.ok(bridgeStatus);
    vm.runInContext(`(function(text,dirty,self) {${bridgeStatus[1]}})`, context)('Saved', false, false);
    assert.match(dialog.saveStatus, /Atomic autosave staging failed/,
                 'periodic editor status must not hide the failed-close error');
    collection.flushPendingSaves = () => { flushes++; return true; };
    dialog.requestClose();
    assert.equal(exits, 1, 'successful retry permits quit');
    assert.equal(dialog.closeSaveError, '');
}
async function deckQuitWaitsForOtherNoteAndKeepsFailureVisible() {
    const {fan, context} = load('app.js');
    const revert = deferred();
    let value = 'A', closeDirty = false, status = '';
    context.recordStatus = (text, dirty) => { status = text; closeDirty = dirty; };
    vm.runInContext('notes={status: recordStatus}', context);
    fan.editors = [{getValue: () => value}, {getValue: () => 'other'}];
    fan.states = [{id: 'n1', loaded: true, dirty: false, busy: false,
                   revision: 'rA', baseline: 'A', status: 'Saved'},
                  {id: 'n2', loaded: true, dirty: false, busy: false,
                   revision: 'r2', baseline: 'other', status: 'Saved'}];
    fan.active = 1; // selected note is clean; n1 must still guard quit
    fan.call = (method, id, text) => {
        assert.equal(method, 'noteEdited');
        assert.equal(id, 'n1');
        return text === 'A' ? revert.promise : Promise.resolve(true);
    };
    value = 'B'; fan.changed(0);
    value = 'A'; fan.changed(0);
    assert.equal(closeDirty, true, 'pending edit in another note blocks quit');
    assert.match(status, /1 other note\(s\) unsaved/);
    revert.resolve(false);
    await revert.promise;
    await Promise.resolve();
    assert.equal(closeDirty, true, 'failed native reversion cannot permit quit');
    fan.active = 0; fan.publish(); // selecting the failed note shows its refusal
    assert.match(status, /Save refused/);
    fan.changed(0); // editor callbacks or a poll must not erase a bridge refusal
    assert.equal(closeDirty, true, 'failed reversion remains unsafe after status refresh');
    assert.match(status, /Save refused/);
}
async function deckNoSelectionStillGuardsPendingEdit() {
    const {fan, context} = load('app.js');
    const acknowledgment = deferred();
    let value = 'A', closeDirty = false;
    context.recordStatus = (_text, dirty) => { closeDirty = dirty; };
    vm.runInContext('notes={status: recordStatus}', context);
    fan.editors = [{getValue: () => value}];
    fan.states = [{id: 'n1', loaded: true, dirty: false, busy: false,
                   revision: 'rA', baseline: 'A', status: 'Saved'}];
    fan.call = method => {
        assert.equal(method, 'noteEdited');
        return acknowledgment.promise;
    };
    value = 'B'; fan.changed(0);
    value = 'A'; fan.changed(0);
    fan.active = -1; fan.publish(); // selection can be absent while an existing note has work
    assert.equal(closeDirty, true, 'absence of a selected note cannot permit quit');
    acknowledgment.resolve(true);
    await Promise.resolve();
}
async function deckExplicitSaveClearsFailedReversion() {
    const {fan, context} = load('app.js');
    let value = 'A', closeDirty = false;
    context.recordStatus = (_text, dirty) => { closeDirty = dirty; };
    vm.runInContext('notes={status: recordStatus}', context);
    fan.editors = [{getValue: () => value}];
    fan.states = [{id: 'n1', loaded: true, dirty: false, busy: false,
                   revision: 'rA', baseline: 'A', status: 'Saved'}];
    fan.call = method => {
        if (method === 'noteEdited') return Promise.resolve(false);
        if (method === 'saveNote') return Promise.resolve({ok: true, revision: 'rA'});
        throw Error(method);
    };
    value = 'B'; fan.changed(0);
    value = 'A'; fan.changed(0);
    await Promise.resolve();
    assert.equal(closeDirty, true);
    assert.equal((await fan.save(0)).ok, true, 'explicit native save recovers refused buffer');
    await Promise.resolve();
    fan.changed(0); // status refresh must not revive a refusal recovered by saveNote
    assert.equal(closeDirty, false, 'quit is safe after successful explicit save');
    assert.equal(fan.states[0].status, 'Saved');
}
async function pinnedUndoToBaseline() {
    const {fan, context} = load('pinned.js');
    let value = 'A', native = 'A';
    const pushes = [];
    vm.runInContext('pinned={status(){}}', context);
    fan.editors = [{getValue: () => value}];
    fan.frames = [{contentDocument: {getElementById: () => ({inert: false})}}];
    fan.state = {loaded: true, dirty: false, busy: false, external: false,
                 revision: 'rA', baseline: 'A', status: 'Saved'};
    fan.call = (method, id, text) => {
        if (method === 'noteEdited') { pushes.push(text); native = text; return Promise.resolve(true); }
        if (method === 'probeNote') return Promise.resolve({ok: true, committed: true, revision: native === 'A' ? 'rA' : 'rB'});
        throw Error(method);
    };
    value = 'B'; fan.changed();
    value = 'A'; fan.changed();
    assert.equal(fan.state.dirty, false);
    assert.deepEqual(pushes, ['B', 'A'], 'undo must replace the native autosave buffer');
    assert.equal(native, 'A');
    await fan.poll();
    assert.equal(fan.state.baseline, 'A');
    assert.equal(fan.state.dirty, false);
}
async function pinnedCloseWaitsForReversion() {
    const {fan, context} = load('pinned.js');
    let value = 'A', native = 'A';
    const revert = deferred(), element = {inert: false};
    vm.runInContext('pinned={status(){}}', context);
    fan.editors = [{getValue: () => value}];
    fan.frames = [{contentDocument: {getElementById: () => element}}];
    fan.state = {loaded: true, dirty: false, busy: false, external: false,
                 revision: 'rA', baseline: 'A', status: 'Saved'};
    fan.call = (method, id, text) => {
        if (method !== 'noteEdited') throw Error(method);
        if (text === 'A') return revert.promise.then(ok => { if (ok) native = text; return ok; });
        native = text; return Promise.resolve(true);
    };
    value = 'B'; fan.changed();
    value = 'A'; fan.changed();
    let closed = false;
    const closing = fan.closeSafely().then(ok => { closed = ok; return ok; });
    await Promise.resolve();
    assert.equal(closed, false, 'unpin must wait until native accepts the clean reversion');
    assert.equal(native, 'B');
    revert.resolve(true);
    assert.equal(await closing, true);
    assert.equal(native, 'A');
    assert.equal(element.inert, true);
}
async function pinnedOutOfOrderPushCannotLookCloseReady() {
    const {fan, context} = load('pinned.js');
    let value='A', native='A';
    const older=deferred();
    vm.runInContext('pinned={status(){}}',context);
    fan.editors=[{getValue:()=>value}];
    fan.state={loaded:true,dirty:false,busy:false,revision:'rA',baseline:'A',status:'Saved'};
    fan.call=(method,id,text)=>{
        assert.equal(method,'noteEdited');
        if(text==='B') return older.promise.then(ok=>{if(ok)native=text;return ok;});
        native=text;return Promise.resolve(true);
    };
    value='B';fan.changed();
    value='A';fan.changed();
    await new Promise(resolve=>setImmediate(resolve));
    older.resolve(true);
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(native,'B');
    assert.equal(fan.closeReady(),false);
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(native,'A');
    assert.equal(fan.closeReady(),true);
}
async function deckOutOfOrderPushCannotLookCloseReady() {
    const {fan, context} = load('app.js');
    let value='A', native='A';
    const older=deferred();
    vm.runInContext('notes={status(){}}',context);
    fan.editors=[{getValue:()=>value}];
    fan.states=[{id:'n1',loaded:true,dirty:false,busy:false,revision:'rA',baseline:'A',status:'Saved'}];
    fan.call=(method,id,text)=>{
        assert.equal(method,'noteEdited');
        if(text==='B') return older.promise.then(ok=>{if(ok)native=text;return ok;});
        native=text;return Promise.resolve(true);
    };
    value='B';fan.changed(0);
    value='A';fan.changed(0);
    await new Promise(resolve=>setImmediate(resolve));
    older.resolve(true);
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(native,'B');
    assert.equal(fan.closeReady(null),false,'stale native buffer must block close');
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(native,'A','close check must reassert the live value');
    assert.equal(fan.closeReady(null),true);
}
async function pinnedCloseWaitsForAllOutOfOrderPushes() {
    const {fan, context} = load('pinned.js');
    let value = 'A', native = 'A', saves = 0;
    const older = deferred(), editor = {inert:false};
    vm.runInContext('pinned={status(){}}', context);
    fan.editors = [{getValue:() => value}];
    fan.frames = [{contentDocument:{getElementById:() => editor}}];
    fan.state = {loaded:true,dirty:false,busy:false,revision:'rA',baseline:'A',status:'Saved'};
    fan.call = (method, id, text) => {
        if(method === 'noteEdited') {
            if(text === 'B') return older.promise.then(ok => {if(ok) native=text; return ok;});
            native=text; return Promise.resolve(true);
        }
        if(method === 'saveNote') {
            saves++; native=text; return Promise.resolve({ok:true,revision:'rA'});
        }
        throw Error(method);
    };
    value='B'; fan.changed();
    value='A'; fan.changed();
    fan.beginClose();
    await Promise.resolve(); await Promise.resolve(); await Promise.resolve();
    assert.equal(fan.closeResult,0,'unpin must wait for an older in-flight push too');
    older.resolve(true);
    for(let i=0;i<8 && fan.closeResult===0;i++) await new Promise(resolve=>setImmediate(resolve));
    assert.equal(fan.closeResult,1);
    assert.equal(native,'A','a late old push cannot replace the current buffer');
    assert.ok(saves>0,'final explicit save reasserts the latest editor value');
}
async function pinnedCloseRefusesFailedCleanReversion() {
    const {fan, context} = load('pinned.js');
    let value = 'A', saves = 0;
    const element = {inert: false};
    vm.runInContext('pinned={status(){}}', context);
    fan.editors = [{getValue: () => value}];
    fan.frames = [{contentDocument: {getElementById: () => element}}];
    fan.state = {loaded: true, dirty: false, busy: false, external: false,
                 revision: 'rA', baseline: 'A', status: 'Saved'};
    fan.call = (method, id, text) => {
        if (method === 'noteEdited') return Promise.resolve(text !== 'A');
        if (method === 'saveNote') { saves++; return Promise.resolve({ok: false, error: 'Recovery write failed'}); }
        throw Error(method);
    };
    value = 'B'; fan.changed();
    value = 'A'; fan.changed();
    assert.equal(await fan.closeSafely(), false, 'failed clean reversion cannot be discarded');
    assert.equal(saves, 1, 'close must attempt an explicit recovery save');
    assert.equal(element.inert, false);
    assert.match(fan.state.status, /Recovery write failed/);
}
async function staleProbeCannotCleanUncommittedTail(name) {
    const {fan, context} = load(name);
    let value = 'A';
    const probe = deferred(), pushes = [];
    vm.runInContext(name === 'app.js' ? 'notes={status(){}}' : 'pinned={status(){}}', context);
    fan.editors = [{getValue: () => value}];
    const state = {id: 'n1', loaded: true, dirty: false, busy: false, external: false,
                   revision: 'rA', baseline: 'A', status: 'Saved'};
    if (name === 'app.js') fan.states = [state]; else fan.state = state;
    fan.call = (method, id, text) => {
        if (method === 'noteEdited') { pushes.push(text); return Promise.resolve(true); }
        if (method === 'probeNote') return probe.promise;
        throw Error(method);
    };
    value = 'B'; fan.changed(name === 'app.js' ? 0 : undefined);
    const polling = fan.poll(); // probe sees committed B before the next edit
    value = 'C'; fan.changed(name === 'app.js' ? 0 : undefined);
    probe.resolve({ok: true, committed: true, revision: 'rB'});
    await polling;
    assert.equal(state.baseline, 'B', 'probe must not attribute an earlier commit to the newest push');
    assert.equal(state.dirty, true, 'uncommitted C must remain unsaved');
    assert.equal(pushes.at(-1), 'C');
}
async function supersededPushFailureCannotDirtyCleanReversion(name) {
    const {fan, context} = load(name);
    let value = 'A';
    const oldPush = deferred();
    vm.runInContext(name === 'app.js' ? 'notes={status(){}}' : 'pinned={status(){}}', context);
    fan.editors = [{getValue: () => value}];
    const state = {id: 'n1', loaded: true, dirty: false, busy: false, external: false,
                   revision: 'rA', baseline: 'A', status: 'Saved'};
    if (name === 'app.js') fan.states = [state]; else fan.state = state;
    fan.call = (method, id, text) => {
        if (method !== 'noteEdited') throw Error(method);
        return text === 'B' ? oldPush.promise : Promise.resolve(true);
    };
    value = 'B'; fan.changed(name === 'app.js' ? 0 : undefined);
    value = 'A'; fan.changed(name === 'app.js' ? 0 : undefined);
    await Promise.resolve(); // latest reversion is accepted first
    oldPush.resolve(false);
    await oldPush.promise;
    await Promise.resolve();
    assert.equal(state.dirty, false, 'old callback must not override the later accepted reversion');
    assert.equal(state.status, 'Saved');
}
async function discardPromptFlushesOtherNativeNotes() {
    const source = fs.readFileSync(path.join(qml, 'Main.qml'), 'utf8');
    const method = source.match(/function requestDiscardClose\(\) \{([\s\S]*?)\n    \}/);
    const gate = source.match(/function checkEditorsForClose\(excludedId, done\) \{([\s\S]*?)\n    \}/);
    assert.ok(method && gate, 'discard prompt needs a guarded native-close route');
    let exits = 0, flushes = 0;
    const dialog = {ids: ['n1', 'n2'], selected: 0, dirty: true, selectedDirty: true,
                    allowDiscard: false, saveStatus: '', closeRequested: true};
    const collection = {lastError: 'Atomic autosave staging failed: Permission denied',
        discardSelectedAfterFlushingOthers(id) { assert.equal(id, 'n1'); flushes++; return false; }};
    const context = vm.createContext({dialog, collection, loader: {item: null},
                                      pinnedWindows: {count: 0}, Qt: {quit() { exits++; }}});
    dialog.closeGateTimer = dialog.closeGateTimer || {start(){},stop(){}};
    dialog.checkEditorsForClose = vm.runInContext(`(function checkEditorsForClose(excludedId, done) {${gate[1]}})`, context);
    dialog.requestDiscardClose = vm.runInContext(`(function requestDiscardClose() {${method[1]}})`, context);
    dialog.requestDiscardClose();
    assert.equal(exits, 0, 'discarding selected must not lose another uncommitted note');
    assert.equal(dialog.allowDiscard, false);
    assert.match(dialog.saveStatus, /Atomic autosave staging failed/);
    assert.match(source.slice(source.indexOf('objectName:"close-prompt-body"')),
                 /text:dialog\.closeSaveError\s*\?\s*dialog\.closeSaveError/,
                 'flush refusal must be visible inside the still-open modal');
    const bridgeStatus = source.match(/function status\(text,dirty,self\) \{([^\n]*)\}/);
    assert.ok(bridgeStatus);
    vm.runInContext(`(function(text,dirty,self) {${bridgeStatus[1]}})`, context)('Unsaved', true, true);
    assert.match(dialog.saveStatus, /Atomic autosave staging failed/,
                 'editor status must not conceal discard-close failure');
    collection.discardSelectedAfterFlushingOthers = id => { assert.equal(id, 'n1'); flushes++; return true; };
    dialog.requestDiscardClose();
    assert.equal(exits, 1, 'successful native commit of other notes permits selected discard');
    assert.equal(dialog.allowDiscard, true);
    assert.equal(flushes, 2);
    assert.equal(dialog.closeSaveError, '');
    for (const handler of [
        /Accessible\.onPressAction: \{ (dialog\.requestDiscardClose\(\)) \}/,
        /if\(event\.key===Qt\.Key_Space[^\n]*?\{ (dialog\.requestDiscardClose\(\)); event\.accepted=true \}/,
        /onClicked: \{ (dialog\.requestDiscardClose\(\)) \}/
    ]) {
        const match = source.slice(source.indexOf('id: discardButton')).match(handler);
        assert.ok(match, 'mouse, keyboard and accessibility discard paths must use guard');
        vm.runInContext(match[1], context);
    }
    assert.equal(exits, 4);
}
async function pinnedQmlRefusesUncommittedNativeMarkdown(
    record = {saveError: 'Atomic autosave staging failed: Permission denied'},
    expectedError = /Atomic autosave staging failed/
) {
    const source = fs.readFileSync(path.join(qml, 'PinnedNoteWindow.qml'), 'utf8');
    const method = source.match(/function requestSafeClose\(\) \{([\s\S]*?)\n    \}/);
    assert.ok(method);
    const {fan, context: js} = load('pinned.js');
    let value = 'new', disk = 'old', closeCount = 0, saves = 0;
    const element = {inert: false};
    vm.runInContext('pinned={status(){}}', js);
    fan.frames = [{contentDocument: {getElementById: () => element}}];
    fan.editors = [{getValue: () => value}];
    fan.state = {loaded: true, dirty: true, busy: false, external: false,
                 revision: 'r1', baseline: 'old', status: 'Unsaved'};
    fan.call = method => {
        if (method === 'noteEdited') return Promise.resolve(true);
        if (method === 'saveNote') return Promise.resolve({ok: true, revision: 'r2'});
        throw Error(method);
    };
    const collection = {lastError: '',
        saveNow(id) { assert.equal(id, 'n1'); saves++; return false; }};
    const pinnedWindow = {documentId: 'n1', record,
        closeCheckPending: false, closeAttempt: 0, status: '',
        closeRequested() { closeCount++; disk = value; }};
    const pinnedEditor = {runJavaScript(script, callback) {
        const result = vm.runInContext(script, js);
        // Qt WebEngine cannot marshal a Promise as runJavaScript's result.
        if (callback) setImmediate(() => callback(result && typeof result.then === 'function' ? null : result));
    }};
    const closePoll = {start() { setImmediate(() => closePoll.onTriggered()); }, stop() {}};
    const closeDeadline = {start(){}, stop(){}};
    const timer = source.match(/id: closePoll[\s\S]*?onTriggered: \{([\s\S]*?)\n        \}/);
    assert.ok(timer, 'the close acknowledgment must be polled as a primitive');
    const qmlContext = vm.createContext({pinnedWindow, collection, pinnedEditor, closePoll, closeDeadline});
    closePoll.onTriggered = vm.runInContext(`(function() {${timer[1]}})`, qmlContext);
    pinnedWindow.requestSafeClose = vm.runInContext(`(function requestSafeClose() {${method[1]}})`, qmlContext);
    pinnedWindow.requestSafeClose();
    for (let i = 0; i < 8 && saves === 0; i++) await new Promise(resolve => setImmediate(resolve));
    assert.equal(saves, 1, 'native saveNow must run after JS bridge acknowledgment');
    assert.equal(closeCount, 0, 'native commit failure must keep window open');
    assert.equal(disk, 'old');
    assert.equal(element.inert, false, 'refused close must unlock editor without reloading');
    assert.equal(value, 'new');
    assert.match(pinnedWindow.status, expectedError);
    const bridgeStatus = source.match(/function status\(text, dirty\) \{([^\n]*)\}/);
    assert.ok(bridgeStatus);
    vm.runInContext(`(function(text,dirty) {${bridgeStatus[1]}})`, qmlContext)('Saved', false);
    assert.match(pinnedWindow.status, expectedError,
                 'periodic status must not hide native commit failure');
    collection.saveNow = id => { assert.equal(id, 'n1'); saves++; return true; };
    pinnedWindow.requestSafeClose();
    for (let i = 0; i < 8 && saves < 2; i++) await new Promise(resolve => setImmediate(resolve));
    assert.equal(closeCount, 1, 'retry after native commit closes');
    assert.equal(saves, 2);
}
async function discardWaitsForOtherEditorsBridgeAck() {
    const source = fs.readFileSync(path.join(qml, 'Main.qml'), 'utf8');
    const gate = source.match(/function checkEditorsForClose\(excludedId, done\) \{([\s\S]*?)\n    \}/);
    assert.ok(gate, 'close must inspect live web editors before flushing native collection');
    const request = source.match(/function requestDiscardClose\(\) \{([\s\S]*?)\n    \}/);
    const {fan, context: page} = load('app.js');
    const pending = deferred();
    vm.runInContext('notes={status(){}}', page);
    fan.editors = [{getValue: () => 'selected'}, {getValue: () => 'latest B'}];
    fan.states = [{id: 'n1', loaded: true, dirty: true, busy: false, baseline: 'old A', revision: 'r1'},
                  {id: 'n2', loaded: true, dirty: false, busy: false, baseline: 'old B', revision: 'r2'}];
    fan.call = (method, id) => { assert.equal(method, 'noteEdited'); assert.equal(id, 'n2'); return pending.promise; };
    fan.changed(1);
    const pinned = load('pinned.js');
    vm.runInContext('pinned={status(){}}', pinned.context);
    const pinAck = deferred();
    pinned.fan.state = {loaded:true, dirty:true, busy:false, baseline:'old C', revision:'r3'};
    pinned.fan.editors = [{getValue: () => 'latest C'}];
    pinned.fan.call = () => pinAck.promise;
    pinned.fan.changed();
    let exits = 0, flushes = 0;
    const run = (script, callback, context) => {
        const value = vm.runInContext(script, context);
        if (callback) setImmediate(() => callback(value && typeof value.then === 'function' ? null : value));
    };
    const dialog = {ids:['n1','n2'],selected:0,allowDiscard:false,saveStatus:'',closeSaveError:''};
    const collection = {lastError:'',discardSelectedAfterFlushingOthers(id) {assert.equal(id,'n1'); flushes++; return true;}};
    const pinWindow = {editorEnabled:true,appCloseReady(cb) { run('fan.closeReady()',cb,pinned.context); }};
    const pinnedWindows = {count:1,objectAt:() => pinWindow};
    const loader = {item:{enabled:true,runJavaScript(script,cb) { run(script,cb,page); }}};
    const ctx = vm.createContext({dialog,collection,pinnedWindows,loader,Qt:{quit(){exits++;}}});
    dialog.closeGateTimer = {start(){},stop(){}};
    dialog.checkEditorsForClose = vm.runInContext(`(function checkEditorsForClose(excludedId, done) {${gate[1]}})`,ctx);
    dialog.requestDiscardClose = vm.runInContext(`(function requestDiscardClose() {${request[1]}})`,ctx);
    dialog.requestDiscardClose();
    assert.equal(loader.item.enabled,false,'freeze the deck before asynchronously checking editor acknowledgments');
    assert.equal(pinWindow.editorEnabled,false,'freeze pinned editors before checking the deck');
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(loader.item.enabled,true,'a refused close restores editing');
    assert.equal(pinWindow.editorEnabled,true,'a refused close restores pinned editing');
    assert.equal(flushes,0,'B is not yet in native collection');
    assert.equal(exits,0);
    assert.match(dialog.closeSaveError,/pending|retry/i);
    pending.resolve(true); pinAck.resolve(true);
    await Promise.resolve(); await Promise.resolve();
    dialog.requestDiscardClose();
    for (let i = 0; i < 6 && flushes === 0; i++) await new Promise(resolve => setImmediate(resolve));
    assert.equal(flushes,1,'after both acknowledgments, flush other native notes');
    assert.equal(exits,1);
}
async function pinnedFailedPushNeverLooksReady() {
    const {fan, context} = load('pinned.js');
    vm.runInContext('pinned={status(){}}', context);
    fan.state = {loaded:true,dirty:true,busy:false,baseline:'old',revision:'r1'};
    fan.editors = [{getValue:() => 'latest'}];
    fan.call = () => Promise.resolve(false);
    fan.changed();
    await Promise.resolve(); await Promise.resolve();
    fan.changed(); // a status refresh must not erase the failure
    assert.equal(fan.closeReady(), false, 'failed bridge push cannot be treated as a committed note');
}
async function pinnedFailedPushPollCannotClearFailure() {
    const {fan, context} = load('pinned.js');
    vm.runInContext('pinned={status(){}}', context);
    fan.state = {loaded:true,dirty:true,busy:false,baseline:'old',revision:'r1'};
    fan.editors = [{getValue:() => 'latest'}];
    fan.call = method => method === 'noteEdited' ? Promise.resolve(false)
                     : Promise.resolve({ok:true,committed:true,revision:'r2'});
    fan.changed();
    await Promise.resolve(); await Promise.resolve();
    assert.equal(fan.closeReady(),false);
    await fan.poll();
    assert.equal(fan.closeReady(),false,'poll is not a positive acknowledgment of failed push');
    assert.equal(fan.state.dirty,true);
    assert.match(fan.state.status,/failed/i);
    fan.call = method => method === 'noteEdited' ? Promise.resolve(false)
                     : Promise.resolve({ok:true,committed:false,revision:'r2'});
    await fan.poll();
    assert.equal(fan.closeReady(),false);
    assert.match(fan.state.status,/failed/i,'even a noncommitting poll must retain the error');
    fan.call = method => method === 'noteEdited' ? Promise.resolve(true)
                     : Promise.resolve({ok:true,committed:true,revision:'r3'});
    fan.changed(); await Promise.resolve(); await Promise.resolve();
    assert.equal(fan.closeReady(),true,'acknowledged retry may clear the failure');
}
async function closeGateCallbackLifetime() {
    const source = fs.readFileSync(path.join(__dirname, '../../src/shell/qml/Main.qml'), 'utf8');
    const gate = source.match(/function checkEditorsForClose\(excludedId, done\) \{([\s\S]*?)\n    \}\n    function requestDiscardClose/);
    assert(gate);
    assert.match(source,/property Timer closeGateTimer: Timer \{\s*interval: 5000[\s\S]*?onTriggered: if \(dialog.closeGateAbort\) dialog.closeGateAbort\(\)/,
                 'QML timer must actually invoke the fail-closed abort after a bounded interval');
    function setup() {
        let callback, calls = [], deckReady;
        const deck = {enabled:true, runJavaScript(_script, cb) { deckReady = cb; }};
        const a = {editorEnabled:true,appCloseReady(cb) { callback = cb; }};
        const b = {editorEnabled:true,appCloseReady(cb) { calls.push('b'); callback = cb; }};
        const list = [a,b];
        const timer = {running:false, start() {this.running=true},stop() {this.running=false},trigger() {this.running=false;dialog.closeGateAbort()}};
        const dialog = {closeGateTimer:timer};
        const context = vm.createContext({dialog,loader:{item:deck},pinnedWindows:{get count(){return list.length},objectAt(i){return list[i]}}});
        dialog.checkEditorsForClose = vm.runInContext(`(function checkEditorsForClose(excludedId, done) {${gate[1]}})`,context);
        const result = [];
        dialog.checkEditorsForClose('', ready => result.push(ready));
        return {deck,a,b,list,timer,result,calls,get callback(){return callback},get deckReady(){return deckReady}};
    }
    const removed = setup();
    removed.deckReady(true); removed.callback(true);
    removed.list.splice(0,1); // second window shifts to index zero during callback
    removed.callback(true);
    assert.deepEqual(removed.result,[false],'removed snapshot member must never allow quit');
    assert.equal(removed.deck.enabled,true);
    const destroyed = setup();
    destroyed.deckReady(true);
    destroyed.list.pop(); // destroyed while first window's bridge callback is pending
    destroyed.callback(true);
    assert.deepEqual(destroyed.result,[false]);
    assert.equal(destroyed.deck.enabled,true);
    const lost = setup();
    assert.equal(lost.deck.enabled,false);
    assert.equal(lost.timer.running,true);
    lost.timer.trigger();
    assert.deepEqual(lost.result,[false],'lost callback must fail closed within the deadline');
    assert.equal(lost.deck.enabled,true,'timeout must unfreeze deck');
    assert.equal(lost.a.editorEnabled,true);
    assert.equal(lost.b.editorEnabled,true);
    lost.deckReady(true);
    assert.deepEqual(lost.result,[false],'late callback must not permit quit');
}

async function ordinaryCloseWaitsForPinnedAck() {
    const source = fs.readFileSync(path.join(qml, 'Main.qml'), 'utf8');
    const gate = source.match(/function checkEditorsForClose\(excludedId, done\) \{([\s\S]*?)\n    \}/);
    const request = source.match(/function requestClose\(\) \{([\s\S]*?)\n    \}/);
    assert.ok(gate && request);
    const {fan, context: page} = load('pinned.js');
    vm.runInContext('pinned={status(){}}', page);
    const pending = deferred();
    fan.state = {loaded:true,dirty:true,busy:false,baseline:'old',revision:'r1'};
    fan.editors = [{getValue:() => 'latest'}]; fan.call = () => pending.promise;
    fan.changed();
    let exits=0, flushes=0;
    const dialog = {dirty:false,selected:0,openNote(){},saveStatus:''};
    const pinWindow = {appCloseReady(cb) {
        setImmediate(() => cb(vm.runInContext('fan.closeReady()', page)));
    }};
    const pinnedWindows = {count:1,objectAt:() => pinWindow};
    const collection = {flushPendingSaves() {flushes++; return true;}};
    const ctx = vm.createContext({dialog,collection,pinnedWindows,loader:{item:null},Qt:{quit(){exits++;}}});
    dialog.closeGateTimer = {start(){},stop(){}};
    dialog.checkEditorsForClose = vm.runInContext(`(function checkEditorsForClose(excludedId, done) {${gate[1]}})`,ctx);
    dialog.requestClose = vm.runInContext(`(function requestClose() {${request[1]}})`,ctx);
    dialog.requestClose();
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(flushes,0); assert.equal(exits,0);
    assert.match(dialog.closeSaveError,/pending|retry/i);
    pending.resolve(true); await Promise.resolve(); await Promise.resolve();
    dialog.requestClose();
    for(let i=0;i<4 && !exits;i++) await new Promise(resolve => setImmediate(resolve));
    assert.equal(flushes,1); assert.equal(exits,1);
}
async function pinnedCloseDeadlineFailsClosedAndRetries() {
    const source = fs.readFileSync(path.join(qml, 'PinnedNoteWindow.qml'), 'utf8');
    const method = source.match(/function requestSafeClose\(\) \{([\s\S]*?)\n    \}/);
    const abort = source.match(/function abortSafeClose\(\) \{([\s\S]*?)\n    \}/);
    const poll = source.match(/id: closePoll[\s\S]*?onTriggered: \{([\s\S]*?)\n        \}/);
    const deadline = source.match(/id: closeDeadline\s*\n\s*interval: 5000; repeat: false\s*\n\s*onTriggered: ([^\n]+)/);
    assert.ok(method && abort && poll && deadline, 'real pinned QML handlers must enforce a five-second deadline');
    const {fan, context: page} = load('pinned.js');
    vm.runInContext('pinned={status(){}}', page);
    const oldPush = deferred(), retrySave = deferred();
    let value = 'new', closes = 0, nativeSaves = 0;
    const element = {inert: false};
    fan.frames = [{contentDocument: {getElementById: () => element}}];
    fan.editors = [{getValue: () => value}];
    fan.state = {loaded: true, dirty: false, busy: false, external: false,
                 revision: 'r1', baseline: 'old', status: 'Saved'};
    fan.call = (method, id, text) => {
        if (method === 'noteEdited') return text === 'new' ? oldPush.promise : Promise.resolve(true);
        if (method === 'saveNote') return text === 'newer' ? retrySave.promise : Promise.resolve({ok:true, revision:'r2'});
        if (method === 'probeNote') return Promise.resolve({ok:true, committed:false, revision:'r2'});
        throw Error(method);
    };
    fan.changed();
    const pinnedWindow = {documentId:'n1', closeCheckPending:false, closeAttempt:0,
        closeSaveError:'', status:'Saved', closeRequested() { closes++; }};
    const collection = {saveNow() { nativeSaves++; return true; }, lastError:''};
    const callbacks = [];
    const pinnedEditor = {runJavaScript(script, callback) {
        const result = vm.runInContext(script, page);
        if (callback) callbacks.push(() => callback(result));
    }};
    const closePoll = {running:false, start(){this.running=true}, stop(){this.running=false}};
    const closeDeadline = {running:false, start(){this.running=true}, stop(){this.running=false}};
    const ctx = vm.createContext({pinnedWindow, collection, pinnedEditor, closePoll, closeDeadline});
    pinnedWindow.requestSafeClose = vm.runInContext(`(function requestSafeClose() {${method[1]}})`,ctx);
    pinnedWindow.abortSafeClose = vm.runInContext(`(function abortSafeClose() {${abort[1]}})`,ctx);
    closePoll.onTriggered = vm.runInContext(`(function() {${poll[1]}})`,ctx);
    closeDeadline.onTriggered = vm.runInContext(`(function() {${deadline[1]}})`,ctx);
    pinnedWindow.requestSafeClose();
    assert.equal(closeDeadline.running,true);
    callbacks.shift()(); // beginClose ack
    assert.equal(closePoll.running,true);
    closePoll.onTriggered();
    callbacks.shift()(); // still pending
    assert.equal(element.inert,true);
    closePoll.onTriggered(); // callback may arrive after the deadline
    closeDeadline.onTriggered(); // simulated 5-second timer event, no real wait
    callbacks.shift()(); // stale poll callback must be ignored
    assert.equal(pinnedWindow.closeCheckPending,false);
    assert.equal(closePoll.running,false);
    assert.equal(element.inert,false,'deadline must restore the same editor and text');
    assert.equal(value,'new');
    assert.equal(nativeSaves,0);
    assert.equal(closes,0);
    assert.match(pinnedWindow.closeSaveError,/timed out|retry/i);
    const bridgeStatus = source.match(/function status\(text, dirty\) \{([^\n]*)\}/);
    vm.runInContext(`(function(text,dirty) {${bridgeStatus[1]}})`,ctx)('Saved',false);
    assert.match(pinnedWindow.status,/timed out|retry/i,'poll status cannot hide timeout');
    await fan.poll();
    vm.runInContext(`(function(text,dirty) {${bridgeStatus[1]}})`,ctx)(fan.state.status,false);
    assert.match(pinnedWindow.status,/timed out|retry/i);
    value = 'newer'; fan.changed();
    pinnedWindow.requestSafeClose();
    callbacks.shift()();
    oldPush.resolve(true);
    for(let i=0;i<5;i++) await Promise.resolve();
    assert.equal(fan.closeResult,0,'late prior close must not complete the retry');
    assert.equal(closes,0);
    retrySave.resolve({ok:true,revision:'r3'});
    for(let i=0;i<20 && fan.closeResult===0;i++) await new Promise(resolve => setImmediate(resolve));
    assert.equal(fan.closeResult,1,'retry succeeds after the timed-out push resolves');
    closePoll.onTriggered(); callbacks.shift()(); callbacks.shift()(); // result, then final readiness
    assert.equal(nativeSaves,1);
    assert.equal(closes,1);
    assert.equal(pinnedWindow.closeSaveError,'');
    assert.equal(closeDeadline.running,false);
}
async function pinnedSupersededCloseCannotUnlockOrSkipFinalValueCheck(oldSucceeded, latePushAcknowledged = false) {
    const source = fs.readFileSync(path.join(qml, 'PinnedNoteWindow.qml'), 'utf8');
    const extract = (pattern) => {
        const match = source.match(pattern);
        assert.ok(match, 'real pinned close handler must be extractable');
        return match[1];
    };
    const request = extract(/function requestSafeClose\(\) \{([\s\S]*?)\n    \}/);
    const abort = extract(/function abortSafeClose\(\) \{([\s\S]*?)\n    \}/);
    const poll = extract(/id: closePoll[\s\S]*?onTriggered: \{([\s\S]*?)\n        \}/);
    const {fan, context: page} = load('pinned.js');
    vm.runInContext('pinned={status(){}}', page);
    const oldSave = deferred(), retrySave = deferred();
    let value = 'A', native = 'original', closes = 0, nativeSaves = 0;
    const element = {inert: false};
    fan.frames = [{contentDocument: {getElementById: () => element}}];
    fan.editors = [{getValue: () => value}];
    fan.state = {loaded:true, dirty:false, busy:false, external:false,
                 revision:'r0', baseline:'original', status:'Saved'};
    fan.call = (method, id, text) => {
        if (method === 'noteEdited') return Promise.resolve(true);
        if (method === 'saveNote') {
            if (text === 'A') return oldSave.promise;
            if (text === 'B') return retrySave.promise;
            throw Error('Unexpected save value: ' + text);
        }
        throw Error(method);
    };
    const pinnedWindow = {documentId:'n1', closeCheckPending:false, closeAttempt:0,
        closeSaveError:'', status:'Saved', closeRequested() { closes++; }};
    const collection = {saveNow() { nativeSaves++; native = fan.state.baseline; return true; }, lastError:''};
    const callbacks = [];
    const pinnedEditor = {runJavaScript(script, callback) {
        const result = vm.runInContext(script, page);
        if (callback) callbacks.push(() => callback(result));
    }};
    const closePoll = {start(){}, stop(){}};
    const closeDeadline = {start(){}, stop(){}};
    const ctx = vm.createContext({pinnedWindow, collection, pinnedEditor, closePoll, closeDeadline});
    pinnedWindow.requestSafeClose = vm.runInContext(`(function requestSafeClose() {${request}})`, ctx);
    pinnedWindow.abortSafeClose = vm.runInContext(`(function abortSafeClose() {${abort}})`, ctx);
    closePoll.onTriggered = vm.runInContext(`(function() {${poll}})`, ctx);

    pinnedWindow.requestSafeClose(); callbacks.shift()();
    assert.equal(element.inert, true);
    pinnedWindow.abortSafeClose();
    assert.equal(element.inert, false, 'cancelled close restores editor');
    value = 'B'; fan.changed();
    pinnedWindow.requestSafeClose(); callbacks.shift()();
    assert.equal(element.inert, true, 'retry freezes editor');
    oldSave.resolve(oldSucceeded ? {ok:true, revision:'rA'} : {ok:false, error:'Recovery write failed'});
    for (let i=0;i<10;i++) await Promise.resolve();
    assert.equal(element.inert, true, 'superseded save continuation cannot unlock retry');
    assert.equal(fan.closeResult, 0, 'old close cannot complete retry');
    assert.equal(closes, 0);
    retrySave.resolve({ok:true, revision:'rB'});
    for (let i=0;i<20 && fan.closeResult===0;i++) await new Promise(resolve => setImmediate(resolve));
    assert.equal(fan.closeResult, 1, 'retry saves B');
    assert.equal(fan.state.baseline, 'B');
    value = 'C'; fan.changed(); // edit after bridge completion, before QML's final poll
    if (latePushAcknowledged) {
        for (let i=0; i<4; i++) await Promise.resolve();
        assert.equal(fan.state.pendingPushes, 0, 'C reached native autosave but not explicit bridge save');
    }
    closePoll.onTriggered();
    while (callbacks.length) callbacks.shift()();
    assert.equal(nativeSaves, 0, 'final value and in-flight push must be checked before native save');
    assert.equal(closes, 0, 'disk B must not close over live C');
    assert.equal(value, 'C', 'failed close preserves the live editor');
    assert.equal(element.inert, false, 'refused close restores editor input');
    assert.equal(native, 'original');
}
async function pinHandoffWaitsForReversedBridgeAcks(unreportedTail = false) {
    // Real QML handlers and the real deck JS, with only WebChannel/native timing stubbed.
    const source = fs.readFileSync(path.join(qml, 'Main.qml'), 'utf8');
    const extract = pattern => {
        const match = source.match(pattern);
        assert.ok(match, 'pin/close handlers must be extractable');
        return match[1];
    };
    const toggle = extract(/function togglePinSelected\(\) \{([\s\S]*?)\n    \}/);
    const gate = extract(/function checkEditorsForClose\(excludedId, done\) \{([\s\S]*?)\n    \}/);
    const {fan, context: page} = load('app.js');
    vm.runInContext('notes={status(){}}', page);
    let value = 'old', native = 'old', disk = 'old', removed = 0, pinOpens = 0;
    const pending = [];
    fan.editors = [{getValue: () => value}];
    fan.states = [{id:'n1', loaded:true, dirty:false, busy:false,
                   baseline:'old', revision:'r1', status:'Saved'}];
    fan.call = (method, id, text) => {
        assert.equal(method, 'noteEdited');
        assert.equal(id, 'n1');
        const ack = deferred(); pending.push({text, ack});
        return ack.promise.then(ok => { if (ok) native = text; return ok; });
    };
    const collection = {
        lastError:'', documentObject: () => ({pinned:false}),
        saveNow(id) { assert.equal(id,'n1'); disk = native; return true; },
        setPinned(id, pinned) { assert.equal(id,'n1'); assert.equal(pinned,true);
            pinOpens++; assert.equal(disk,value, 'pinned editor must open current Markdown'); return true; }
    };
    const callbacks = [];
    const deck = {enabled:true, runJavaScript(script, callback) {
        const result = vm.runInContext(script, page);
        callbacks.push(() => callback(result));
    }};
    const timer = {running:false, start(){this.running=true}, stop(){this.running=false}};
    const retry = {running:false, start(){this.running=true}, stop(){this.running=false},
                   onTriggered(){this.running=false; if (dialog.closeGateRetry) dialog.closeGateRetry();}};
    const dialog = {selectedId:'n1', pinnedIds:[], saveStatus:'',
        closeGateTimer:timer, closeGateRetryTimer:retry,
        reselectAfterFiling(){removed++;}};
    const ctx = vm.createContext({dialog, collection, loader:{item:deck},
        reselectAfterFiling: id => dialog.reselectAfterFiling(id),
        pinnedWindows:{count:0}, notesStore:{load(){return {};}}});
    dialog.checkEditorsForClose = vm.runInContext(`(function checkEditorsForClose(excludedId, done) {${gate}})`,ctx);
    dialog.togglePinSelected = vm.runInContext(`(function togglePinSelected() {${toggle}})`,ctx);
    value = 'first'; fan.changed(0);
    value = 'latest'; fan.changed(0);
    dialog.togglePinSelected();
    assert.equal(deck.enabled,false, 'handoff freezes the live editor before async callbacks');
    assert.equal(pinOpens,0);
    callbacks.shift()(); // pending bridge work is not safe
    assert.equal(pinOpens,0, 'handoff must not remove the fan note while pushes are in flight');
    // Deliver the newer call before the older one: native ends up stale until JS repairs it.
    pending[1].ack.resolve(true); await Promise.resolve(); await Promise.resolve();
    assert.equal(pinOpens,0);
    pending[0].ack.resolve(true); await Promise.resolve(); await Promise.resolve();
    assert.equal(native,'first');
    if (unreportedTail) value = 'tail typed without an editor event';
    for (let i=0; i<5 && pending.length<3; i++) {
        if (retry.running) retry.onTriggered();
        while (callbacks.length) callbacks.shift()();
        await Promise.resolve();
    }
    assert.equal(pending.length,3, 'deck must re-push the live text after reversed acknowledgments');
    assert.equal(pending[2].text,value,'handoff captures even a tail not yet reported by onChange');
    assert.equal(pinOpens,0);
    pending[2].ack.resolve(true); await Promise.resolve(); await Promise.resolve();
    for (let i=0; i<5 && !pinOpens; i++) {
        if (retry.running) retry.onTriggered();
        while (callbacks.length) callbacks.shift()();
        await Promise.resolve();
    }
    assert.equal(pinOpens,1, 'pin succeeds once the native buffer matches the live editor');
    assert.equal(disk,value);
    assert.equal(removed,1);
    assert.deepEqual(Array.from(dialog.pinnedIds),['n1']);
}
async function pinHandoffRefusesNativeSaveFailure() {
    const source = fs.readFileSync(path.join(qml, 'Main.qml'), 'utf8');
    const gate = source.match(/function checkEditorsForClose\(excludedId, done\) \{([\s\S]*?)\n    \}/);
    const toggle = source.match(/function togglePinSelected\(\) \{([\s\S]*?)\n    \}/);
    assert.ok(gate && toggle);
    const {fan, context: page} = load('app.js');
    vm.runInContext('notes={status(){}}',page);
    let value = 'old', saved = 0, pins = 0, removed = 0;
    fan.editors = [{getValue: () => value}];
    fan.states = [{id:'n1',loaded:true,dirty:false,busy:false,baseline:'old',revision:'r1',status:'Saved'}];
    fan.call = () => Promise.resolve(true);
    const deck = {enabled:true, runJavaScript(script, callback) { callback(vm.runInContext(script,page)); }};
    const timer = {start(){}, stop(){}};
    const dialog = {selectedId:'n1', pinnedIds:[], saveStatus:'', closeGateTimer:timer,
                    closeGateRetryTimer:timer};
    const collection = {documentObject: () => ({pinned:false}),
        saveNow() { saved++; return false; }, setPinned() { pins++; return true; }};
    const ctx = vm.createContext({dialog,collection,loader:{item:deck},pinnedWindows:{count:0},
        reselectAfterFiling(){removed++;}});
    dialog.checkEditorsForClose = vm.runInContext(`(function checkEditorsForClose(excludedId, done) {${gate[1]}})`,ctx);
    dialog.togglePinSelected = vm.runInContext(`(function togglePinSelected() {${toggle[1]}})`,ctx);
    value = 'new'; fan.changed(0); await Promise.resolve();
    dialog.togglePinSelected();
    assert.equal(saved,1);
    assert.equal(pins,0,'native save failure must not pin');
    assert.equal(removed,0,'failed pin must keep the fan note');
    assert.deepEqual(Array.from(dialog.pinnedIds),[]);
    assert.equal(deck.enabled,true,'failure restores the live editor');
    assert.match(dialog.saveStatus,/Pin refused/);
}
async function discardMustNotBeRecommittedAtShutdown() {
    const source = fs.readFileSync(path.resolve(qml, '../main.cpp'), 'utf8');
    assert.doesNotMatch(source, /aboutToQuit[\s\S]{0,150}flushPendingSaves\s*\(/,
        'shutdown must not recommit the selected note after requestDiscardClose excludes it');
}
(async () => { await pinnedSaveTail(); await pinnedRefusesFailedRecovery(); await pendingReloadTail();
    await deckUndoToBaseline(); await deckQuitWaitsForNativeReversion();
    await cleanQuitRefusesFailedNativeFlush(); await pinnedQmlRefusesUncommittedNativeMarkdown();
    await pinnedQmlRefusesUncommittedNativeMarkdown(null, /Close refused · Unable to save this note; retry/);
    await discardPromptFlushesOtherNativeNotes();
    await discardMustNotBeRecommittedAtShutdown();
    await pinHandoffWaitsForReversedBridgeAcks();
    await pinHandoffWaitsForReversedBridgeAcks(true);
    await pinHandoffRefusesNativeSaveFailure();
    await discardWaitsForOtherEditorsBridgeAck();
    await ordinaryCloseWaitsForPinnedAck();
    await pinnedCloseDeadlineFailsClosedAndRetries();
    await pinnedSupersededCloseCannotUnlockOrSkipFinalValueCheck(false);
    await pinnedSupersededCloseCannotUnlockOrSkipFinalValueCheck(true);
    await pinnedSupersededCloseCannotUnlockOrSkipFinalValueCheck(false, true);
    await pinnedFailedPushNeverLooksReady();
    await pinnedFailedPushPollCannotClearFailure();
    await closeGateCallbackLifetime();
    await deckQuitWaitsForOtherNoteAndKeepsFailureVisible();
    await deckNoSelectionStillGuardsPendingEdit();
    await deckExplicitSaveClearsFailedReversion(); await pinnedUndoToBaseline();
    await pinnedCloseWaitsForReversion(); await pinnedCloseWaitsForAllOutOfOrderPushes();
    await deckOutOfOrderPushCannotLookCloseReady(); await pinnedOutOfOrderPushCannotLookCloseReady();
    await pinnedCloseRefusesFailedCleanReversion();
    await staleProbeCannotCleanUncommittedTail('app.js');
    await staleProbeCannotCleanUncommittedTail('pinned.js');
    await supersededPushFailureCannotDirtyCleanReversion('app.js');
    await supersededPushFailureCannotDirtyCleanReversion('pinned.js');
    console.log('pending edits: PASS'); })().catch(error => { console.error(error); process.exitCode = 1; });
