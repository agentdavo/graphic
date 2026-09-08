/* Local-only application. No fetch, network service, or execution of capture text. */
(() => {
  'use strict';
  const C=InspectorCore,$=id=>document.getElementById(id);
  let A=null,B=null,current=null,selectedEvent=null,slot=null,rawA=null,rawB=null,imageA=null,imageB=null;
  let history=[],schemas={},version=0,zoom=1,panX=0,panY=0,pinned=false,drag=null,lastDiff=null;
  const cache=new WeakMap();
  const message=(text,error=false)=>{$('status').textContent=text;$('status').classList.toggle('error',error);};
  const attempt=fn=>async(...args)=>{try{await fn(...args);}catch(e){message(e.message,true);}};
  const label=text=>C.fields(text).label||text;
  function pathSafe(path){if(typeof path!=='string'||path.includes('\\')||path.startsWith('/')||path.split('/').some(p=>p==='..'||p==='.'||!p)||path.includes(':'))throw Error('Unsafe capture path');return path;}
  function checkCapture(c){if(!c||!Number.isInteger(c.frame)||c.frame<0||!Array.isArray(c.checkpoints)||!c.checkpoints.length||typeof c.sha256!=='string')throw Error('Invalid capture.json');for(const p of c.checkpoints)if(!Array.isArray(p)||p.length!==2||(p[0]!==null&&(!Number.isInteger(p[0])||p[0]<1))||typeof p[1]!=='string')throw Error('Invalid checkpoint selection');}
  async function fromFolder(files){
    const map=new Map();for(const f of files){const path=f.webkitRelativePath.split('/').slice(1).join('/');map.set(pathSafe(path),f);}
    async function text(path,optional=false){const f=map.get(pathSafe(path));if(!f){if(optional)return null;throw Error('Missing '+path);}return f.text();}
    const meta=JSON.parse(await text('capture.json'));checkCapture(meta);
    const points=[];for(const [event,name] of meta.checkpoints){const dir=event===null?'complete':`event_${String(event).padStart(6,'0')}`;let images=[],error=null;
      try{images=JSON.parse(await text(dir+'/images.json'));if(!Array.isArray(images))throw Error('Invalid image list');}catch(e){error=e.message;}
      const metricsText=await text(dir+'/metrics.json',true);let metrics=null;try{metrics=metricsText?JSON.parse(metricsText):null;}catch{metrics={error:'Malformed metrics.json'};}
      const bufferText=await text(dir+'/buffers.json',true);
      points.push({event,label:name,dir,images,error,buffers:bufferText?JSON.parse(bufferText):[],resources:await text(dir+'/resources.txt',true),metrics});
    }
    const schemaText=await text('push-schema.json',true);
    return {meta,points,events:C.trace(await text('events.tsv')),schemas:schemaText?JSON.parse(schemaText):{},name:files[0].webkitRelativePath.split('/')[0],
      async raw(point,image){const path=pathSafe(point.dir+'/'+pathSafe(image.file)),file=map.get(path);if(!file)throw Error('Missing raw attachment: '+path);return new Uint8Array(await file.arrayBuffer());}};
  }
  function fromBundle(data){checkCapture(data.meta);return {...data,async raw(point,image){if(image.error)throw Error(image.error);const encoded=data.assets[image.data];if(typeof encoded!=='string')throw Error('Missing embedded raw attachment');return Uint8Array.from(atob(encoded),c=>c.charCodeAt(0));}};}
  async function bytes(capture,point,image){if(image.error)throw Error(image.error);let map=cache.get(capture);if(!map){map=new Map();cache.set(capture,map);}const key=point.dir+'/'+image.file;if(!map.has(key))map.set(key,capture.raw(point,image));const b=await map.get(key);C.validate(image,b);return b;}
  function option(value,text){const o=document.createElement('option');o.value=value;o.textContent=text;return o;}
  async function refreshCapture(){
    $('numeric').checked=!!A.meta.comparison?.numeric;$('absolute').value=A.meta.comparison?.absolute??0;$('relative').value=A.meta.comparison?.relative??0;
    version++;schemas=A.schemas||{};current=null;selectedEvent=null;slot=null;lastDiff=null;
    $('captureName').textContent=`${A.name} · frame ${A.meta.frame} · ${A.meta.path||'path unknown'}`;
    $('checkpoint').replaceChildren(...A.points.map((p,i)=>option(i,`${p.event??'End'} · ${label(p.label)}`)));
    history=C.resourceHistory(A.events);$('historyResource').replaceChildren(...[...new Set(history.map(r=>r.kind+':'+r.id))].map(k=>option(k,k)));showHistory();
    buildTree();await choosePoint(A.points.length-1);
  }
  function buildTree(){
    const tree=$('tree'),query=$('search').value.toLowerCase();tree.replaceChildren();
    const frames=new Map();let pass=null,frame=null;
    for(const event of A.events){
      if(!frames.has(event.frame)){const group=document.createElement('details');group.open=event.frame===A.meta.frame;const title=document.createElement('summary');title.textContent='Frame '+event.frame;group.append(title);frames.set(event.frame,group);tree.append(group);}
      const group=frames.get(event.frame);if(frame!==event.frame){pass=null;frame=event.frame;}
      if(event.op==='pass_begin'){pass=document.createElement('details');pass.open=true;const title=document.createElement('summary');title.textContent=C.fields(event.detail).label||'Pass at event '+event.event;pass.append(title);group.append(pass);}
      if(!query||`${event.event} ${event.op} ${event.detail}`.toLowerCase().includes(query)){
        const button=document.createElement('button');button.className='event';button.dataset.event=event.event;
        button.classList.toggle('active',selectedEvent?.event===event.event);button.classList.toggle('captured',A.points.some(p=>p.event===event.event));
        const number=document.createElement('span');number.className='num';number.textContent=event.event;
        button.append(number,document.createTextNode(event.op));button.title=event.detail;
        button.onclick=attempt(()=>chooseEvent(event));(pass||group).append(button);
      }
      if(event.op==='pass_end')pass=null;
    }
    $('eventCount').textContent=A.events.length+' events';
  }
  function details(event){
    selectedEvent=event;document.querySelectorAll('.event').forEach(e=>e.classList.toggle('active',Number(e.dataset.event)===event?.event));
    $('eventTitle').textContent=event?`#${event.event} · ${event.op}`:'Complete frame';
    $('eventDetails').textContent=event?`Frame ${event.frame}\n`+Object.entries(C.fields(event.detail)).filter(([key])=>key!=='push_hex').map(([key,value])=>`${key}: ${value}`).join('\n'):'All recorded operations in this frame.';
    const f=event?C.fields(event.detail):{},schema=schemas[f.label]||schemas[f.pipeline];
    showReferences(event);
    if(!f.push_hex){$('push').textContent='No push bytes recorded for this event.';return;}
    let result='A · '+f.push_hex.match(/.{1,8}/g).join(' ')+'\n';
    if(schema){try{result+=C.typedPush(f.push_hex,schema).map(x=>`${x.name} (${x.type} @ ${x.offset}): ${x.values.join(', ')}`).join('\n');}catch(e){result+='Schema error: '+e.message;}}
    else result+='No declared layout. Load a push schema to decode field values.';
    const other=B?.events.find(x=>x.event===event.event&&x.frame===event.frame),bf=other?C.fields(other.detail):{};
    if(bf.push_hex){result+='\n\nB · '+bf.push_hex.match(/.{1,8}/g).join(' ');const bs=(B.schemas||{})[bf.label]||schema;if(bs){try{result+='\n'+C.typedPush(bf.push_hex,bs).map(x=>`${x.name}: ${x.values.join(', ')}`).join('\n');}catch(e){result+='\nSchema error: '+e.message;}}}
    $('push').textContent=result+'\n\nGPU addresses are process-specific. Candidate targets are linked below when their bytes were captured.';
  }
  function clearImage(reason){rawA=rawB=imageA=imageB=null;$('image').hidden=true;$('pixelMarker').hidden=true;$('empty').hidden=false;$('empty').replaceChildren(document.createTextNode(reason));$('imageInfo').textContent='No attachment';$('pixelReadout').textContent='No raw data selected.';}
  async function chooseEvent(event){
    details(event);const i=A.points.findIndex(p=>p.event===event.event);
    if(i>=0){await choosePoint(i,false);return;}
    if(event.op==='frame_end'&&event.frame===A.meta.frame&&A.points.some(p=>p.event===null)){await choosePoint(A.points.findIndex(p=>p.event===null),false);return;}
    version++;current=null;$('checkpoint').selectedIndex=-1;$('attachment').replaceChildren();clearImage('No snapshot was captured after this event.');
    $('uncaptured').hidden=false;$('resources').textContent='No resource snapshot at this event.';$('metrics').textContent='No measurement snapshot at this event.';$('buffer').replaceChildren();$('bufferReadout').textContent='No buffer snapshot at this event.';
    $('captureCommand').textContent=`inspect_frame.py JOURNAL --replay REPLAY_EXE --frame ${event.frame} --event ${event.event} --out NEW_DIRECTORY --workbench`;
  }
  async function choosePoint(index,updateDetails=true){
    if(!A)return;const p=A.points[index];if(!p)return;version++;clearImage('Loading checkpoint…');current=p;$('checkpoint').value=index;$('uncaptured').hidden=true;
    if(updateDetails)details(A.events.find(e=>e.event===p.event)||null);
    $('resources').textContent=`Journal: ${A.meta.journal}\nSHA-256: ${A.meta.sha256}\nPath: ${A.meta.path||'unknown'}\nReplay environment: ${JSON.stringify(A.meta.metadata||'not recorded',null,2)}\n\n`+(p.resources||'Resource snapshot unavailable.');
    $('metrics').textContent=p.metrics?'Inspection run — not a normal-frame benchmark\nCPU and GPU elapsed times overlap.\n'+JSON.stringify(p.metrics,null,2):'Metrics unavailable.';
    $('buffer').replaceChildren(...(p.buffers||[]).map((b,i)=>option(i,`${b.kind} ${b.id} · ${b.label} · ${b.size} bytes`)));$('buffer').selectedIndex=0;$('buffer').value='0';$('bufferReadout').textContent='Select a type and decode captured bytes.';showReferences(selectedEvent);
    $('attachment').replaceChildren(...p.images.map(x=>option(x.slot,`${x.label} · ${x.width}×${x.height}`)));
    if(!p.images.some(x=>String(x.slot)===String(slot)))slot=p.images[0]?.slot;
    $('attachment').value=slot??'';await loadImage();
  }
  async function loadImage(){
    const token=++version;clearImage('Loading raw attachment…');pinned=false;lastDiff=null;
    if(!current||current.error||!current.images.length){clearImage(current?.error||'No attachments captured.');return;}
    try{
      const a=current.images.find(x=>String(x.slot)===String(slot));if(!a)throw Error('Attachment unavailable');
      const ar=await bytes(A,current,a);if(token!==version)return;imageA=a;rawA=ar;
      if(B){const p=B.points.find(x=>x.event===current.event);const b=p?.images.find(x=>String(x.slot)===String(slot));if(b){try{const br=await bytes(B,p,b);if(token!==version)return;imageB=b;rawB=br;}catch(e){message('Capture B: '+e.message,true);}}}
      if(token!==version)return;$('empty').hidden=true;$('image').hidden=false;
      $('imageInfo').textContent=`${C.formats[Number(a.format)]} · ${a.width} × ${a.height} · slot ${a.slot}`;
      draw();fit();readPixel(0,0);
    }catch(e){if(token===version){clearImage(e.message);message(e.message,true);}}
  }
  function comparisonAllowed(){if(!A||!B)throw Error('Open capture B to compare');C.compatible(A.meta,B.meta,$('allowDifferent').checked);}
  function draw(){
    if(!rawA)return;const mode=$('mode').value,channel=$('channel').value,ev=Number($('exposure').value),canvas=$('image');$('ev').textContent=ev;
    canvas.width=Number(imageA.width);canvas.height=Number(imageA.height);const ctx=canvas.getContext('2d');
    try{
      let pixels=C.render(imageA,rawA,channel,ev);
      if(mode!=='a'){
        comparisonAllowed();if(!rawB)throw Error('Capture B has no matching raw attachment');
        const al=C.layout(imageA),bl=C.layout(imageB);if((imageA.id&&imageB.id&&imageA.id!==imageB.id)||al.w!==bl.w||al.h!==bl.h||al.f!==bl.f)throw Error('Attachment layouts differ');
        if(mode==='diff'){lastDiff=C.difference(imageA,rawA,imageB,rawB,tolerance());pixels=lastDiff.mask;$('differenceResult').textContent=lastDiff.count+' differing texels';}
        else {const b=C.render(imageB,rawB,channel,ev);if(mode==='b')pixels=b;else{const cut=Math.floor(canvas.width*Number($('wipe').value)/100);for(let y=0;y<canvas.height;y++)pixels.set(b.subarray((y*canvas.width+cut)*4,(y+1)*canvas.width*4),(y*canvas.width+cut)*4);}}
      }
      ctx.putImageData(new ImageData(pixels,canvas.width,canvas.height),0,0);canvas.hidden=false;$('empty').hidden=true;transform();
    }catch(e){canvas.hidden=true;$('pixelMarker').hidden=true;$('empty').hidden=false;$('empty').textContent=e.message;message(e.message,true);}
  }
  function transform(){$('image').style.transform=`translate(${panX}px,${panY}px) scale(${zoom})`;const m=$('pixelMarker');m.hidden=!rawA||$('image').hidden;m.style.left=(panX+Number($('pixelX').value)*zoom)+'px';m.style.top=(panY+Number($('pixelY').value)*zoom)+'px';m.style.width=m.style.height=Math.max(3,zoom)+'px';}
  function fit(){if(!imageA)return;const box=$('viewport');zoom=Math.min((box.clientWidth-36)/Number(imageA.width),(box.clientHeight-36)/Number(imageA.height));panX=(box.clientWidth-Number(imageA.width)*zoom)/2;panY=(box.clientHeight-Number(imageA.height)*zoom)/2;transform();}
  function readPixel(x,y){
    if(!rawA)return;const a=C.pixel(imageA,rawA,x,y);$('pixelX').value=x;$('pixelY').value=y;
    let text=`(${x}, ${y})\nA: ${a.values.map(String).join(', ')}\nBytes: ${a.hex}`;
    if(rawB){try{comparisonAllowed();const al=C.layout(imageA),bl=C.layout(imageB);if((imageA.id&&imageB.id&&imageA.id!==imageB.id)||al.w!==bl.w||al.h!==bl.h||al.f!==bl.f)throw Error('Attachment layouts differ');const b=C.pixel(imageB,rawB,x,y);text+=`\n\nB: ${b.values.map(String).join(', ')}\nBytes: ${b.hex}`;}catch(e){text+='\n\nB unavailable: '+e.message;}}
    $('pixelReadout').textContent=text;transform();
  }
  async function firstDifference(){
    comparisonAllowed();$('narrowCommand').textContent=`python tools/narrow_capture.py ${shellQuote(A.meta.capture_directory||'CAPTURE_A')} ${shellQuote(B.meta.capture_directory||'CAPTURE_B')} --out NEW_DIRECTORY`+($('numeric').checked?` --numeric --absolute ${Number($('absolute').value)} --relative ${Number($('relative').value)}`:'');$('differenceResult').textContent='Comparing raw checkpoints…';
    const sourceA=A,sourceB=B;for(let i=0;i<A.points.length;i++){
      const a=A.points[i],b=B.points[i],key=x=>[String(x.slot),x.id??null,Number(x.samples||1),Number(x.width),Number(x.height),Number(x.format)];
      if(a.error||b.error)throw Error(a.error||b.error);
      if(JSON.stringify(a.images.map(key))!==JSON.stringify(b.images.map(key)))throw Error(`Resource layouts differ at ${a.label}`);
      for(let j=0;j<a.images.length;j++){
        const x=a.images[j],y=b.images[j];if(Number(x.samples||1)>1)continue;const ab=await bytes(sourceA,a,x),bb=await bytes(sourceB,b,y);
        if(A!==sourceA||B!==sourceB)return;const d=C.difference(x,ab,y,bb,tolerance());
        if(d.count){slot=x.slot;$('mode').value='diff';await choosePoint(i);readPixel(...d.first);pinned=true;panX=$('viewport').clientWidth/2-(d.first[0]+.5)*zoom;panY=$('viewport').clientHeight/2-(d.first[1]+.5)*zoom;transform();
          $('differenceResult').textContent=`First: event ${a.event??'End'} · ${x.label} · (${d.first.join(', ')}) · ${d.count} texels`;
          message('First differing captured checkpoint located. Capture individual events within this pass to narrow it further.');return;}
        await new Promise(resolve=>setTimeout(resolve,0));
      }
    }
    $('differenceResult').textContent=($('numeric').checked?'Match within numeric tolerance across captured single-sample attachments':'Exact raw match across captured single-sample attachments');message($('numeric').checked?'Captured single-sample attachments match within numeric tolerance; unresolved MSAA samples are excluded.':'All captured single-sample attachments match; unresolved MSAA samples are excluded.');
  }
  function shellQuote(value){return "'"+String(value).replaceAll("'","''")+"'";} // PowerShell literal, never executed by the viewer.
  function tolerance(){return {numeric:$('numeric').checked,absolute:Number($('absolute').value),relative:Number($('relative').value)};}
  function showHistory(){
    const key=$('historyResource').value;$('history').replaceChildren();
    for(const r of history.filter(x=>x.kind+':'+x.id===key)){
      const b=document.createElement('button');b.textContent=`#${r.event} ${r.op}: ${r.access} (${r.evidence})`;
      b.onclick=attempt(()=>chooseEvent(A.events.find(e=>e.event===r.event)));$('history').append(b);
    }
  }
  function showReferences(event){
    $('pushRefs').replaceChildren();if(!event)return;
    const refs=C.references(event.detail),f=C.fields(event.detail);
    if(event.op==='draw_indirect')for(const [key,offset,type] of [['commands','command_offset','indirect'],['counts','count_offset','u32'],['indices',null,'u32']])if(f[key])refs.push({kind:'buffer',id:f[key],offset:Number(f[offset]||0),type,label:key});
    for(const ref of refs){const b=document.createElement('button');b.textContent=`${ref.label||'Address candidate @'+ref.pushOffset}: ${ref.kind} ${ref.id} +${ref.offset}`;
      b.onclick=attempt(async()=>{if(!current)throw Error('Capture this event before following its memory references');const i=(current.buffers||[]).findIndex(x=>x.kind===ref.kind&&String(x.id)===ref.id);if(i<0)throw Error('Referenced resource was not captured');const buffer=current.buffers[i];$('buffer').value=i;$('bufferOffset').value=ref.offset-Number(buffer.offset||0);$('bufferType').value=ref.type||(schemas.$buffers?.[buffer.label]?'schema':'f32');$('bufferCount').value=1;$('bufferStride').value=0;await readBuffer();});$('pushRefs').append(b);
    }
  }
  async function readBuffer(){
    $('bufferReadout').textContent='Loading captured bytes…';
    const point=current,source=A,b=point?.buffers?.[Number($('buffer').value)];if(!b)throw Error('No buffer snapshot selected');
    if(b.error)throw Error(b.error);const size=Number(b.size);if(!Number.isSafeInteger(size)||size<1||size>64*1024*1024)throw Error('Invalid buffer size');
    const raw=await source.raw(point,b);if(source!==A||point!==current)return;if(raw.length!==size)throw Error('Truncated or oversized raw buffer');
    const offset=Number($('bufferOffset').value),schema=schemas.$buffers?.[b.label]||schemas.$buffers?.[b.id];
    const rows=C.bufferRows(raw,offset,$('bufferType').value||'u32',Number($('bufferCount').value||'16'),Number($('bufferStride').value),schema);
    $('historyResource').value=b.kind+':'+b.id;showHistory();
    const format=(items,data)=>items.map(r=>`@${r.offset}: `+r.fields.map(f=>f.name+'='+f.values.join(', ')).join(' | ')).join('\n')+'\nBytes: '+Array.from(data.subarray(offset,offset+64),v=>v.toString(16).padStart(2,'0')).join(' ');
    let result='A\n'+format(rows,raw);
    if(B){try{comparisonAllowed();const other=B,p=other.points.find(x=>x.event===point.event),target=p?.buffers?.find(x=>x.kind===b.kind&&String(x.id)===String(b.id));if(!target||target.error||Number(target.size)!==size)throw Error('No compatible B buffer snapshot');const data=await other.raw(p,target);if(source!==A||point!==current||other!==B)return;if(data.length!==size)throw Error('Truncated B buffer');result+='\n\nB\n'+format(C.bufferRows(data,offset,$('bufferType').value||'u32',Number($('bufferCount').value||'16'),Number($('bufferStride').value),schema),data);}catch(e){result+='\n\nB unavailable: '+e.message;}}
    $('bufferReadout').textContent=result;
  }
  $('readBuffer').onclick=attempt(readBuffer);$('buffer').onchange=()=>{$('bufferReadout').textContent='Selection changed. Decode to read this buffer.';};$('historyResource').onchange=showHistory;
  $('openA').onchange=attempt(async e=>{if(!e.target.files.length)return;const c=await fromFolder(e.target.files);A=c;B=null;$('mode').value='a';$('allowDifferent').checked=false;await refreshCapture();if(rawA)message('Capture A loaded. Select an event or attachment.');});
  $('openB').onchange=attempt(async e=>{if(!e.target.files.length)return;B=await fromFolder(e.target.files);if(!A){A=B;B=null;await refreshCapture();}else{details(selectedEvent);await loadImage();}message('Capture B loaded. Different recordings require explicit comparison consent.');});
  $('schema').onchange=attempt(async e=>{if(!e.target.files.length)return;schemas=JSON.parse(await e.target.files[0].text());if(!schemas||typeof schemas!=='object'||Array.isArray(schemas))throw Error('Schema must map pipeline labels to field arrays');details(selectedEvent);message('Push layout loaded. Values are decoded only for matching pipelines.');});
  $('search').oninput=()=>{if(A)buildTree();};$('checkpoint').onchange=attempt(e=>choosePoint(Number(e.target.value)));
  $('attachment').onchange=attempt(e=>{slot=e.target.value;const image=current?.images.find(x=>String(x.slot)===String(slot));if(image?.id){$('historyResource').value='image:'+image.id;showHistory();}return loadImage();});
  const step=attempt(async d=>{if(A)await choosePoint(Math.max(0,Math.min(A.points.length-1,A.points.indexOf(current)+d)));});
  $('previous').onclick=()=>step(-1);$('next').onclick=()=>step(1);
  for(const id of ['mode','channel','exposure','wipe','allowDifferent','numeric','absolute','relative'])$(id).oninput=attempt(()=>{draw();if(rawA)readPixel(Number($('pixelX').value),Number($('pixelY').value));});
  $('fit').onclick=fit;$('actual').onclick=()=>{zoom=1;panX=panY=16;transform();};$('inspectPixel').onclick=attempt(()=>{readPixel(Number($('pixelX').value),Number($('pixelY').value));pinned=true;});
  $('firstDifference').onclick=attempt(firstDifference);
  $('viewport').onwheel=e=>{if(!rawA)return;e.preventDefault();const r=$('viewport').getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top,next=Math.max(.03,Math.min(64,zoom*Math.exp(-e.deltaY*.001)));panX=x-(x-panX)*next/zoom;panY=y-(y-panY)*next/zoom;zoom=next;transform();};
  $('viewport').onpointerdown=e=>{if(!rawA)return;drag={x:e.clientX,y:e.clientY,px:panX,py:panY,moved:false};$('viewport').setPointerCapture(e.pointerId);};
  $('viewport').onpointermove=e=>{if(drag){if(Math.hypot(e.clientX-drag.x,e.clientY-drag.y)>3)drag.moved=true;if(drag.moved){panX=drag.px+e.clientX-drag.x;panY=drag.py+e.clientY-drag.y;transform();}}if(rawA&&!pinned&&!drag){const r=$('viewport').getBoundingClientRect(),x=Math.floor((e.clientX-r.left-panX)/zoom),y=Math.floor((e.clientY-r.top-panY)/zoom);if(x>=0&&y>=0&&x<Number(imageA.width)&&y<Number(imageA.height))readPixel(x,y);}};
  $('viewport').onpointerup=e=>{if(drag&&!drag.moved){const r=$('viewport').getBoundingClientRect(),x=Math.floor((e.clientX-r.left-panX)/zoom),y=Math.floor((e.clientY-r.top-panY)/zoom);if(x>=0&&y>=0&&x<Number(imageA.width)&&y<Number(imageA.height)){readPixel(x,y);pinned=true;}}drag=null;};
  $('viewport').onpointercancel=()=>{drag=null;};
  document.addEventListener('keydown',e=>{if(['INPUT','SELECT','TEXTAREA'].includes(e.target.tagName))return;if(e.key==='ArrowLeft'||e.key==='ArrowRight'){e.preventDefault();step(e.key==='ArrowLeft'?-1:1);}});
  window.addEventListener('resize',fit);
  if(window.INSPECTOR_BOOT)attempt(async()=>{A=fromBundle(INSPECTOR_BOOT.a);B=INSPECTOR_BOOT.b?fromBundle(INSPECTOR_BOOT.b):null;await refreshCapture();if(rawA)message('Embedded capture loaded. Raw pixels and events are ready to inspect.');})();
})();
