/** One existing editor per document: isolates Selection and engine-level listeners.
 * The note index is fixed for the life of the frame, so identity never depends on
 * fan position. Colour, typography and padding arrive purely as CSS variables.
 */
"use strict";
const index=Number(new URLSearchParams(location.search).get("note"));

let editor=new Vditor("editor",{
 cdn:new URL("vendor/package",location.href).href,lang:"en_US",mode:"ir",height:"100%",
 // Vditor leaves Tab to browser focus traversal unless its tab option is set.
 // Let the editor's own range/list/code handlers consume the trusted native key.
 tab:"	",
 // The pinned Vditor fixTab handles Tab, but its Shift+Tab branch is unimplemented.
 // Complete only literal-tab removal at a collapsed caret; list nesting, tables,
 // selection ranges and all other keys remain Vditor's own semantics. Its ensuing
 // fixTab call performs normal rendering/input/undo bookkeeping (no execCommand).
 keydown:event=>{
  if(event.key!=="Tab" || !event.shiftKey || event.ctrlKey || event.altKey || event.metaKey)return;
  if(editor.vditor.currentMode!=="ir" || !event.target.isContentEditable)return;
  const selection=getSelection();
  if(!selection.rangeCount || !selection.isCollapsed)return;
  const caret=selection.getRangeAt(0);
  const element=caret.startContainer.nodeType===Node.ELEMENT_NODE?caret.startContainer:caret.startContainer.parentElement;
  const block=element.closest("code,p,li");
  if(!block || !event.target.contains(block))return;
  const before=caret.cloneRange();before.selectNodeContents(block);before.setEnd(caret.startContainer,caret.startOffset);
  const text=before.toString();
  if(!text.endsWith("\t"))return;
  const walker=document.createTreeWalker(block,NodeFilter.SHOW_TEXT);
  let offset=text.length-1,node;
  while((node=walker.nextNode())){
   if(offset<node.length){
    // Delete exactly the verified tab, never a neighbouring authored character.
    const remove=document.createRange();remove.setStart(node,offset);remove.setEnd(node,offset+1);
    remove.deleteContents();remove.collapse(true);selection.removeAllRanges();selection.addRange(remove);return;
   }
   offset-=node.length;
  }
 },
 cache:{enable:false},value:parent.fixtures[index],
 toolbar:["edit-mode","headings","bold","italic","strike","inline-code","link","list","ordered-list","check","quote","code","table","line",
  // This NOTE's font family and size (never the selection's: nothing is written into the
  // Markdown). The click is taken by installNoteFont()'s capture-phase listener, so the
  // vendor's own handler below never runs.
  {name:"ff-font",tip:"Note font",tipPosition:"s",click:()=>{},
   icon:'<svg viewBox="0 0 24 24" aria-hidden="true"><text x="12" y="17" text-anchor="middle" font-size="14" font-weight="600" font-family="sans-serif" fill="currentColor">Aa</text></svg>'},
  "upload","record","undo","redo"],
 // Drops, pastes and recordings are COPIED INTO THE LIBRARY and referenced relatively,
 // so the notes folder stays a self-contained thing the user can move or sync.
 //
 // Vditor's contract: return a STRING to show it as an error tip, or return nothing and
 // insert the Markdown ourselves. We do the latter, because the engine — not the page —
 // decides the final filename.
 upload:{url:"/asset", max:64*1024*1024, multiple:true,
  handler:files=>{ parent.fan.importFiles(index, files); return null }},
 // Highlighting is ON and entirely local: `cdn` above points at the vendored copy, so
 // highlight.min.js, third-languages.js and the token stylesheet are all loaded from this
 // directory's vendor tree and nothing is fetched online.
 //
 // It is also what supplies the language list. The vendor reads
 // `options.preview.hljs.langs || ALIAS_CODE_LANGUAGES.concat(window.hljs?.listLanguages())`
 // for the code-block picker, so with highlighting disabled the picker can only offer the
 // sixteen renderer aliases (mermaid, flowchart, echarts…) — bash, yaml and text would be
 // missing from it. Loading hljs supplies the real list.
 preview:{delay:100,hljs:{enable:true,style:"github",lineNumber:false},
  // linkBase makes a RELATIVE asset path resolve against the NOTES FOLDER rather than
  // against this page's own directory under /usr/share/fanfold/qml. Without it
  // `![](Assets/images/x.png)` would be looked up beside editor.html and render broken,
  // which is the whole reason assets are stored relatively in the first place.
  markdown:{sanitize:true, linkBase:(parent.fan&&parent.fan.linkBase)||""}},
 input:()=>parent.fan.changed(index),
 after:()=>{ parent.fan.onReady(index,editor); installTableControls(); installRecorder(); brandUploadButton(); installNoteFont(); }
});

