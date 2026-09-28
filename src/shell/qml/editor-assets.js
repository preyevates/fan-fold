/** Getting files into a note: drops, pastes, recordings and exported icons.
 *
 * Shared by the fan's deck page (app.js) and a pinned note's page (pinned.js). Both
 * provide the same globals this file relies on: `fan` with `editors`, `active`, `call` and
 * `changed(slot)`, and `notes` with `status(text, dirty, self)`, `recordingName` and
 * `beginRecordingName()`. Loaded after the page's own script, which creates `fan`.
 */
"use strict";
/**
 * Copy dropped/pasted/recorded files into the library's Assets tree and insert links.
 *
 * A DOM File exposes no filesystem path, so the BYTES travel over the bridge as base64
 * and the engine decides the final name. The inserted Markdown is RELATIVE, keeping the
 * notes folder portable.
 *
 * @param {number} slot  Editor slot that received the drop.
 * @param {FileList|File[]} files  Files dropped, pasted or recorded.
 */
/**
 * A finished recording from the MediaRecorder button.
 *
 * Separate from importFiles because a recording has no filename of its own: the name is
 * always prompted for, with no timestamp default, so a voice note is findable by what it
 * says rather than when it happened. The shell owns the prompt and the write; this page
 * never touches the library.
 *
 * Inserts an <audio> element rather than a Markdown link so the clip plays in place.
 * Markdown has no audio syntax, so this is raw HTML in the .md — still plain text, still
 * a relative path, and Vditor renders it inline.
 */
fan.importRecording=async(slot,base64,mime)=>{
 const editor=fan.editors[slot]; if(!editor) return;
 try{
  // Ask FIRST, write second: a cancelled prompt must leave nothing behind in Assets/.
  // The prompt is modal, so the answer arrives as a property change rather than a return
  // value — WebChannel methods must return synchronously, and this one cannot.
  const name=await new Promise(resolve=>{
   notes.recordingNameChanged.connect(function handler(){
    const v=notes.recordingName;
    if(v===""){ return; }                       // still open
    notes.recordingNameChanged.disconnect(handler);
    resolve(v==="\u0000" ? "" : v);             // NUL means discarded
   });
   notes.beginRecordingName();
  });
  if(!name){ notes.status("Recording discarded",false,false); return; }
  // The ".webm" suffix is appended here rather than being part of the prompted name:
  // the container is chosen by this code, and a hand-typed suffix would be doubled.
  const r=await fan.call("importAsset",name+".webm",base64,mime||"audio/webm");
  if(!r||!r.ok){ notes.status((r&&r.error)||"That recording could not be saved",false,false); return; }
  const path=encodeURI(r.relative);
  const block=`<audio controls src="${path}"></audio>`;
  const current=editor.getValue();
  const sep=current.length&&!/\n\n$/.test(current)?(/\n$/.test(current)?"\n":"\n\n"):"";
  editor.setValue(current+sep+block+"\n", true);
  fan.changed(slot);
 }catch(e){ notes.status("That recording could not be saved",false,false); }
};
/**
 * Append an image link for a file ALREADY inside the library, such as an exported icon.
 * Same append-not-splice discipline as every other asset: predictable regardless of
 * caret position, and inside Vditor's own undo history.
 */
/**
 * The last caret placed in the editor, kept alive across the panel.
 *
 * Every control on the way to picking an icon — the footer button, the checkboxes, the
 * file dialog — steals focus and destroys the text selection, so by insert time the
 * target caret no longer exists and Vditor's insertMD silently no-ops: the buffer is
 * left byte-identical and no exception is raised. This listener snapshots the range
 * whenever the selection is inside the active editor, and appendImage restores it when
 * the live selection has been discarded. Without the snapshot, insertion succeeds only
 * when the selection happens to survive the round trip.
 */
fan._lastCaret=-1;
/** The editor lives in a same-origin iframe, so the main document's selectionchange
 *  never fires for a selection inside it and the main getSelection() cannot see it.
 *  Everything selection-related must go through the editor element's OWN document. The
 *  listener is attached lazily because the editor does not exist at load time; the flag
 *  keeps it single. */
fan._caretHook=(host)=>{
 const doc=host.ownerDocument;
 if(doc.__ffCaretHooked) return; doc.__ffCaretHooked=true;
 doc.addEventListener("selectionchange",()=>{
  const ed=fan.editors[fan.active]; if(!ed||!ed.vditor) return;
  // The MODE's content pane, not vditor.element: the outer container also holds the
  // toolbar and the two hidden mode panes, so an offset measured against it restores
  // into whichever hidden pane's text comes first, and the insert lands in a pane
  // getValue() never reads — the DOM grows while the buffer stays unchanged.
  const m=ed.vditor[ed.vditor.currentMode];
  const h=m&&m.element; if(!h) return;
  const sel=h.ownerDocument.getSelection();
  if(!sel.rangeCount || !h.contains(sel.anchorNode)) return;
  // Only trust a snapshot taken while this DOCUMENT has focus. The control that steals
  // focus (footer button, panel checkbox) lives in the PARENT document, so this
  // document's activeElement still reports the editor pane while hasFocus() goes false —
  // and the "selection reset to start" event Vditor fires on that transition arrives
  // exactly then. Filtering on hasFocus() discards those zero offsets and keeps the
  // real caret position.
  if(!h.ownerDocument.hasFocus()) return;
  const r=sel.getRangeAt(0);
  const probe=r.cloneRange();
  probe.selectNodeContents(h);
  probe.setEnd(r.startContainer, r.startOffset);
  fan._lastCaret=probe.toString().length;
 });
};

