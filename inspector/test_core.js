const assert=require('node:assert/strict');
const C=require('./core.js');
const image=(format,width=1,height=1)=>({format,width,height});
const bytes=(size,write)=>{const b=new Uint8Array(size);write(new DataView(b.buffer));return b;};
assert.equal(C.half(0x3c00),1);assert.equal(C.half(0xbc00),-1);assert.equal(C.half(1),2**-24);assert.equal(C.half(0x7c00),Infinity);assert.ok(Number.isNaN(C.half(0x7e00)));
assert.deepEqual(C.pixel(image(2),Uint8Array.from([1,2,3,4]),0,0).values,[3,2,1,4]);
assert.deepEqual(C.pixel(image(1),Uint8Array.from([200,10,0,127]),0,0).values,[200,10,0,127]);
const hdr=bytes(8,v=>[0x3c00,0x3800,0,0x3c00].forEach((n,i)=>v.setUint16(i*2,n,true)));
assert.deepEqual(C.pixel(image(9),hdr,0,0).values,[1,.5,0,1]);
assert.equal(C.render(image(9),hdr)[0],186);assert.notEqual(C.render(image(9),hdr,'rgb',2)[0],186);assert.equal(C.pixel(image(9),hdr,0,0).values[0],1);
const packed=bytes(4,v=>v.setUint32(0,(15<<6)|((15<<6)<<11)|((15<<5)<<22),true));
assert.deepEqual(C.pixel(image(8),packed,0,0).values,[1,1,1]);
assert.deepEqual(C.pixel(image(10),bytes(4,v=>v.setFloat32(0,.5,true)),0,0).values,[.5]);
assert.deepEqual(C.pixel(image(11),bytes(4,v=>v.setUint32(0,0xffffffff,true)),0,0).values,[4294967295]);
assert.deepEqual(Array.from(C.render(image(12),bytes(4,v=>{v.setUint16(0,32768,true);v.setUint16(2,32768,true);}))),[128,128,255,255]);
assert.throws(()=>C.pixel(image(0),new Uint8Array(3),0,0),/Truncated/);assert.throws(()=>C.pixel(image(0),new Uint8Array(4),1,0),/outside/);assert.throws(()=>C.layout(image(99)),/Unsupported/);
const a=new Uint8Array(16),b=a.slice();b[12]=128;const d=C.difference(image(0,2,2),a,image(0,2,2),b);assert.deepEqual(d.first,[1,1]);assert.equal(d.count,1);assert.equal(C.difference(image(0,2,2),a,image(0,2,2),a).count,0);
assert.throws(()=>C.difference(image(0,2,2),a,image(1,2,2),b),/layouts/);
const meta={frame:0,checkpoints:[[null,'end']],sha256:'a'};assert.throws(()=>C.compatible(meta,{...meta,sha256:'b'}),/Different/);C.compatible(meta,{...meta,sha256:'b'},true);assert.throws(()=>C.compatible(meta,{...meta,frame:1},true),/identical/);
assert.deepEqual(C.fields('pipeline=4 label=left texture push_hex=ff00 counts=3,1,0'),{pipeline:'4',label:'left texture',push_hex:'ff00',counts:'3,1,0'});
assert.equal(C.typedPush('00000000000000000000000080000000',[{name:'gain',offset:12,type:'u32'}])[0].values[0],'128');
assert.throws(()=>C.typedPush('ff',[{name:'gain',offset:0,type:'u32'}]),/outside/);assert.throws(()=>C.typedPush('zz',[]),/Invalid/);
console.log('Raw decoder, display transforms, comparison guards and typed layouts passed.');

// Numeric comparisons preserve exact mode and include every source channel.
const fp=v=>bytes(4,d=>d.setFloat32(0,v,true));
assert.equal(C.difference(image(10),fp(1),image(10),fp(1.01)).count,1);
assert.equal(C.difference(image(10),fp(1),image(10),fp(1.01),{numeric:true,absolute:.02}).count,0);
assert.equal(C.difference(image(10),fp(100),image(10),fp(101),{numeric:true,relative:.02}).count,0);
assert.equal(C.difference(image(10),fp(0),image(10),fp(-0)).count,1);
assert.equal(C.difference(image(10),fp(0),image(10),fp(-0),{numeric:true}).count,0);
assert.equal(C.difference(image(10),fp(Infinity),image(10),fp(-Infinity),{numeric:true,absolute:1e20}).count,1);
assert.throws(()=>C.difference(image(10),fp(1),image(10),fp(2),{numeric:true,absolute:-1}),/Tolerances/);
const command=bytes(20,v=>{v.setUint32(0,36,true);v.setUint32(4,2,true);v.setInt32(12,-8,true);});
assert.equal(C.bufferRows(command,0,'indirect',1)[0].fields[3].values[0],'-8');
assert.throws(()=>C.bufferRows(command,1,'indirect',1),/outside/);
assert.throws(()=>C.bufferRows(command,0,'u32',257),/outside/);
assert.equal(C.bufferRows(command,0,'schema',1,20,[{name:'instances',offset:4,type:'u32'}])[0].fields[0].values[0],'2');
assert.equal(C.bufferRows(bytes(8,v=>v.setBigUint64(0,9007199254740993n,true)),0,'u64',1)[0].fields[0].values[0],'9007199254740993');
const history=C.resourceHistory([{event:1,frame:0,op:'pass_begin',detail:'color=1048576 depth=0 extra=0,0'},
 {event:2,frame:0,op:'draw_indirect',detail:'indices=1048577 commands=1048578 counts=1048579 refs=0:buffer:1048578:20,'},
 {event:3,frame:0,op:'pass_end',detail:''},{event:4,frame:0,op:'dispatch',detail:''},
 {event:5,frame:0,op:'free_buffer',detail:'value=1048578'}]);
assert.equal(history.filter(r=>r.access==='possible write').length,1);
assert.equal(history.filter(r=>r.access==='read').length,3);
assert.equal(history.find(r=>r.access==='address candidate').id,'1048578');
assert.equal(history.at(-1).access,'free');
console.log('Numeric tolerances, indirect/structured buffers, 64-bit values and resource histories passed.');

const resolves=C.resourceHistory([{event:1,frame:0,op:'pass_begin',detail:'color=10 depth=11 extra=12,0 resolves=20,21,0,22 raster_samples=0'},
 {event:2,frame:0,op:'draw',detail:''},{event:3,frame:0,op:'pass_end',detail:''}]);
assert.deepEqual(resolves.filter(r=>r.access==='resolve write').map(r=>[r.event,r.id]),[[3,'20'],[3,'21'],[3,'22']]);