/**
 * A table toolbar that appears while the caret is inside a table.
 *
 * Vditor CAN insert and delete table rows and columns in instant-render mode — the
 * operations are bound to ⌘=, ⇧⌘F, ⌘-, ⇧⌘G, ⇧⌘= and ⇧⌘- — but its own table panel is a
 * WYSIWYG-mode feature, so in this editor the capability exists with nothing to click.
 * Rather than switch the whole editor to WYSIWYG (which would change how every note
 * renders), this floats a small strip above the table the caret is in and replays those
 * same key events, so the controls and the shortcuts drive one implementation.
 */
function installTableControls(){
 const bar=document.createElement("div");
 bar.className="ff-table-bar";
 bar.setAttribute("role","toolbar");
 bar.setAttribute("aria-label","Table");
 // Each entry replays the vendor's own binding: [label, tooltip, key, shift].
 //
 // The keys are what the MATCHER expects, not what the documentation prints. Vditor
 // compares `event.key` case-insensitively and, for a ⇧ binding, first rewrites "-" to
 // "_" and "=" to "+" — because that is the character a real keyboard emits with Shift
 // held. Sending "=" with shiftKey:true therefore matches nothing at all.
 const verbs=[
  ["+↑","Insert row above","F",true],
  ["+↓","Insert row below","=",false],
  ["+←","Insert column left","G",true],
  ["+→","Insert column right","+",true],
  ["−","Delete row","-",false],
  ["−|","Delete column","_",true]];
 for(const [label,tip,key,shift] of verbs){
  const b=document.createElement("button");
  b.type="button"; b.textContent=label;
  b.className="vditor-tooltipped vditor-tooltipped__s";
  b.setAttribute("aria-label",tip);
  // Mousedown, not click: the caret must still be in the cell when the verb runs.
  b.addEventListener("mousedown",event=>{
   event.preventDefault();
   const root=editorRoot(); if(!root)return;
   root.dispatchEvent(new KeyboardEvent("keydown",
    {key,ctrlKey:true,metaKey:false,shiftKey:shift,bubbles:true,cancelable:true}));
   parent.fan.changed(index);
   setTimeout(positionTableBar,0);
  });
  bar.appendChild(b);
 }
 document.body.appendChild(bar);

 /** Show the strip over the table holding the caret; hide it everywhere else. */
 function positionTableBar(){
  const selection=window.getSelection();
  let node=selection&&selection.rangeCount?selection.getRangeAt(0).startContainer:null;
  if(node&&node.nodeType===3)node=node.parentElement;
  let table=null;
  for(let n=node;n&&n!==document.body;n=n.parentElement){
   if(n.tagName==="TABLE"){table=n;break}
  }
  if(!table){bar.classList.remove("visible");return}
  const box=table.getBoundingClientRect();
  bar.style.left=Math.round(box.left)+"px";
  // Above the table when there is room, otherwise just below its top edge.
  bar.style.top=Math.round(box.top>26?box.top-24:box.bottom+4)+"px";
  bar.classList.add("visible");
 }
 document.addEventListener("selectionchange",positionTableBar);
 document.addEventListener("scroll",positionTableBar,true);
 window.addEventListener("resize",positionTableBar);
}

/** Chromium can leave Vditor's non-text closing-fence marker behind when a native
 * Select All is followed by Backspace/Delete. Let the native edit run first so Vditor
 * records the user's keyboard operation, then normalize that one full-document delete
 * through Vditor's public setValue path. Both paths share the same debounced undo
 * snapshot, so the deletion remains one undoable edit rather than a history reset. */
