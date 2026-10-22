'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const main = fs.readFileSync(path.join(__dirname, '../../src/shell/qml/Main.qml'), 'utf8');
const start = main.indexOf('    function followLink(href) {');
const end = main.indexOf('\n    /**', start);
const opened = [], external = [];
const documents = {local: {absolutePath:'/notes/Remediation/x.md'}, other: {absolutePath:'/notes/Other/x.md'}};
const dialog = {order:['local'], fileNames:{local:'x.md'}, openNoteId(id){opened.push(id)}, saveStatus:''};
const context = {dialog, collection:{documentObject:id=>documents[id]},
    shellControl:{rootPath:'/notes'}, Qt:{openUrlExternally:url=>external.push(url)}};
vm.createContext(context);
vm.runInContext(main.slice(start,end),context);
context.followLink('file:///notes/Other/x.md');
assert.deepEqual(opened,[], 'sibling x.md must not open current-folder x.md');
assert.deepEqual(external,['file:///notes/Other/x.md']);
external.length=0;
context.followLink('file:///notes/Remediation/x.md');
assert.deepEqual(opened,['local']);
assert.deepEqual(external,[]);
context.followLink('file:///elsewhere/missing.md');
assert.deepEqual(external,['file:///elsewhere/missing.md']);
context.followLink('file:///notes/Assets/report%20%231.pdf');
assert.equal(external.at(-1),'file:///notes/Assets/report%20%231.pdf');
for (const href of ['javascript:alert(1)','data:text/html,hello']) {
    const before=external.length;context.followLink(href);assert.equal(external.length,before);
}
console.log('PASS: exact note identity, sibling path, missing note, asset and blocked schemes');
