/** Three resident Vditor documents, one per stable note ID.
 *
 * Identity lives in the native manifest: switching, reordering or renaming never
 * rebuilds an editor, so each document keeps its own engine undo stack, selection
 * and dirty buffer. Explicit clean reload is the only content replacement.
 */
"use strict";
let notes;
const fan=window.fan={editors:[],frames:[],ranges:[],states:[],active:0,constructions:0,manifest:null};
window.fixtures=[];
fan.call=(method,...args)=>new Promise(resolve=>notes[method](...args,resolve));
/** Publish the aggregate dirty flag for close protection, plus the SELECTED note's own
 * dirty flag, which is what the File details panel compares against the file.
 * Guarded for an empty deck: the 1 s poll calls this unconditionally, and with no notes
 * `states[active]` does not exist. */
fan.publish=()=>{if(!fan.states[fan.active]){notes.status("",false,false);return;}const other=fan.states.filter((s,i)=>i!==fan.active&&(s.dirty||s.busy)).length;notes.status(fan.states[fan.active].status+(other?" · "+other+" other note(s) unsaved":""),fan.states.some(s=>s.dirty||s.busy),!!fan.states[fan.active].dirty);};
/** The routine footer is one of two words, derived from the dirty flag and nothing else:
 * `Saved` when this buffer matches the file it was loaded from or last saved to,
 * `Unsaved` when it does not. Longer phrasings only restate the action the reader just
 * performed and can already see.
 *
 * What is NOT routine overrides both words and keeps its full reason: an external change
 * offering RELOAD, and every CONFLICT or refusal.
 *
 * Compare against the engine export captured AFTER load, never raw bytes, which may
 * normalize. */
fan.status=i=>{const s=fan.states[i];return s.dirty?(s.external?"CONFLICT · external file changed; edits kept":"Unsaved"):(s.external?"External change · click RELOAD":"Saved")};
fan.changed=i=>{const s=fan.states[i];if(!s.loaded)return;const text=fan.editors[i].getValue();s.dirty=text!==s.baseline;s.status=fan.status(i);fan.publish();
 // Autosave seam: hand the buffer to the native engine on every edit. The engine owns the
 // 250 ms debounce and the crash-recovery journal, so nothing is scheduled here. This does
 // not commit and does not change the footer wording — an unsaved buffer still reads
 // "Unsaved" until the engine's quiet period elapses and it writes.
 //
 // `pushed` records exactly WHAT was handed over, which the poll below requires:
 // re-baselining to the editor's live value when the engine reports a commit would mark
 // text typed since that push as already-saved, reporting "Saved" for characters the
 // file does not contain.
 if(s.dirty && !s.busy) { s.pushed=text; notes.noteEdited(s.id,text); }};
/** Capture id, revision and Markdown before async bridge dispatch; never target the current selection later. */
fan.save=async i=>{
 const s=fan.states[i];if(!s.loaded||s.busy)return;
 fan.changed(i);if(!s.dirty)return;
 // A save in flight has not landed yet, so the buffer still differs from the file: the
 // routine word stays "Unsaved" rather than becoming a third transient state.
 const id=s.id,expected=s.revision,text=fan.editors[i].getValue();s.busy=true;s.status="Unsaved";fan.publish();
 const r=await fan.call("saveNote",id,text,expected);s.busy=false;
 if(r.ok){s.revision=r.revision;s.baseline=text;s.external=false;s.dirty=fan.editors[i].getValue()!==text;s.status=fan.status(i);}
 else {s.status=r.error;s.dirty=true;if(r.error.includes("CONFLICT"))s.external=true;}
 fan.publish();return r;
};
/** Reload is explicit and only clean; dirty Markdown and undo are never silently replaced. */
fan.reload=async i=>{
 const s=fan.states[i];if(s.busy)return;
 if(s.dirty){s.status="CONFLICT · reload refused: copy edits first; buffer kept";fan.publish();return;}
 const before=fan.editors[i].getValue();
 s.busy=true;const r=await fan.call("loadNote",s.id);
 if(s.dirty || fan.editors[i].getValue()!==before){s.busy=false;s.dirty=true;s.status="CONFLICT · edited during reload; buffer kept";fan.publish();return;}
 if(r.ok){s.loaded=false;fan.editors[i].setValue(r.text);fan.ranges[i]=null;s.revision=r.revision;s.filename=r.filename;s.baseline=fan.editors[i].getValue();s.loaded=true;fan.frames[i].contentDocument.getElementById("editor").inert=false;s.external=false;s.dirty=false;s.status=fan.status(i);}
 else s.status=r.error;
 s.busy=false;fan.publish();
};
/** Once-a-second reconciliation against the engine. Read-only: it offers reload,
 * including while collapsed, and never advances a dirty revision.
 *
 * Polling the file digest and treating ANY movement as an external edit is only sound
 * without autosave. With autosave enabled the application's own 250 ms commit moves the
 * revision the editor is still holding, which a digest race reports as a CONFLICT on a
 * note nothing else has touched.
 *
 * The engine is the authority on which of the two happened, so the poll asks it rather
 * than racing a digest: `committed` means the bytes on disk are the buffer this editor
 * pushed, and `conflict` is the engine's own external-change verdict. */