let replaceAllArmed=false;
let replaceAllDeletePending=false;
const editorRoot=()=>editor?.vditor?.[editor.vditor.currentMode]?.element;
document.addEventListener("keydown",event=>{
 const root=editorRoot();
 if(!root)return;
 if((event.ctrlKey||event.metaKey)&&!event.altKey&&event.key.toLowerCase()==="a"
    &&root.contains(event.target)){
  replaceAllArmed=true;
  return;
 }
 if(replaceAllArmed&&(event.key==="Backspace"||event.key==="Delete")){
  const selection=getSelection();
  replaceAllDeletePending=selection.rangeCount===1&&!selection.isCollapsed
   &&root.contains(selection.anchorNode)&&root.contains(selection.focusNode);
  replaceAllArmed=false;
  return;
 }
 if(!["Control","Meta","Shift"].includes(event.key))replaceAllArmed=false;
},true);
document.addEventListener("pointerdown",()=>{replaceAllArmed=false;replaceAllDeletePending=false;},true);
document.addEventListener("input",()=>{
 if(!replaceAllDeletePending)return;
 replaceAllDeletePending=false;
 const root=editorRoot();
 editor.setValue("");
 root.focus();
 const caret=document.createRange();
 caret.selectNodeContents(root);caret.collapse(true);
 const selection=getSelection();selection.removeAllRanges();selection.addRange(caret);
});
/** Keep the vendor's hint panel — the code-block LANGUAGE picker — inside this WebEngine
 * viewport.
 *
 * The vendor positions it from the caret alone and never consults the viewport, so near
 * the bottom or right edge of a small card the list hangs outside the window where no
 * scroll can reach it. This repositions the vendor's own element host-side: the list, its
 * order and its values are untouched, only `top`/`left` are pulled back inside, after
 * `max-height` (in editor-theme.css) has bounded it so the rest scrolls.
 * @param {HTMLElement} el The `.vditor-hint` element the engine has just shown.
 */
const MARGIN=6;
const fitHint=el=>{
 if(getComputedStyle(el).display==="none")return;
 const box=el.getBoundingClientRect();
 let dy=0,dx=0;
 if(box.bottom>innerHeight-MARGIN)dy=innerHeight-MARGIN-box.bottom;
 if(box.top+dy<MARGIN)dy=MARGIN-box.top;
 if(box.right>innerWidth-MARGIN)dx=innerWidth-MARGIN-box.right;
 if(box.left+dx<MARGIN)dx=MARGIN-box.left;
 // offsetTop/offsetLeft are the used values, so this works whether the vendor placed the
 // panel with `left` or with `right`, and re-running it is idempotent.
 if(dy){el.style.top=(el.offsetTop+dy)+"px";}
 if(dx){el.style.right="auto";el.style.left=(el.offsetLeft+dx)+"px";}
};
const hintWatch=new MutationObserver(records=>{
 for(const record of records){
  const el=record.target;
  if(el.classList&&el.classList.contains("vditor-hint"))requestAnimationFrame(()=>fitHint(el));
 }
});
hintWatch.observe(document.body,{subtree:true,attributes:true,attributeFilter:["style","class"]});
addEventListener("resize",()=>document.querySelectorAll(".vditor-hint").forEach(fitHint));
/** Test hook: the measured viewport box of the visible language picker, if any. */
window.hintMetrics=()=>{
 const el=[...document.querySelectorAll(".vditor-hint")].find(e=>getComputedStyle(e).display!=="none");
 if(!el)return null;
 const box=el.getBoundingClientRect(),style=getComputedStyle(el);
 const items=[...el.querySelectorAll("button")].map(b=>b.textContent.trim());
 return {top:box.top,left:box.left,right:box.right,bottom:box.bottom,
  width:box.width,height:box.height,scrollHeight:el.scrollHeight,clientHeight:el.clientHeight,
  overflowY:style.overflowY,color:style.color,background:style.backgroundColor,
  viewport:{w:innerWidth,h:innerHeight},items:items,count:items.length};
};
document.addEventListener("selectionchange",()=>{if(parent.fan.active===index)parent.fan.remember();});
document.addEventListener("keydown",e=>{if(e.key==="Escape"){e.preventDefault();e.stopImmediatePropagation();
 // An open note-font popover takes Escape first: closing it must not collapse the card.
 const pop=parent.appearance&&parent.appearance.noteFont;
 if(pop&&pop.isOpen()){pop.close();return;}
 parent.fan.suspend();parent.notes.collapse();}},true);
document.addEventListener("keydown",e=>{if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==="s"){e.preventDefault();e.stopImmediatePropagation();parent.fan.save(index);}},true);

