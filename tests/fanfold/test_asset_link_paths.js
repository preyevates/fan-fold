'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
(async()=>{
    for(const prefix of ['../', '', '../../']) {
        let text='Original';
        const fan={active:0,states:[{assetPrefix:prefix}],editors:[{getValue:()=>text,setValue:v=>{text=v}}],
            changed(){},call:async()=>({ok:true,relative:'Assets/report #1.pdf',bucket:'files'})};
        const notes={status(){},recordingName:'Voice',
            recordingNameChanged:{connect(handler){this.handler=handler},disconnect(){}},
            beginRecordingName(){this.recordingNameChanged.handler()}};
        class FileReader {
            readAsDataURL(){this.result='data:application/pdf;base64,QQ==';this.onload()}
        }
        const context={fan,notes,FileReader,console};vm.createContext(context);
        vm.runInContext(fs.readFileSync(path.join(__dirname,'../../src/shell/qml/editor-assets.js'),'utf8'),context);
        await fan.importFiles(0,[{name:'report #1.pdf',type:'application/pdf'}]);
        assert.match(text,new RegExp('\\('+prefix.replaceAll('.','\\.')+'Assets/report%20%231.pdf\\)'), 'imported file must resolve against note folder');
        text='Original';
        fan.appendImage(0,'Assets/icons/icon #1.svg','Icon',false);
        assert.ok(text.includes(`(${prefix}Assets/icons/icon%20%231.svg)`));
        text='Original';
        await fan.importRecording(0,'QQ==','audio/webm');
        assert.ok(text.includes(`src="${prefix}Assets/report%20%231.pdf"`));
    }
    console.log('PASS: note-relative imported file, image and recording paths at three depths');
})().catch(error=>{console.error(error);process.exitCode=1});