fan.poll=async()=>{
 for(let i=0;i<fan.states.length;i++){
  const s=fan.states[i];if(!s.loaded||s.busy)continue;
  const r=await fan.call("probeNote",s.id);
  if(!r.ok){s.status=r.error;continue;}
  if(r.conflict){s.external=true;s.status=s.dirty?"CONFLICT · external change; edits kept, save refused":"External change · click RELOAD";continue;}
  if(r.committed){
   // Our own autosave landed. Adopt its revision and re-baseline to the text that was
   // ACTUALLY committed (`pushed`), never to the editor's live value: more may have been
   // typed since that push, and baselining to the live value would declare those
   // characters saved when the file does not contain them. fan.changed() immediately
   // afterwards re-compares the live buffer against the new baseline and pushes the
   // remainder, so a tail typed during the write reaches disk instead of being marked
   // clean.
   s.revision=r.revision;s.external=false;
   s.baseline=(s.pushed!==undefined)?s.pushed:fan.editors[i].getValue();
   s.dirty=false;s.status=fan.status(i);
   fan.changed(i);
   continue;
  }
  if(r.revision!==s.revision){s.external=true;s.status=s.dirty?"CONFLICT · external change; edits kept, save refused":"External change · click RELOAD";}
 }
 fan.publish();
};

/** Explicit rename of the selected note.
 *
 * Refused while that buffer is dirty or busy: a rename must never race an
 * in-flight save. The native side repeats the revision check and every name
 * rule; this layer only protects the unsaved buffer and reports the reason.
 * @param {string} title Title typed in the header field.
 * @returns {Promise<object>} Native result, or a local refusal object.
 */
fan.renameAt=async(i,title)=>{
 const s=fan.states[i];
 if(!s)return {ok:false,error:"Rename refused · no such note"};
 if(!s.loaded||s.busy){s.status="Rename refused · note is not ready";fan.publish();return {ok:false};}
 fan.changed(i);
 if(s.dirty){s.status="Rename refused · save or reload this note first";fan.publish();return {ok:false};}
 const r=await fan.call("renameNote",s.id,title,s.revision);
 // A rename is visible in the title itself, so it says nothing routine either; the note
 // is still clean, which is what the footer reports.
 if(r.ok){s.revision=r.revision||s.revision;s.filename=r.filename;s.status=fan.status(i);}
 else s.status=r.error;
 fan.publish();await fan.refresh();return r;
};
fan.renameActive=title=>fan.renameAt(fan.active,title);
/** Rename one note by identity rather than by selection; the fan order is untouched. */
fan.renameActiveId=(id,title)=>fan.renameAt(fan.states.findIndex(s=>s.id===id),title);
/** Assign one literal colour to the selected note only. The value is stored resolved, so
 * no later palette change can reach it; every other note keeps its own colour. */
fan.setPaper=async color=>{
 const r=await fan.call("setPaper",fan.states[fan.active].id,color);
 if(r.ok)await fan.refresh();else{fan.states[fan.active].status=r.error;fan.publish();}
 return r;
};
/** Select the palette offering the CHOICES. Nothing is repainted: the swatches on offer
 * change, every note keeps the literal colour it already had, and no editor is rebuilt. */
fan.setPalette=async key=>{
 const r=await fan.call("setPalette",key);
 if(r.ok)await fan.refresh();else{fan.states[fan.active].status=r.error;fan.publish();}
 return r;
};
/** Persist a fan order supplied as note IDs. Editors are untouched. */
fan.setOrder=async ids=>{const r=await fan.call("setOrder",ids);if(r.ok)await fan.refresh();return r;};
/** Re-read the native manifest and repaint per-note colours; never rebuilds editors. */
fan.refresh=async()=>{fan.manifest=await fan.call("manifest");window.appearance&&appearance.apply();return fan.manifest;};

