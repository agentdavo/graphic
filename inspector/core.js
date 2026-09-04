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
  function difference(a,ab,b,bb) {
    const l=validate(a,ab),r=validate(b,bb);if(l.w!==r.w||l.h!==r.h||l.f!==r.f)throw Error('Attachment layouts differ');
    let count=0,first=null;const mask=new Uint8ClampedArray(l.w*l.h*4);
    for(let i=0;i<l.w*l.h;i++){let changed=false;for(let j=0;j<l.stride;j++)if(ab[i*l.stride+j]!==bb[i*l.stride+j])changed=true;
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
  const api={formats,layout,validate,ufloat,half,pixel,render,difference,compatible,fields,trace,typedPush};
  if(typeof module!=='undefined')module.exports=api;else root.InspectorCore=api;
})(globalThis);
