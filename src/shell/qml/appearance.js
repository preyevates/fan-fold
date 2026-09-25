/** Global appearance (typography, geometry, padding and icon sizes) plus the ONE
 * global Palette control.
 *
 * The Palette control is the only colour control here, and it is a CHOOSER: it decides
 * which swatches the round footer button offers and nothing else. Selecting a palette
 * repaints no note — each note stores its own resolved colour, so a palette change
 * cannot reach it. There is deliberately no competing per-note control in this panel; a
 * note's own colour is chosen from that note's footer button, next to the note it
 * changes. The selection lives in the notes manifest rather than appearance.json because
 * it belongs with the notes.
 *
 * There is no global colour of any kind: a tab label inherits its own note's ink,
 * automatic or explicitly chosen for that note.
 *
 * The panel itself is fixed neutral chrome (appearance.css) rather than a piece of the
 * note it settles over, so it reads the same over every paper. Persistence is serialized;
 * native validation is authoritative. Editor content, history and caret are never written.
 */
window.appearance={value:null,queue:Promise.resolve(),open:false,families:[],systemFamily:""};
const ap=window.appearance;
/** What the RETIRED sans/serif/mono enum meant, kept only to migrate a configuration
 * written before the family control existed. The migration is in memory: an older
 * appearance.json keeps its current look and is never rewritten at startup, and the key
 * leaves the file only if the user later saves a real family. */
const LEGACY_FONTS={sans:{family:"Noto Sans",generic:"sans-serif"},
                    serif:{family:"Noto Serif",generic:"serif"},
                    mono:{family:"DejaVu Sans Mono",generic:"monospace"}};
/** @return {{family:string,generic:string,source:string}} the family that should actually
 * be painted. A stored family that this machine does not have resolves to the empty
 * family plus the CSS generic — the platform's own default — rather than to a name
 * nothing can render. Nothing here writes the preference back. */
ap.fontChoice=()=>{
 const s=ap.value,list=ap.families;
 const known=name=>list.indexOf(name)>=0;
 const saved=String(s.fontFamilyName||"").trim();
 if(saved)return known(saved)?{family:saved,generic:"sans-serif",source:"chosen"}
                             :{family:"",generic:"sans-serif",source:"unavailable"};
 const legacy=LEGACY_FONTS[s.fontFamily]||LEGACY_FONTS.sans;
 return known(legacy.family)?{family:legacy.family,generic:legacy.generic,source:"legacy"}
                            :{family:"",generic:legacy.generic,source:"legacy-missing"};
};
/** CSS quoted-string encoding, not sanitization: preserve the exact family name.
 * setProperty receives one value, never a stylesheet rule or a URL. Native validation
 * remains a separate boundary; even a future broader catalog cannot escape this string. */