/** file:// URL of the notes folder, so relative asset links resolve there. Set once at
 *  boot and read by each editor frame's `preview.markdown.linkBase`. */
fan.linkBase="";

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
/** @return the palette record currently offering choices, with its swatches.
 * `palettes` is the full named theme/variant CHOICE list and carries labels only, so
 * the swatches of the selected one come from the native side ready-built. */
fan.paletteOf=()=>fan.manifest.activePalette;
/** @return the whole choice list for the global theme control: curated sets first, then
 * every imported terminalcolors.com family/variant. Each entry has key, label and a
 * lower-cased `search` field for typeahead. */
fan.paletteChoices=()=>fan.manifest.palettes;
/** Assign one note's ink: "auto", or an explicit literal, low contrast included. */
fan.setInk=async(id,value)=>{
 const r=await fan.call("setInk",id,value);
 if(r.ok)await fan.refresh();else{fan.states[fan.active].status=r.error;fan.publish();}
 return r;
};
/** @return the {mode,value} of one note's ink: "auto" plus the derived colour, or
 * "explicit" plus the stored literal. */
fan.inkModeOf=i=>{
 const id=fan.states[i].id;
 return {mode:fan.manifest.inkMode[id],stored:fan.manifest.inkStored[id],painted:fan.manifest.ink[id]};
};
/** @return the {paper,ink} a note actually renders: its own stored literal colour and the
 * contrast ink computed natively from it. Deliberately independent of the palette.
 *
 * Must tolerate an EMPTY deck: slot i may not exist (empty library, or a note
 * mid-removal). Throwing here propagates out of appearance.apply() and, via
 * appearance.init(), aborts the rest of the boot chain — notes.ready() never runs and
 * the poll never starts. Default paper/ink is the correct answer for "no note". */
fan.swatchColorOf=i=>{
 const s=fan.states[i];
 if(!s||!fan.manifest||fan.manifest.paper[s.id]===undefined)
  return {paper:"#f5f0e6",ink:"#1b1b1f"};
 return {paper:fan.manifest.paper[s.id],ink:fan.manifest.ink[s.id]};
};
/** @return true when this note's literal colour is one the selected palette offers. */
fan.paperInPalette=i=>fan.paletteOf().swatches.some(s=>s.paper===fan.manifest.paper[fan.states[i].id]);

/** Store a per-document DOM range; Vditor undo remains owned by that engine. */
fan.remember=()=>{
 const frame=fan.frames[fan.active]; if(!frame)return;
 const s=frame.contentWindow.getSelection();
 if(s.rangeCount)fan.ranges[fan.active]=s.getRangeAt(0).cloneRange();
};
fan.suspend=()=>{fan.remember();fan.frames[fan.active]?.contentDocument.activeElement?.blur();};
/** Build a live editor for one note id and return its slot. Used for a note that did not
 *  exist when the page loaded — created from the empty state, restored from Archive, or
 *  opened from the Library. */
fan.build=async id=>{
 const existing=fan.slotOf(id); if(existing>=0)return existing;
 const i=fan.states.length;
 const r=await fan.call("loadNote",id);
 fan.states[i]={id,loaded:false,dirty:false,revision:r.ok?r.revision:"",baseline:"",
                status:r.ok?"Saved":r.error,external:false,busy:false,
                filename:r.ok?r.filename:id};
 fixtures[i]=r.ok?r.text:"";
 await new Promise(resolve=>{
  fan.resolveReady=resolve;fan.constructions++;
  const frame=document.createElement("iframe");fan.frames[i]=frame;
  frame.setAttribute("aria-label",fan.states[i].filename||id);
  frame.src="editor.html?note="+i;document.body.append(frame);
 });
 return i;
};
/** The slot holding this note's editor, or -1. Identity, never position. */
fan.slotOf=id=>fan.states.findIndex(s=>s&&s.id===id);
/** Switch visibility and restore native editor focus without setValue/clearStack/destroy.
 *
 * `index` is a position in the CURRENT manifest — what the QML side selected — and is
 * resolved to a note ID before touching any editor. The editor array is append-only and
 * indexed by construction order, so manifest index and editor slot diverge as soon as a
 * note is archived, trashed or created; they coincide only for a fixed, never-changing
 * deck.
 *
 * Conflating the two presents one note's content under another note's name: after
 * trashing the first of two notes, the title, spine and fan stick show the second note
 * while the editor body still shows the first — and a save from that state targets
 * whichever id the stale slot carries.
 */