/**
 * Our own record button, replacing Vditor's.
 *
 * Vditor hand-rolls a recorder on ScriptProcessorNode (deprecated since 2014), hardcodes
 * `SAMPLE_RATE = 5e3`, downsamples every capture to 5 kHz mono and assembles the WAV bytes
 * by hand. That is below telephone quality and some players refuse the result outright.
 *
 * MediaRecorder is the browser's own API: Chromium captures, encodes and containers the
 * audio in C++, off the main thread. Measured in this engine (Qt WebEngine 6.10 / Chromium
 * 134): `audio/webm;codecs=opus` is supported for BOTH record and playback. Opus is built
 * for voice and is roughly a fifth the size of raw PCM. No sample rate appears anywhere —
 * it follows the capture device, so a better microphone simply yields better audio.
 *
 * The toolbar button is intercepted in the CAPTURE phase and propagation stopped, so
 * Vditor's own handler never runs and its recorder is never constructed.
 */
/**
 * Rename and re-glyph the upload button.
 *
 * Vditor calls it "upload" and draws a cloud-arrow, which describes a server round trip
 * this application never makes: the file is COPIED INTO THE LIBRARY beside the note. The
 * freedesktop `folder-images-symbolic` mark and the label "Insert image" describe what
 * the verb actually does.
 *
 * Delegated and idempotent for the same reason as the recorder: the toolbar is built
 * lazily on first reveal, so a one-shot query at editor-ready time finds nothing.
 */
function brandUploadButton(){
 const paint = () => {
  const el = document.querySelector('[data-type="upload"]');
  if(!el || el.dataset.ffBranded) return;
  el.dataset.ffBranded = "1";
  el.setAttribute("aria-label", "Insert image");
  if(el.getAttribute("data-tip") !== null) el.setAttribute("data-tip", "Insert image");
  // Replace ONLY the <svg>. The upload item is the one toolbar entry Vditor renders as a
  // <div> rather than a <button>, precisely because it hides a working
  // <input type="file"> inside it. An innerHTML swap on the container destroys that
  // input, leaving a button with a new face and no function.
  const svg = el.querySelector("svg");
  if(svg){
   const ns = "http://www.w3.org/2000/svg";
   const fresh = document.createElementNS(ns, "svg");
   fresh.setAttribute("viewBox", "0 0 24 24");
   fresh.setAttribute("width", "16"); fresh.setAttribute("height", "16");
   fresh.setAttribute("aria-hidden", "true");
   const g = document.createElementNS(ns, "g");
   g.setAttribute("transform", "translate(1,1)");
   const path = document.createElementNS(ns, "path");
   // Breeze folder-images-symbolic, path taken VERBATIM from
   // /usr/share/icons/breeze/places/24/folder-images-symbolic.svg rather than
   // hand-approximated, so it matches the desktop's own framed-photo glyph exactly.
   path.setAttribute("d", "M 3 3 L 3 19 L 19 19 L 19 3 L 3 3 z M 4 4 L 18 4 L 18 17 L 18 18 L 4 18 L 4 17 L 4 4 z M 4 17 L 6.8 17 L 9.6 13.7 L 8.2 12 L 4 17 z M 18 17 L 13.8 12 L 9.6 17 L 18 17 z M 7 5 A 2 2 0 0 0 5 7 A 2 2 0 0 0 7 9 A 2 2 0 0 0 9 7 A 2 2 0 0 0 7 5 z ");
   path.setAttribute("fill", "currentColor");
   g.appendChild(path); fresh.appendChild(g);
   svg.replaceWith(fresh);
  }
 };
 paint();
 // The toolbar can be rebuilt (mode switches), so keep watching cheaply.
 new MutationObserver(paint).observe(document.body, {childList:true, subtree:true});
}

/**
 * The format toolbar's "Aa" entry: open this note's font popover in the host page.
 *
 * DELEGATED and capture-phase, like the recorder and upload button: the toolbar is built
 * lazily on first reveal, so there is no element to bind at `after:` time. The popover
 * lives in the parent document (it reuses the Settings panel's combobox), and this frame
 * fills the whole viewport, so the button's own rect is already in host coordinates.
 * A press anywhere else in this frame is an outside click and closes it.
 */
function installNoteFont(){
 const pop=()=>parent.appearance&&parent.appearance.noteFont;
 document.addEventListener("click",ev=>{
  const hit=ev.target.closest&&ev.target.closest('[data-type="ff-font"]');
  if(!hit)return;
  ev.preventDefault();ev.stopImmediatePropagation();
  const p=pop();if(p)p.toggle(index,hit.getBoundingClientRect());
 },true);
 document.addEventListener("mousedown",ev=>{
  const p=pop();if(!p||!p.isOpen())return;
  if(ev.target.closest&&ev.target.closest('[data-type="ff-font"]'))return;
  p.close(false);
 },true);
}