/** Walk the editor's text to a character offset and plant the caret there. */
fan._restoreCaret=(host,offset)=>{
 const doc=host.ownerDocument;
 const sel=doc.getSelection();
 const walker=doc.createTreeWalker(host,NodeFilter.SHOW_TEXT);
 let node, seen=0;
 while((node=walker.nextNode())){
  const len=node.textContent.length;
  if(seen+len>=offset){
   const r=doc.createRange();
   r.setStart(node,Math.max(0,offset-seen)); r.collapse(true);
   sel.removeAllRanges(); sel.addRange(r);
   return r;
  }
  seen+=len;
 }
 return null;
};
fan.appendImage=(slot,relative,label,inline)=>{
 const editor=fan.editors[slot]; if(!editor||!relative) return;
 const alt=String(label||relative).replace(/[\[\]~]/g,"");
 const href=encodeURI(relative);
 // WRAP is a styled MARKDOWN image (![alt~wrap](href) plus a CSS float keyed on the alt
 // suffix). Vditor's IR mode renders inline HTML as a permanently escaped source marker,
 // so <img align> can never paint. Pure Markdown; other renderers show a plain image.
 const markdown = inline
   ? "!["+alt+"~wrap]("+href+")"
   : "!["+alt+"]("+href+")";
 /**
  * Insertion point: buffer surgery, not editor APIs.
  *
  * Three layers fail in order. The icon panel's controls destroy the selection before
  * insert time; a restored DOM selection is ignored because Vditor reads its own cached
  * per-mode range; and handing that cache a fresh range makes insertMD mutate the IR
  * pane's DOM without the model ever learning of it, leaving getValue() byte-identical.
  *
  * So none of that machinery is used. The caret is kept as a character offset into the
  * rendered text, which survives re-renders; the rendered text just after the caret
  * becomes an anchor string; that anchor is located in the SOURCE buffer; and the
  * markdown is spliced in before it with setValue, the one API that round-trips
  * reliably.
  */
 let value=editor.getValue();
 let at=-1;
 const v=editor.vditor;
 const host=v && v[v.currentMode] && v[v.currentMode].element;
 if(host) fan._caretHook(host);
 if(host && fan._lastCaret>=0){
  const doc=host.ownerDocument;
  const walker=doc.createTreeWalker(host,NodeFilter.SHOW_TEXT);
  let node, seen=0, anchor="";
  while((node=walker.nextNode())){
   const t=node.textContent, len=t.length;
   if(anchor.length===0 && seen+len>fan._lastCaret){
    anchor=t.substr(Math.max(0,fan._lastCaret-seen));
   } else if(anchor.length>0){
    anchor+=t;
   }
   if(anchor.length>=24) break;
   seen+=len;
  }
  anchor=anchor.substr(0,24).trim();
  if(anchor.length>=6) at=value.indexOf(anchor);
 }
 if(at>=0){
  value=value.slice(0,at)
    + (inline ? markdown+" " : "\n\n"+markdown+"\n\n")
    + value.slice(at);
 } else {
  // No caret was ever placed in this note, so append at the end.
  value=value.replace(/\s*$/,"")+"\n\n"+markdown+"\n";
 }
 editor.setValue(value);
 fan.changed(slot);
};
fan.importFiles=async(slot,files)=>{
 const editor=fan.editors[slot]; if(!editor||!files) return;
 try{
  for(const file of Array.from(files)){
   try{
    const base64=await new Promise((resolve,reject)=>{
     const reader=new FileReader();
     // readAsDataURL gives "data:<mime>;base64,<payload>"; only the payload crosses.
     reader.onload=()=>resolve(String(reader.result).split(",")[1]||"");
     reader.onerror=()=>reject(reader.error);
     reader.readAsDataURL(file);
    });
    const r=await fan.call("importAsset",file.name||"asset",base64,file.type||"");
    if(!r||!r.ok){ notes.status((r&&r.error)||"That file could not be added",false,false); continue; }
    // An image renders inline; anything else becomes a plain link the desktop can open.
    // The link is APPENDED rather than spliced at the caret: a caret left in the title
    // splices the link into the heading, and insertMD can place the block off-screen.
    // Appending through setValue is predictable regardless of caret position and keeps
    // the whole insertion inside Vditor's own undo history.
    const path=encodeURI(r.relative);
    const label=(file.name||r.relative).replace(/[\[\]]/g,"");
    const markdown=r.bucket==="images" ? `![${label}](${path})` : `[${label}](${path})`;
    const current=editor.getValue().replace(/\s*$/,"");
    editor.setValue(current+"\n\n"+markdown+"\n");
    fan.changed(slot);
   }catch(e){ notes.status("That file could not be read",false,false); }
  }
 } finally {
  // ALWAYS give the editor back. Vditor sets contenteditable="false" when recording
  // starts and restores it only inside its XHR upload branch, which a custom upload
  // handler bypasses entirely: the note otherwise accepts the recording, writes the
  // file, inserts the link, and then refuses every keystroke. Recovery belongs in
  // `finally` so a failed import cannot leave the note read-only either.
  fan.releaseEditor(slot);
 }
};

/** Re-enable editing after an operation that took the buffer away (recording, upload). */
fan.releaseEditor=slot=>{
 const editor=fan.editors[slot]; if(!editor) return;
 const v=editor.vditor; if(!v) return;
 const element=v[v.currentMode] && v[v.currentMode].element;
 if(element) element.setAttribute("contenteditable","true");
 if(v.upload) v.upload.isUploading=false;
 if(v.tip && v.tip.hide) v.tip.hide();
};