fan.select=async index=>{
 const manifest=await fan.refresh();
 const id=manifest.ids[index];
 if(id===undefined)return;
 let slot=fan.slotOf(id);
 if(slot<0)slot=await fan.build(id);
 fan.focusSlot(slot);
};
/** Make one already-built SLOT the visible editor. Takes a slot, never a manifest index,
 *  so internal callers that already know which editor they mean cannot accidentally go
 *  through the index translation a second time. */
fan.focusSlot=slot=>{
 if(!fan.editors[slot])return;
 // apply(), not only render(): the HOST document's --paper/--ink carry the SELECTED
 // note's colours, and the settings panel now derives its whole surface from them.
 // Without this, switching notes left the panel wearing the previously viewed note.
 fan.remember();fan.active=slot;fan.publish();window.appearance?.apply();window.appearance?.render();
 fan.frames.forEach((el,i)=>el.classList.toggle("active",i===slot));
 requestAnimationFrame(()=>{
  fan.frames[slot].contentWindow.focus();fan.editors[slot].focus();
  const r=fan.ranges[slot];if(r&&r.startContainer.isConnected){const s=fan.frames[slot].contentWindow.getSelection();s.removeAllRanges();s.addRange(r);}
 });
};
/** Disclose the existing Vditor toolbar in place and keep the caret visible. */
fan.toggleFormatting=()=>{
 fan.remember();
 fan.frames.forEach(f=>f.contentDocument.documentElement.classList.toggle("formatting"));
 // focusSlot, not select: `fan.active` is already a SLOT. Passing it to select() would
 // re-read it as a manifest index and, once the two diverge, focus a different note.
 fan.focusSlot(fan.active);
 requestAnimationFrame(()=>{
  const doc=fan.frames[fan.active].contentDocument;
  const area=doc.querySelector(".vditor-reset");
  const node=doc.getSelection().anchorNode;
  const target=node&&node.nodeType===1?node:(node?node.parentElement:null);
  if(target&&area&&target.scrollIntoView)target.scrollIntoView({block:"nearest"});
 });
};
fan.formattingVisible=()=>fan.frames[fan.active].contentDocument.documentElement.classList.contains("formatting");
fan.selectionInfo=()=>{const s=fan.frames[fan.active].contentWindow.getSelection();return {anchor:s.anchorNode?.textContent,offset:s.anchorOffset,focus:s.focusNode?.textContent,focusOffset:s.focusOffset};};
fan.onReady=(index,editor)=>{fan.editors[index]=editor;if(editor.vditor&&editor.vditor.element)fan._caretHook(editor.vditor.element);const s=fan.states[index];if(s.revision){s.baseline=editor.getValue();s.loaded=true;s.dirty=false;s.status=fan.status(index);}else{fan.frames[index].contentDocument.getElementById("editor").inert=true;}fan.resolveReady();};
new QWebChannel(qt.webChannelTransport,async channel=>{
 notes=window.notes=channel.objects.notes;
 // Resolve relative asset links against the library folder. Must be set BEFORE the
 // editor frames are built, since each reads it when constructing its Vditor.
 const root=await new Promise(r=>notes.libraryPath(r));
 fan.linkBase=root?("file://"+String(root).replace(/\/*$/,"/")):"";
 fan.manifest=await fan.call("manifest");
 fan.states=fan.manifest.ids.map(id=>({id,loaded:false,dirty:false,revision:"",baseline:"",status:"Saved",external:false,busy:false,filename:fan.manifest.files[id]}));
 for(let i=0;i<fan.states.length;i++){
  const s=fan.states[i],r=await fan.call("loadNote",s.id);
  if(r.ok){fixtures[i]=r.text;s.revision=r.revision;s.filename=r.filename;}else{fixtures[i]="";s.status=r.error;}
  await new Promise(resolve=>{
   fan.resolveReady=resolve;fan.constructions++;
   const frame=document.createElement("iframe");fan.frames[i]=frame;
   // aria-label, not title: the accessible name is kept, the browser popup is not.
   frame.setAttribute("aria-label",s.filename||s.id);
   frame.src="editor.html?note="+i;document.body.append(frame);
  });
 }
 await appearance.init();fan.select(notes.selected);notes.ready();
 setInterval(fan.poll,1000);
 const params=new URLSearchParams(location.search);
 // The fan suite runs in bounded stages across separate processes: one drives the
 // controls, the next must find them persisted and then resets, and the last must find
 // the reset persisted.
 if(params.has("hover"))await runHoverTests(params.get("hover"));
 else if(params.has("fan"))await runFanTests(params.get("fan"));
 else if(params.has("test")||params.has("reopen"))await runTests(params.has("reopen"));
});