ap.quoteFamily=family=>'"'+String(family).replace(/["\\\x00-\x1f\x7f]/g,c=>
 c==='"'||c==='\\'?'\\'+c:'\\'+c.charCodeAt(0).toString(16)+' ')+'"';
ap.cssFont=()=>{
 const choice=ap.fontChoice();
 return (choice.family?ap.quoteFamily(choice.family)+', ':"")+choice.generic;
};
const hexOf=value=>{
 const m=/^#([0-9a-f]{6})$/i.exec(String(value||"").trim());
 return m?m[1]:null;
};
/** Qt's own darkening: QColor::darker() scales the HSV VALUE component, which is why
 * scaling every channel by the same ratio reproduces it exactly — hue and saturation are
 * ratios between the channels and survive untouched. 1.14 is the factor the native card
 * already uses for its left spine, so the code tone and the spine are the same colour by
 * construction rather than by two numbers that happen to agree. */
ap.spineOf=paper=>{
 const hex=hexOf(paper);
 if(!hex)return String(paper);
 const c=[0,2,4].map(i=>parseInt(hex.slice(i,i+2),16));
 const peak=Math.max(...c);
 if(peak===0)return "#"+hex;
 const ratio=Math.max(0,Math.min(255,Math.round(peak/1.14)))/peak;
 return "#"+c.map(v=>Math.max(0,Math.min(255,Math.round(v*ratio))).toString(16).padStart(2,"0")).join("");
};
/** WCAG 2.1 relative luminance of an #rrggbb value. */
ap.luminance=color=>{
 const hex=hexOf(color);
 if(!hex)return 1;
 const part=i=>{const v=parseInt(hex.slice(2*i,2*i+2),16)/255;
                return v<=0.03928?v/12.92:Math.pow((v+0.055)/1.055,2.4)};
 return 0.2126*part(0)+0.7152*part(1)+0.0722*part(2);
};
/** The black/white endpoint with the better ACTUAL contrast against this surface. Used
 * for the untokenised source text so a fence is legible on any note colour. */
ap.readableInk=color=>{
 const l=ap.luminance(color);
 return ((l+0.05)/0.05)>=(1.05/(l+0.05))?"#101010":"#f4f4f4";
};
/** Lift the vendored light-background token palette onto a dark spine WITHOUT flattening
 * it: brightness scales lightness, saturate keeps the hue separation that is the whole
 * point of the colours. A light spine needs nothing and gets nothing. */
ap.tokenBoost=spine=>{
 const l=ap.luminance(spine);
 if(l>=0.42)return "none";
 return l<0.12?"brightness(3.1) saturate(1.25)":"brightness(2.2) saturate(1.15)";
};
/** Push global typography plus each note own paper/ink into the resident iframes. */
ap.apply=()=>{
 // Callable before init() has landed a value (e.g. a colour pick racing a slow editor
 // build): painting nothing is correct, and throwing here would break every later
 // repaint in the page.
 if(!ap.value)return;
 const s=ap.value,font=ap.cssFont();
 fan.frames.forEach((frame,i)=>{
  const style=frame.contentDocument.documentElement.style,preset=fan.swatchColorOf(i);
  const spine=ap.spineOf(preset.paper);
  // The note's OWN typography override, when it has one; the global value otherwise.
  // A stored family this machine lacks falls back to the global font rather than to a
  // name nothing can render. Tab labels and panels (the host document below) keep the
  // global type regardless.
  const own=fan.fontOf?fan.fontOf(i):{family:"",size:0};
  const noteFont=own.family&&ap.families.indexOf(own.family)>=0?ap.quoteFamily(own.family)+", sans-serif":font;
  const noteSize=own.size>0?own.size:s.fontSize;
  const values={paper:preset.paper,ink:preset.ink,spine:spine,codeink:ap.readableInk(spine),
   tokenboost:ap.tokenBoost(spine),font:noteFont,size:noteSize+"px",
   leading:s.lineSpacing,padx:s.padX+"px",pady:s.padY+"px",tabs:s.tabSpacing,icon:s.iconSize+"px",
   // The footer command palette's own geometry, handed to the editor so the format
   // toolbar can be built from the SAME numbers rather than its own fixed padding:
   // box = icon + 12 (6px a side), gap = 2. Mirrors `surface.actionSize`/`actionGap`
   // in Main.qml — change the rule there and both strips still agree.
   actionsize:(s.iconSize+12)+"px",actiongap:"2px",
   // Chromium draws <audio controls> with its own closed shadow root: the play triangle,
   // timer and seek line cannot be selected, let alone recoloured. What reaches them is
   // `color-scheme`, which flips the whole widget between the browser's light and dark
   // renderings. Pick by the PAPER's luminance — the same test that chooses the auto ink —
   // so a dark note gets the light-on-dark control and a pale note keeps the dark-on-light
   // one. With our CSS stripping the capsule background, the widget then sits directly on
   // the paper instead of on a white pill.
   audioscheme:ap.luminance(preset.paper)<0.42?"dark":"light"};
  // Property assignment only. No rule is parsed, no document is rewritten, no editor
  // value is replaced: a paper change repaints the code block and its source live and
  // the undo history and caret are untouched.
  Object.entries(values).forEach(([k,v])=>style.setProperty("--"+k,v));
 });
 // The host document carries the selected note's paper, ink and the global type; the
 // settings panel DERIVES its whole surface from them (see appearance.css), so it
 // repaints with the note exactly as the File details and Library panels do.
 const current=fan.swatchColorOf(fan.active),host=document.documentElement.style;
 Object.entries({paper:current.paper,ink:current.ink,spine:ap.spineOf(current.paper),font:font,
  size:s.fontSize+"px",icon:s.iconSize+"px"}).forEach(([k,v])=>host.setProperty("--"+k,v));
 notes.previewAppearance(s);ap.render();if(ap.noteFont)ap.noteFont.render();
};
/** The status line is for FAILURE only. A successful save is evident from the note
 * changing under the panel, so there is no routine "saved" footer; an explicit error —
 * an unsafe path, a refused write — appears here and stays until the next successful
 * write clears it. Nothing about this changes what is persisted. */
ap.status=(message,failed)=>{
 const el=document.querySelector("#appearance-status");
 if(!el)return;
 el.textContent=failed?message:"";el.hidden=!failed;
};
ap.persist=()=>{const snapshot=JSON.parse(JSON.stringify(ap.value));ap.queue=ap.queue.then(async()=>{const r=await fan.call("saveAppearance",snapshot);if(r.ok){ap.value=r.settings;ap.apply();}ap.status(r.error,!r.ok);return r});return ap.queue;};
/** One control, one value, in the type the native normalizer expects: number, bool, or the
 * trimmed colour string whose empty case means automatic per-note ink. */
ap.readControl=el=>el.type==="checkbox"?el.checked:(el.type==="number"?Number(el.value):String(el.value).trim());
/** Update a single global field; the selection snapshot stays in the resident iframe. */
ap.change=async(key,value)=>{ap.value[key]=value;ap.apply();return ap.persist();};
/** Reset covers typography and geometry only: note colours, the palette and every
 * Markdown byte are left exactly as they were. */
ap.reset=async()=>{await ap.queue;const r=await fan.call("resetAppearance");if(r.ok){ap.value=r.settings;ap.apply();}ap.status(r.error,!r.ok);};
ap.toggle=force=>{fan.remember();ap.open=typeof force==="boolean"?force:!ap.open;document.querySelector("#settings").hidden=!ap.open;if(ap.open){if(ap.refreshLibraryPath)ap.refreshLibraryPath();for(const frame of fan.frames)if(frame)frame.contentDocument.querySelectorAll(".vditor-hint").forEach(h=>h.style.display="none");}else fan.focusSlot(fan.active);ap.render();};
/** One accessible in-page combobox, shared by BOTH long lists in this panel.
 *
 * ROOT CAUSE for not using a native <select> here: a select's dropdown is not part of the
 * page at all. Chromium renders it as a platform popup that no stylesheet in this document
 * can reach, so it arrives with a thick pale platform scrollbar sitting beside the panel's
 * own slim dark one. No CSS fixes that, because there is no element to style. So the list
 * becomes real DOM: a button plus a listbox, no library and no new dependency.
 *
 * What the platform gave away for free is replaced deliberately, not dropped: type-to-
 * search becomes an explicit search field with substring typeahead over the whole label
 * (family name included), Up/Down/Home/End move the active option, Enter commits, Escape
 * closes and gives the keyboard back to the button it came from, and the list is height-
 * bounded against the real viewport and flips above the button near the bottom edge, so
 * every one of the 100+ entries stays reachable by scrolling rather than hanging off the
 * card. The scrollbar is the panel's own slim one, because now it is ours to style.
 */
ap.combos={};
ap.makeCombo=(id,ariaLabel,onPick)=>{
 const root=document.createElement("div");root.className="combo";
 const button=document.createElement("button");
 button.type="button";button.className="combo-button";button.id=id;
 button.setAttribute("role","combobox");button.setAttribute("aria-haspopup","listbox");
 button.setAttribute("aria-expanded","false");button.setAttribute("aria-controls",id+"-list");
 button.setAttribute("aria-label",ariaLabel);
 const valueText=document.createElement("span");valueText.className="combo-value";
 const caret=document.createElement("span");caret.className="combo-caret";caret.setAttribute("aria-hidden","true");caret.textContent="▾";
 button.append(valueText,caret);
 // The popup is a child of the DOCUMENT, not of the row it belongs to.
 //
 // `.settings-body` scrolls (`overflow:auto`) and `#settings` clips (`overflow:hidden`),
 // so a list positioned inside the row would be sliced off at the panel edge the moment
 // it was longer than the space below the button. Positioned against the viewport
 // instead, it is bounded by the real WebEngine viewport in fit() below and nothing can
 // clip it.
 const pop=document.createElement("div");pop.className="combo-pop";pop.hidden=true;
 pop.dataset.combo=id;
 const search=document.createElement("input");
 search.type="text";search.className="combo-search";search.autocomplete="off";search.spellcheck=false;
 search.setAttribute("aria-label",ariaLabel+" — type to search");
 const list=document.createElement("ul");
 list.className="combo-list";list.id=id+"-list";list.setAttribute("role","listbox");
 list.setAttribute("aria-label",ariaLabel);
 pop.append(search,list);root.append(button);document.body.append(pop);
 const state={items:[],value:"",filtered:[],active:-1,open:false};
 const labelOf=value=>{const found=state.items.find(i=>i.value===value);return found?found.label:""};
 const setActive=index=>{
  state.active=index;
  [...list.children].forEach((li,i)=>li.classList.toggle("active",i===index));
  const li=index>=0?list.children[index]:null;
  if(li&&li.id){li.scrollIntoView({block:"nearest"});search.setAttribute("aria-activedescendant",li.id)}
  else search.removeAttribute("aria-activedescendant");
 };
 const render=()=>{
  const q=search.value.trim().toLowerCase();
  state.filtered=q?state.items.filter(i=>i.search.indexOf(q)>=0):state.items.slice();
  list.textContent="";
  state.filtered.forEach((item,index)=>{
   const li=document.createElement("li");
   li.className="combo-option";li.setAttribute("role","option");
   li.id=id+"-option-"+index;li.dataset.value=item.value;
   li.textContent=item.group?item.group+" · "+item.label:item.label;
   li.setAttribute("aria-selected",String(item.value===state.value));
   // mousedown, not click: the search field must not lose focus before the pick lands.
   li.addEventListener("mousedown",event=>{event.preventDefault();pick(item.value)});
   list.append(li);
  });
  if(!state.filtered.length){const empty=document.createElement("li");empty.className="combo-empty";empty.textContent="No match";list.append(empty)}
  const at=state.filtered.findIndex(i=>i.value===state.value);
  setActive(state.filtered.length?(at>=0?at:0):-1);
 };
 /** Place and bound the list against the ACTUAL viewport: never off an edge, never
  *  taller than the space it has, flipped above the button when below is too tight, and
  *  always scrollable rather than truncated — so every entry stays reachable. */
 const fit=()=>{
  const box=button.getBoundingClientRect(),margin=6;
  const below=window.innerHeight-box.bottom-margin,above=box.top-margin;
  const up=below<150&&above>below;
  const width=Math.max(120,Math.min(box.width,window.innerWidth-2*margin));
  pop.style.width=width+"px";
  pop.style.left=Math.max(margin,Math.min(box.left,window.innerWidth-width-margin))+"px";
  list.style.maxHeight=Math.max(64,Math.min(220,(up?above:below)-40))+"px";
  // Height is only known once max-height is applied, so the top is set afterwards.
  const height=pop.getBoundingClientRect().height;
  pop.style.top=(up?Math.max(margin,box.top-margin-height):Math.min(box.bottom+3,window.innerHeight-margin-height))+"px";
 };
 const open=()=>{
  if(state.open)return;
  state.open=true;pop.hidden=false;button.setAttribute("aria-expanded","true");
  search.value="";render();fit();search.focus();
 };
 const close=restore=>{
  if(!state.open)return;
  state.open=false;pop.hidden=true;button.setAttribute("aria-expanded","false");
  if(restore)button.focus();
 };
 const pick=value=>{
  state.value=value;valueText.textContent=labelOf(value)||value;
  close(true);onPick(value);
 };
 const move=step=>{
  if(!state.filtered.length)return;
  const at=state.active<0?0:(state.active+step+state.filtered.length)%state.filtered.length;
  setActive(at);
 };
 button.addEventListener("click",()=>state.open?close(true):open());
 button.addEventListener("keydown",event=>{
  if(event.key==="ArrowDown"||event.key==="Enter"||event.key===" "){open();event.preventDefault()}
 });
 search.addEventListener("input",()=>{render();fit()});
 search.addEventListener("keydown",event=>{
  if(event.key==="ArrowDown"){move(1);event.preventDefault()}
  else if(event.key==="ArrowUp"){move(-1);event.preventDefault()}
  else if(event.key==="Home"){setActive(0);event.preventDefault()}
  else if(event.key==="End"){setActive(state.filtered.length-1);event.preventDefault()}
  else if(event.key==="Enter"){if(state.active>=0)pick(state.filtered[state.active].value);event.preventDefault()}
  else if(event.key==="Escape"){
   // Escape belongs to the open list, not to the card behind it: stop it here so the
   // note is never collapsed out from under a dropdown, and give the button the keyboard.
   close(true);event.preventDefault();event.stopPropagation();
  }
  else if(event.key==="Tab")close(false);
 });
 document.addEventListener("mousedown",event=>{
  if(state.open&&!root.contains(event.target)&&!pop.contains(event.target))close(false);
 });
 window.addEventListener("resize",()=>{if(state.open)fit()});
 // The panel it belongs to can scroll or close underneath an open list.
 const body=()=>document.querySelector("#settings .settings-body");
 document.addEventListener("scroll",()=>{if(state.open)fit()},true);
 const panelWatch=new MutationObserver(()=>{
  const panel=document.querySelector("#settings");
  if(state.open&&panel&&panel.hidden)close(false);
 });
 requestAnimationFrame(()=>{
  const panel=document.querySelector("#settings");
  if(panel)panelWatch.observe(panel,{attributes:true,attributeFilter:["hidden"]});
  void body();
 });
 const api={root,button,pop,search,list,state,
  /** @param items [{value,label,group?}] @param value the currently selected value */
  set(items,value){
   state.items=items.map(i=>({value:i.value,label:i.label,group:i.group||"",
    search:((i.group?i.group+" ":"")+i.label).toLowerCase()}));
   api.select(value);if(state.open)render();
  },
  select(value){state.value=value;valueText.textContent=labelOf(value)||String(value||"")},
  open,close,isOpen:()=>state.open,
  /** Test hook: what the list is actually offering and where it actually is. */
  metrics(){
   const box=list.getBoundingClientRect(),style=getComputedStyle(list);
   return {open:state.open,total:state.items.length,shown:state.filtered.length,
           value:state.value,label:valueText.textContent,active:state.active,
           activeLabel:state.active>=0&&state.filtered[state.active]?state.filtered[state.active].label:"",
           top:box.top,left:box.left,right:box.right,bottom:box.bottom,
           height:box.height,scrollHeight:list.scrollHeight,clientHeight:list.clientHeight,
           overflowY:style.overflowY,scrollbarWidth:style.scrollbarWidth,
           scrollbarColor:style.scrollbarColor,
           viewport:{w:window.innerWidth,h:window.innerHeight},
           expanded:button.getAttribute("aria-expanded"),
           focus:document.activeElement===search?"search":(document.activeElement===button?"button":"other")};
  }};
 ap.combos[id]=api;
 return api;
};
/** The per-note font popover opened from the editor's format toolbar ("Aa").
 *
 * One note may deviate from the global Settings font and size; the global values stay the
 * default and are not touched here. Per NOTE, never per selection: nothing is written
 * into the Markdown, so no span or inline HTML ever reaches the file. The override lives
 * in library metadata beside the note's paper/ink and arrives here through the manifest.
 *
 * It wears the note's own paper/ink through the same derived tokens as the Settings panel
 * (appearance.css) and reuses the SAME accessible font combobox rather than a second list.
 * Every change is a CSS-variable repaint of the resident editor: text, caret and undo
 * history are never touched. Escape and a click anywhere outside close it.
 */
ap.makeNoteFontPanel=()=>{
 const panel=document.createElement("div");
 panel.id="note-font";panel.hidden=true;panel.setAttribute("role","dialog");
 panel.setAttribute("aria-label","This note's font");
 panel.innerHTML="<label class=\"row theme-row\"><span>Font</span><span class=\"combo-slot\" id=\"note-font-slot\"></span></label>"
  +"<label class=\"row\"><span>Size</span><input type=\"number\" id=\"note-font-size\" step=\"1\"></label>"
  +"<div class=\"row actions\"><button type=\"button\" id=\"note-font-default\">Default</button></div>";
 document.body.append(panel);
 const size=panel.querySelector("#note-font-size"),reset=panel.querySelector("#note-font-default");
 const state={slot:-1};
 const idOf=()=>fan.states[state.slot]?fan.states[state.slot].id:"";
 const combo=ap.makeCombo("note-font-select","Note font",family=>{
  const id=idOf();if(!id)return;
  fan.setNoteFont(id,family,fan.fontOf(state.slot).size);
 });
 panel.querySelector("#note-font-slot").append(combo.root);
 const api={panel,combo,size,reset,state,
  isOpen:()=>!panel.hidden,
  /** Show what this note actually PAINTS: its override when set, the global otherwise. */
  render(){
   if(panel.hidden||!ap.value)return;
   const own=fan.fontOf(state.slot),range=(fan.manifest&&fan.manifest.fontSizeRange)||{min:12,max:28};
   combo.set(ap.families.map(f=>({value:f,label:f})),
    own.family&&ap.families.indexOf(own.family)>=0?own.family:(ap.fontChoice().family||ap.systemFamily||""));
   size.min=range.min;size.max=range.max;
   if(document.activeElement!==size)size.value=own.size>0?own.size:ap.value.fontSize;
   panel.classList.toggle("overridden",!!(own.family||own.size));
  },
  /** @param slot editor slot  @param box the toolbar button's rect (iframe = viewport). */
  open(slot,box){
   fan.remember();state.slot=slot;panel.hidden=false;api.render();
   const margin=6,w=panel.offsetWidth,h=panel.offsetHeight;
   panel.style.left=Math.max(margin,Math.min(box.left,window.innerWidth-w-margin))+"px";
   panel.style.top=Math.max(margin,Math.min(box.bottom+4,window.innerHeight-h-margin))+"px";
   combo.button.focus();
  },
  close(restore){
   if(panel.hidden)return;
   combo.close(false);panel.hidden=true;
   if(restore!==false)fan.focusSlot(fan.active);
  },
  toggle(slot,box){api.isOpen()&&state.slot===slot?api.close():api.open(slot,box)}};
 size.addEventListener("input",()=>{
  if(size.value===""||!size.checkValidity())return;
  const id=idOf();if(!id)return;
  fan.setNoteFont(id,fan.fontOf(state.slot).family,Math.round(Number(size.value)));
 });
 reset.addEventListener("click",async()=>{
  const id=idOf();if(!id)return;
  await fan.setNoteFont(id,"",0);size.blur();api.render();
 });
 panel.addEventListener("keydown",event=>{
  if(event.key==="Escape"){event.preventDefault();event.stopPropagation();api.close()}
 });
 document.addEventListener("mousedown",event=>{
  if(!panel.hidden&&!panel.contains(event.target)&&!combo.pop.contains(event.target))api.close(false);
 });
 return api;
};
ap.render=()=>{
 if(!ap.value)return;
 document.querySelectorAll("[data-setting]").forEach(el=>{
  const v=ap.value[el.dataset.setting];
  // A checkbox carries its state in `checked`, and a blank colour field must render as
  // blank rather than as the string "undefined", which is what a bare .value= would do.
  if(el.type==="checkbox")el.checked=v===true;else el.value=v===undefined||v===null?"":v;
 });
 // The panel says what each control does and nothing else: the note's current hex, the
 // "not in this palette" aside and the theme's source/license line are deliberately not
 // shown. Provenance stays in the Credits section and the catalog source notices.
 // The font control shows the family that is actually PAINTED, which is not always the
 // one stored: an unavailable family resolves to this platform's default and says so.
 const font=ap.combos["font-select"];
 if(font){
  const choice=ap.fontChoice();
  font.select(choice.family||ap.systemFamily||"");
  font.button.classList.toggle("combo-fallback",choice.source==="unavailable");
 }
};
ap.init=async()=>{
 const panel=document.createElement("aside");panel.id="settings";panel.hidden=true;panel.setAttribute("aria-label","Global appearance settings");
 // Plain grouped rows: a heading that names the thing, then one control per line whose
 // label is what that control does. No paragraph explains a control that a two-word label
 // already names, and no row exists that does not drive something.
 const html=[
  "<header><img id=\"settings-appicon\" alt=\"\" aria-hidden=\"true\"><strong>Fan Fold</strong><span class=\"header-sub\">Settings</span><button id=\"close-settings\" aria-label=\"Close settings\">×</button></header>",
  "<small class=\"lede\">Adjust how your notes look and behave</small>",
  "<div class=\"settings-body\">",
  // Where the notes LIVE, named plainly. The folder-glyph button raises the same native
  // chooser as the welcome panel; the row itself shows the current path in full.
  "<section><h2>Notes Folder</h2>",
  "<label class=\"row\"><span id=\"library-path\">none chosen</span><button id=\"library-change\" class=\"themed-action\" aria-label=\"Change notes folder\">📁</button></label>",
  "</section>",
  // The theme chooser is NOT here: it lives in the note's colour panel, beside the
  // swatches it repopulates. Settings keeps only the geometry the theme cannot carry.
  "<section><h2>Notes</h2>",
  "<label class=\"row\"><span>Corner Radius</span><input type=\"number\" data-setting=\"radius\" min=\"0\" max=\"28\"></label>",
  "<label class=\"row theme-row\"><span>Font</span><span class=\"combo-slot\" id=\"font-slot\"></span></label>",
  "<label class=\"row\"><span>Size</span><input type=\"number\" data-setting=\"fontSize\" min=\"12\" max=\"28\" step=\"1\"></label>",
  "<label class=\"row\"><span>Line Space</span><input type=\"number\" data-setting=\"lineSpacing\" min=\"1.0\" max=\"2.2\" step=\"0.05\"></label>",
  "<label class=\"row\"><span>Padding Left</span><input type=\"number\" data-setting=\"padX\" min=\"8\" max=\"48\"></label>",
  "<label class=\"row\"><span>Padding Top</span><input type=\"number\" data-setting=\"padY\" min=\"6\" max=\"40\"></label>",
  "<label class=\"row\"><span>Indent Size</span><input type=\"number\" data-setting=\"tabSpacing\" min=\"2\" max=\"8\"></label>",
  "</section>",
  "<section><h2>Tabs</h2>",
  "<label class=\"row\"><span>Spacing</span><input type=\"number\" data-setting=\"fanSpacing\" min=\"28\" max=\"140\" step=\"1\" aria-describedby=\"fan-spacing-help\"></label>",
  "<small id=\"fan-spacing-help\">Tab separation. May overlap.</small>",
  "<label class=\"row\"><span>Tab Width</span><input type=\"number\" data-setting=\"fanTabWidth\" min=\"24\" max=\"72\" step=\"1\"></label>",
  "<label class=\"row\"><span>Tab Height</span><input type=\"number\" data-setting=\"fanTabLength\" min=\"60\" max=\"240\" step=\"1\"></label>",
  "<label class=\"row\"><span>Label Size</span><input type=\"number\" data-setting=\"fanLabelFontSize\" min=\"7\" max=\"18\" step=\"1\"></label>",
  "<label class=\"row checkbox\"><span>Bold</span><input type=\"checkbox\" data-setting=\"fanLabelBold\"></label>",
  "<label class=\"row checkbox\"><span>Auto-hide</span><input type=\"checkbox\" data-setting=\"fanAutoHide\"></label>",
  "</section>",
  "<section><h2>Cards</h2>",
  "<label class=\"row\"><span>Width</span><input type=\"number\" data-setting=\"width\" min=\"480\" max=\"800\"></label>",
  "<label class=\"row\"><span>Height</span><input type=\"number\" data-setting=\"height\" min=\"360\" max=\"650\"></label>",
  "</section>",
  "<section><h2>Buttons</h2>",
  "<label class=\"row\"><span>Size</span><input type=\"number\" data-setting=\"iconSize\" min=\"10\" max=\"24\"></label>",
  "</section>",
  // About: what this build IS, then who it is built from. One quiet place at the end
  // rather than provenance riding every row of the theme chooser. The version is the
  // PACKAGED revision (see debian/rules), so "which release am I running" is answered
  // by the application rather than by the package manager.
  "<section class=\"last\"><h2>About</h2>",
  "<div class=\"row aboutrow\"><span>Fan Fold</span><span id=\"about-version\" class=\"aboutver\">—</span></div>",
  "<small class=\"credits\">Markdown notes as ordinary .md files in a folder you choose. The folder is the database: any editor can read it.</small>",
  // The author, credited alongside the third parties below.
  "<div class=\"row aboutrow aboutby\"><span>Created by</span><span>preyevates</span></div>",
  "<h3 class=\"credits-h\">Credits</h3>",
  "<small class=\"credits\">Editor: Vditor (MIT, B3log). Colour sets: ColorBrewer 2.0 (Apache 2.0, Cynthia Brewer, Mark Harrower, Penn State); Nord (Sven Greb); Catppuccin; Solarized (Ethan Schoonover); Gruvbox (Pavel Pertsev); Dracula; Monokai (Wimer Hazenberg); One Dark (GitHub Atom); Tokyo Night (folke); Ayu (Ike Ku); Everforest (sainnhe); Rosé Pine; Material (Mattia Astorino); Kanagawa (rebelot); GitHub Primer; Nightfox (EdenEast) — all reproduced from their published palettes.</small>",
  "</section></div>",
  "<footer><button id=\"reset-appearance\">Reset</button><small id=\"appearance-status\" role=\"status\" hidden></small></footer>"
 ].join("");
 panel.innerHTML=html;
 document.body.append(panel);
 document.querySelector("#close-settings").onclick=()=>ap.toggle(false);
 document.querySelector("#reset-appearance").onclick=ap.reset;
 // Notes-folder wiring: icon, current path, and the native chooser. The path refreshes
 // each time the panel opens (ap.toggle re-reads it) so a change made from the welcome
 // panel shows here without a restart.
 const icon=document.querySelector("#settings-appicon");
 fan.call("appIcon").then(u=>{if(u)icon.src=u;else icon.remove();});
 // About → version. A failed call leaves the em-dash rather than inventing a number.
 fan.call("appVersion").then(v=>{const el=document.querySelector("#about-version");if(el&&v)el.textContent=v;});
 ap.refreshLibraryPath=()=>fan.call("libraryPath").then(p=>{
  const el=document.querySelector("#library-path");
  if(el)el.textContent=p||"none chosen";
 });
 ap.refreshLibraryPath();
 document.querySelector("#library-change").onclick=()=>fan.call("chooseFolder");
 panel.querySelectorAll("[data-setting]").forEach(el=>el.addEventListener("input",()=>{if(!el.checkValidity())return;ap.change(el.dataset.setting,ap.readControl(el));}));
 // The palette chooser is deliberately ABSENT here: it lives in the note's colour
 // panel (Main.qml themeList), beside the swatches it repopulates.
 // ONE global font control, and it offers what this machine actually has.
 //
 // The families come from the native side (QFontDatabase, i.e. the platform's own
 // fontconfig answer) — nothing is downloaded, copied, installed or cached here, and no
 // directory is scanned by hand or hard-coded per distribution. The retired
 // sans/serif/mono enum is still read for a configuration written before this control
 // existed, in memory only: nothing is rewritten at startup, so an untouched
 // appearance.json keeps exactly the look it had.
 ap.families=await fan.call("fontFamilies")||[];
 ap.systemFamily=await fan.call("fontResolve","");
 const fontCombo=ap.makeCombo("font-select","Font",async family=>{
  await ap.change("fontFamilyName",family);
 });
 panel.querySelector("#font-slot").append(fontCombo.root);
 fontCombo.button.setAttribute("aria-describedby","font-help");
 fontCombo.set(ap.families.map(family=>({value:family,label:family})),"");
 ap.noteFont=ap.makeNoteFontPanel();
 const r=await fan.call("loadAppearance");ap.value=r.settings;ap.apply();
 // A load WARNING is a real failure to report (unsafe path, unreadable file): it shows.
 // The ordinary "loaded fine" case says nothing at all.
 ap.status(r.warning,!!r.warning);
};
