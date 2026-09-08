/* Pure capture decoding shared by the offline viewer and Node regressions. */
(function (root) {
  'use strict';
  const formats = {0:'RGBA8 UNORM',1:'RGBA8 sRGB',2:'BGRA8 UNORM',8:'R11G11B10 float',9:'RGBA16 float',10:'D32 float',11:'R32 uint',12:'RG16 UNORM'};
  function layout(image) {
    const w=Number(image.width),h=Number(image.height),f=Number(image.format);
    if(!Number.isInteger(w)||!Number.isInteger(h)||w<1||h<1||w*h>16777216) throw Error('Invalid or oversized image (limit: 16 million pixels)');
    if(!(f in formats)) throw Error('Unsupported raw format '+f);
    return {w,h,f,stride:f===9?8:4};
  }
  function validate(image,bytes) { const l=layout(image); if(bytes.byteLength!==l.w*l.h*l.stride) throw Error(`Truncated or oversized raw image: expected ${l.w*l.h*l.stride} bytes, received ${bytes.byteLength}`); return l; }
  function ufloat(v,m) { const e=v>>>m,f=v&((1<<m)-1);return e===31?(f?NaN:Infinity):e===0?f/2**m*2**-14:(1+f/2**m)*2**(e-15); }
  function half(v) { return (v&32768?-1:1)*ufloat(v&32767,10); }
  function values(view,offset,f) {
    if(f<=2) {const a=Array.from(new Uint8Array(view.buffer,view.byteOffset+offset,4));if(f===2)[a[0],a[2]]=[a[2],a[0]];return a;}
    if(f===9)return [0,2,4,6].map(i=>half(view.getUint16(offset+i,true)));
    if(f===10)return [view.getFloat32(offset,true)];
    if(f===11)return [view.getUint32(offset,true)];
    if(f===12)return [view.getUint16(offset,true),view.getUint16(offset+2,true)];
    const v=view.getUint32(offset,true);return [ufloat(v&2047,6),ufloat((v>>>11)&2047,6),ufloat(v>>>22,5)];
  }
  function pixel(image,bytes,x,y) {
    const l=validate(image,bytes);if(!Number.isInteger(x)||!Number.isInteger(y)||x<0||y<0||x>=l.w||y>=l.h)throw Error('Pixel outside image');
    const offset=(y*l.w+x)*l.stride;
    return {values:values(new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),offset,l.f),hex:Array.from(bytes.subarray(offset,offset+l.stride),v=>v.toString(16).padStart(2,'0')).join(' ')};
  }
  function color(v,f,channel,exposure) {
    let c;
    if(f<=2)c=v.map(x=>x/255);
    else if(f===11)c=v[0]===0?[0,0,0,1]:[97,57,23].map(p=>((v[0]*p)&255)/255).concat(1);
    else if(f===10)c=[v[0],v[0],v[0],1];
    else if(f===12) {let x=v[0]/65535*2-1,y=v[1]/65535*2-1,z=1-Math.abs(x)-Math.abs(y);if(z<0)[x,y]=[(1-Math.abs(y))*(x>=0?1:-1),(1-Math.abs(x))*(y>=0?1:-1)];const n=Math.hypot(x,y,z);c=[x,y,z].map(t=>t/n*.5+.5).concat(1);}
    else c=v.slice(0,3).map(t=>{t=Math.max(0,t)*2**exposure;return (t/(1+t))**(1/2.2);}).concat(v[3]??1);
    if(channel!=='rgb') {const index={r:0,g:1,b:2,a:3}[channel];c=[c[index],c[index],c[index],1];}
    return c.slice(0,3).map(t=>Math.round(Math.max(0,Math.min(1,Number.isFinite(t)?t:0))*255)).concat(255);
  }
  function render(image,bytes,channel='rgb',exposure=0) {
    const l=validate(image,bytes),view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),out=new Uint8ClampedArray(l.w*l.h*4);
    for(let i=0;i<l.w*l.h;i++)out.set(color(values(view,i*l.stride,l.f),l.f,channel,exposure),i*4);
    return out;
  }
  function difference(a,ab,b,bb,options={}) {
    const abs=Number(options.absolute||0),rel=Number(options.relative||0),numeric=!!options.numeric;
    if(!Number.isFinite(abs)||!Number.isFinite(rel)||abs<0||rel<0)throw Error('Tolerances must be finite and nonnegative');
    const l=validate(a,ab),r=validate(b,bb);if(l.w!==r.w||l.h!==r.h||l.f!==r.f)throw Error('Attachment layouts differ');
    let count=0,first=null;const mask=new Uint8ClampedArray(l.w*l.h*4);
    for(let i=0;i<l.w*l.h;i++){let changed=false;for(let j=0;j<l.stride;j++)if(ab[i*l.stride+j]!==bb[i*l.stride+j])changed=true;
      if(changed&&numeric){
        const av=values(new DataView(ab.buffer,ab.byteOffset,ab.byteLength),i*l.stride,l.f),bv=values(new DataView(bb.buffer,bb.byteOffset,bb.byteLength),i*l.stride,l.f);
        changed=av.some((v,k)=>v!==bv[k]&&(!Number.isFinite(v)||!Number.isFinite(bv[k])||Math.abs(v-bv[k])>abs+rel*Math.max(Math.abs(v),Math.abs(bv[k]))));
      }
      if(changed){count++;first??=[i%l.w,Math.floor(i/l.w)];}mask.set(changed?[255,103,80,255]:[12,17,23,255],i*4);}
    return {count,first,mask};
  }
  function compatible(a,b,allow=false) {
    if(a.frame!==b.frame||JSON.stringify(a.checkpoints)!==JSON.stringify(b.checkpoints))throw Error('Comparison requires identical frame and checkpoint selections');
    if(a.sha256!==b.sha256&&!allow)throw Error('Different journals: explicitly enable comparison of different recordings');
  }
  function fields(detail) {const out={};for(const m of detail.matchAll(/(?:^|\s)(\w+)=(.*?)(?=\s\w+=|$)/g))out[m[1]]=m[2];return out;}
  function trace(tsv) {
    const lines=tsv.trim().split(/\r?\n/);if(lines[0]!=='event\tframe\top\tdetail')throw Error('Invalid event trace header');
    return lines.slice(1).filter(Boolean).map(line=>{const [event,frame,op,...detail]=line.split('\t');const e=Number(event),f=Number(frame);if(!Number.isInteger(e)||e<1||!Number.isInteger(f)||f<0||!op)throw Error('Invalid event trace record');return {event:e,frame:f,op,detail:detail.join('\t')};});
  }
  function typedPush(hex,schema) {
    if(!/^(?:[0-9a-f]{2})*$/i.test(hex))throw Error('Invalid push bytes');
    const bytes=Uint8Array.from(hex.match(/../g)||[],x=>parseInt(x,16)),v=new DataView(bytes.buffer),types={u32:[4,'getUint32'],i32:[4,'getInt32'],f32:[4,'getFloat32'],u64:[8,'getBigUint64'],f64:[8,'getFloat64']};
    if(!Array.isArray(schema))throw Error('Schema fields must be an array');
    return schema.map(f=>{const type=types[f.type],count=f.count??1;if(!type||!Number.isInteger(f.offset)||f.offset<0||!Number.isInteger(count)||count<1||count>256||f.offset+type[0]*count>bytes.length)throw Error('Schema field outside push block or unsupported type: '+f.name);return {name:String(f.name),type:f.type,offset:f.offset,values:Array.from({length:count},(_,i)=>String(v[type[1]](f.offset+i*type[0],true)))};});
  }
  function bufferRows(bytes,offset,type,count=16,stride=0,fields=null) {
    const types={u32:[4,'getUint32'],i32:[4,'getInt32'],f32:[4,'getFloat32'],u64:[8,'getBigUint64'],f64:[8,'getFloat64']};
    const indirect=[{name:'indexCount',type:'u32',offset:0},{name:'instanceCount',type:'u32',offset:4},{name:'firstIndex',type:'u32',offset:8},{name:'vertexOffset',type:'i32',offset:12},{name:'firstInstance',type:'u32',offset:16}];
    const schema=type==='indirect'?indirect:type==='schema'?fields:[{name:type,type,offset:0}];
    if(!Array.isArray(schema)||!schema.length)throw Error('Declare buffer fields in schema.$buffers[label]');
    let width=0;for(const f of schema){if(!types[f.type]||!Number.isInteger(f.offset)||f.offset<0)throw Error('Invalid buffer field');const n=f.count??1;if(!Number.isInteger(n)||n<1||n>256)throw Error('Invalid field count');width=Math.max(width,f.offset+types[f.type][0]*n);}
    stride=stride||width;
    if(!Number.isSafeInteger(offset)||offset<0||!Number.isInteger(count)||count<1||count>256||!Number.isSafeInteger(stride)||stride<width||offset+(count-1)*stride+width>bytes.length)throw Error('Buffer range outside captured bytes (maximum 256 rows)');
    const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
    return Array.from({length:count},(_,i)=>({offset:offset+i*stride,fields:schema.map(f=>({name:f.name,values:Array.from({length:f.count??1},(_,j)=>String(view[types[f.type][1]](offset+i*stride+f.offset+j*types[f.type][0],true)))}))}));
  }
  function references(detail){return (fields(detail).refs||'').split(',').filter(Boolean).map(s=>{const [pushOffset,kind,id,offset]=s.split(':');return {pushOffset:Number(pushOffset),kind,id,offset:Number(offset)};});}
  function resourceHistory(events){
    const rows=[];let targets=[],resolves=[];
    const add=(e,kind,id,access,evidence)=>{if(id!==undefined&&id!==''&&!(kind==='image'&&id==='0'))rows.push({event:e.event,frame:e.frame,op:e.op,kind,id:String(id),access,evidence});};
    for(const e of events){const f=fields(e.detail);
      if(e.op==='frame_begin')targets=[];
      if(e.op==='pass_begin'){resolves=(f.resolves||'').split(',').filter(x=>x&&x!=='0');targets=[f.color,f.depth,...(f.extra||'').split(',')].filter(x=>x&&x!=='0');for(const id of targets)add(e,'image',id,((id===f.depth?f.clear_depth:f.clear_color)==='1'?'clear write':'attachment'), 'recorded attachment; clear flag '+(id===f.depth?f.clear_depth:f.clear_color));}
      if(e.op==='pass_end'){for(const id of resolves)add(e,'image',id,'resolve write','fixed-function resolve');targets=[];resolves=[];}
      if(e.op==='draw'||e.op==='draw_indirect')for(const id of targets)add(e,'image',id,'possible write','draw attachment; coverage/depth outcome unknown');
      if(e.op==='draw_indirect')for(const key of ['indices','commands','counts'])add(e,'buffer',f[key],'read',key);
      if(e.op==='buffer_upload'||e.op==='image_upload')add(e,e.op==='buffer_upload'?'buffer':'image',f.resource,'write','recorded upload');
      if(e.op==='fill')add(e,'buffer',f.buffer,'write','recorded fill');
      if(e.op==='copy_to_ring')add(e,'buffer',f.buffer,'read','copy source');
      if(e.op==='make_buffer'||e.op==='make_image')add(e,e.op==='make_buffer'?'buffer':'image',f.buffer||f.image,'create','recorded allocation');
      if(e.op==='free_buffer'||e.op==='free_image')add(e,e.op==='free_buffer'?'buffer':'image',f.value,'free','recorded lifetime end');
      if(e.op==='barrier')for(const m of e.detail.matchAll(/image=(\d+) use=(\d+)/g))add(e,'image',m[1],'transition','use '+m[2]);
      for(const r of references(e.detail))add(e,r.kind,r.id,'address candidate','push @'+r.pushOffset+' byte '+r.offset+'; shader access unknown');
    }return rows;
  }
  const api={bufferRows,references,resourceHistory,formats,layout,validate,ufloat,half,pixel,render,difference,compatible,fields,trace,typedPush};
  if(typeof module!=='undefined')module.exports=api;else root.InspectorCore=api;
})(globalThis);
