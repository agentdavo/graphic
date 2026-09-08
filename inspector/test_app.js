/* Application-state integration in a small DOM adapter, not a browser/layout test. */
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const nodes=[];
class Element{
  constructor(tag='div'){this.tagName=tag.toUpperCase();this.children=[];this.value='';this.checked=false;this.hidden=false;this.dataset={};this.style={};this.clientWidth=900;this.clientHeight=600;this.textContent='';this.classes=new Set();this.classList={toggle:(k,on)=>on?this.classes.add(k):this.classes.delete(k)};nodes.push(this);}
  append(...children){this.children.push(...children);} replaceChildren(...children){this.children=children;}
  getContext(){return {putImageData:data=>{this.drawn=data;}};} getBoundingClientRect(){return {left:0,top:0};} setPointerCapture(){}
}
const html=fs.readFileSync(process.argv[2],'utf8');
const boot=JSON.parse(html.match(/window\.INSPECTOR_BOOT=(.*?);<\/script>/s)[1]);
const ids=new Map([...html.matchAll(/id="([^"]+)"/g)].map(m=>[m[1],new Element()]));
for(const [id,value]of Object.entries({channel:'rgb',exposure:'0',mode:'a',wipe:'50',pixelX:'0',pixelY:'0'}))ids.get(id).value=value;
const document={getElementById:id=>ids.get(id),createElement:tag=>new Element(tag),createTextNode:text=>({textContent:text}),querySelectorAll:()=>nodes.filter(n=>n.className==='event'),addEventListener(){}};
const context={InspectorCore:require('./core.js'),INSPECTOR_BOOT:boot,document,console,setTimeout,Uint8Array,Uint8ClampedArray,DataView,Map,WeakMap,ImageData:class{constructor(data,w,h){this.data=data;this.width=w;this.height=h;}},atob:s=>Buffer.from(s,'base64').toString('binary'),addEventListener(){}};context.window=context;
vm.runInNewContext(fs.readFileSync(__dirname+'/app.js','utf8'),context);
(async()=>{
  await new Promise(setImmediate);
  assert.equal(ids.get('image').hidden,false,'embedded attachment renders');
  await ids.get('firstDifference').onclick();assert.match(ids.get('status').textContent,/Different journals/);
  ids.get('allowDifferent').checked=true;await ids.get('firstDifference').onclick();
  assert.match(ids.get('differenceResult').textContent,/32, 0/);assert.match(ids.get('differenceResult').textContent,/1024 texels/);
  assert.match(ids.get('pixelReadout').textContent,/A: 255, 0, 0, 255/);assert.match(ids.get('pixelReadout').textContent,/B: 128, 0, 0, 255/);
  ids.get('bufferType').value='u32';ids.get('bufferCount').value='1';ids.get('bufferOffset').value='0';ids.get('bufferStride').value='0';
  await ids.get('readBuffer').onclick();assert.match(ids.get('bufferReadout').textContent,/u32=255/);assert.match(ids.get('bufferReadout').textContent,/u32=128/);
  ids.get('numeric').checked=true;ids.get('absolute').value='128';await ids.get('firstDifference').onclick();assert.match(ids.get('differenceResult').textContent,/Match within numeric tolerance/);ids.get('numeric').checked=false;
  const draw=nodes.find(n=>n.dataset.event===15);assert.ok(draw);await draw.onclick();
  assert.equal(ids.get('image').hidden,true,'uncaptured event must clear viewport');
  assert.equal(ids.get('uncaptured').hidden,false);assert.match(ids.get('push').textContent,/gain \(u32 @ 12\): 255/);assert.match(ids.get('push').textContent,/gain: 128/);
  assert.match(ids.get('captureCommand').textContent,/--event 15/);
  await ids.get('next').onclick();await new Promise(setImmediate);assert.equal(ids.get('image').hidden,false);
  ids.get('pixelX').value='99999';await ids.get('inspectPixel').onclick();assert.match(ids.get('status').textContent,/outside image/);
  // Exercise the same File interface used by the directory picker.
  const file=(path,content)=>({webkitRelativePath:'capture/'+path,text:async()=>content,arrayBuffer:async()=>{const b=Buffer.from(content,'base64');return b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength);}});
  const a=boot.a,files=[file('capture.json',JSON.stringify(a.meta)),file('events.tsv','event\tframe\top\tdetail\n'+a.events.map(e=>[e.event,e.frame,e.op,e.detail].join('\t')).join('\n'))];
  for(const p of a.points){files.push(file(p.dir+'/images.json',JSON.stringify(p.images)));files.push(file(p.dir+'/buffers.json',JSON.stringify(p.buffers||[])));for(const image of [...p.images,...(p.buffers||[])])files.push(file(p.dir+'/'+image.file,a.assets[image.data]));}
  await ids.get('openA').onchange({target:{files}});assert.equal(ids.get('image').hidden,false);assert.match(ids.get('captureName').textContent,/capture/);await ids.get('readBuffer').onclick();assert.match(ids.get('bufferReadout').textContent,/u32=255/);
  const raw=files.find(f=>f.webkitRelativePath==='capture/complete/'+a.points.at(-1).images[0].file);raw.arrayBuffer=async()=>new ArrayBuffer(1);
  await ids.get('openA').onchange({target:{files}});assert.equal(ids.get('image').hidden,true);assert.match(ids.get('empty').children[0].textContent,/Truncated/);assert.match(ids.get('status').textContent,/Truncated/);
  console.log('Embedded boot, folder loading, comparison consent, first divergence, raw A/B values, typed push layout, uncaptured events, truncated images and bounds checks passed.');
})().catch(e=>{console.error(e);process.exitCode=1;});