function installRecorder(){
 // DELEGATED, not bound to the element: the formatting toolbar is built lazily when the
 // footer's "B" is first pressed, so at `after:` time the record button does not exist yet.
 // A capture-phase listener on the document catches the click whenever the button appears,
 // and stopImmediatePropagation keeps Vditor's own 5 kHz recorder from ever constructing.
 let rec=null, chunks=[], stream=null;

 /**
  * Recording is indicated LOUDLY: the button turns red and pulses, its tooltip becomes
  * "Stop recording", and a live mm:ss counter appears beside it. Vditor's own
  * `vditor-menu--current` tint is too unobtrusive to notice while you are talking at
  * the microphone.
  */
 let tick=null;
 const setActive = (on) => {
  const el=document.querySelector('[data-type="record"]');
  if(!el) return;
  const host = el.parentElement || el;
  host.classList.toggle("vditor-menu--current", on);
  el.classList.toggle("ff-recording", on);
  el.setAttribute("aria-label", on ? "Stop recording" : "Start recording");
  // Vditor renders the tooltip from this attribute.
  const bubble = el.getAttribute("data-tip") !== null ? "data-tip" : "aria-label";
  el.setAttribute(bubble, on ? "Stop recording" : "Start recording");
  let clock = document.getElementById("ff-rec-clock");
  if(on){
   if(!clock){
    clock=document.createElement("span");
    clock.id="ff-rec-clock"; clock.className="ff-rec-clock";
    // INSIDE the button, absolutely positioned below the glyph. As an inline sibling the
    // clock takes layout space and shifts every toolbar button to its right the moment
    // recording starts. Out of flow, nothing moves.
    host.style.position = "relative";
    host.appendChild(clock);
   }
   const t0=Date.now();
   const paint=()=>{ const s=Math.floor((Date.now()-t0)/1000);
    clock.textContent=String(Math.floor(s/60)).padStart(2,"0")+":"+String(s%60).padStart(2,"0"); };
   paint(); clearInterval(tick); tick=setInterval(paint, 500);
  } else {
   clearInterval(tick); tick=null;
   if(clock) clock.remove();
  }
 };

 const stop = () => {
  if(rec && rec.state!=="inactive") rec.stop();
  if(stream) stream.getTracks().forEach(t=>t.stop());
  stream=null; setActive(false);
 };

 document.addEventListener("click", async ev => {
  const hit = ev.target.closest && ev.target.closest('[data-type="record"]');
  if(!hit) return;
  ev.preventDefault(); ev.stopImmediatePropagation();
  if(rec && rec.state==="recording"){ stop(); return; }
  try {
   stream = await navigator.mediaDevices.getUserMedia({audio:true});
  } catch(e) {
   parent.notes.status("Microphone unavailable: "+((e&&e.name)||e), false, false); return;
  }
  chunks=[];
  // Measured in this engine (Qt WebEngine 6.10 / Chromium 134): audio/webm;codecs=opus is
  // supported for both record and playback. Fall back to the bare container rather than
  // letting the constructor throw on a future build with a different codec set.
  const mime = MediaRecorder.isTypeSupported("audio/webm;codecs=opus") ? "audio/webm;codecs=opus"
             : (MediaRecorder.isTypeSupported("audio/webm") ? "audio/webm" : "");
  try {
   rec = mime ? new MediaRecorder(stream,{mimeType:mime}) : new MediaRecorder(stream);
  } catch(e) {
   stop(); parent.notes.status("This build cannot record audio", false, false); return;
  }
  rec.ondataavailable = e => { if(e.data && e.data.size) chunks.push(e.data); };
  rec.onstop = async () => {
   const blob=new Blob(chunks,{type:rec.mimeType||"audio/webm"}); chunks=[];
   if(!blob.size){ parent.notes.status("Nothing was recorded", false, false); return; }
   const bytes=new Uint8Array(await blob.arrayBuffer());
   let bin=""; for(let i=0;i<bytes.length;i++) bin+=String.fromCharCode(bytes[i]);
   parent.fan.importRecording(index, btoa(bin), "audio/webm");
  };
  rec.start(); setActive(true);
  parent.notes.status("Recording — click again to stop", false, false);
 }, true);
}
