import { test } from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { WebSocket } from 'ws';
import { createService } from '../server.mjs';

async function client(url, name, build='test-net1') {
  const ws=new WebSocket(url), messages=[], waiters=[];
  ws.on('message', data=>{ const msg=JSON.parse(data); messages.push(msg); for (const resolve of waiters.splice(0)) resolve(); });
  await once(ws,'open');
  const send = msg => ws.send(JSON.stringify(msg));
  const take = async (type, predicate=()=>true) => {
    const deadline=Date.now()+3000;
    while (true) {
      const i=messages.findIndex(m=>m.type===type && predicate(m));
      if (i>=0) return messages.splice(i,1)[0];
      if (Date.now()>deadline) throw Error(`Missing ${type}: ${JSON.stringify(messages)}`);
      await new Promise(resolve=>{ const timer=setTimeout(resolve,25); waiters.push(()=>{clearTimeout(timer);resolve();}); });
    }
  };
  send({type:'hello',protocol:1,build,name});
  const welcome=await take('welcome');
  return { ws,send,take,id:welcome.id,messages };
}

test('real clients: presence, invite, selected fighters, readiness, frame order, rematch, disconnect',async t=>{
  const service=createService({port:0,host:'127.0.0.1'});
  t.after(()=>service.close()); await once(service.server,'listening');
  const url=`ws://127.0.0.1:${service.server.address().port}/play`;
  const a=await client(url,'Alice'), b=await client(url,'Bob'), other=await client(url,'Other','different');
  assert.equal((await a.take('players',m=>m.players.some(p=>p.id===b.id))).players.length,1);
  assert.equal((await other.take('players')).players.length,0);
  b.send({type:'fighter',fighter:1});
  await a.take('players',m=>m.players.some(p=>p.id===b.id&&p.fighter===1));
  a.send({type:'invite',peer:b.id}); assert.equal((await b.take('invite')).name,'Alice');
  b.send({type:'accept'}); const ma=await a.take('match'),mb=await b.take('match');
  assert.deepEqual(ma.fighters,[0,1]); assert.equal(ma.side,0); assert.equal(mb.side,1); assert.equal(ma.id,mb.id);
  a.send({type:'ready',match:ma.id}); b.send({type:'ready',match:ma.id}); await a.take('go'); await b.take('go');
  for (let seq=0;seq<60;seq++) {
    a.send({type:'input',match:ma.id,seq,bits:seq%64}); b.send({type:'input',match:ma.id,seq,bits:63-seq%64});
    const fa=await a.take('frame'),fb=await b.take('frame'); assert.deepEqual(fa,fb); assert.equal(fa.seq,seq); assert.equal(fa.a,seq%64);
  }
  a.send({type:'hash',match:ma.id,seq:60,hash:'12345678'});b.send({type:'hash',match:ma.id,seq:60,hash:'12345678'});
  a.send({type:'rematch',match:ma.id}); b.send({type:'rematch',match:ma.id});
  assert.equal((await a.take('match')).id,ma.id); await b.take('match');
  b.ws.close(); assert.match((await a.take('ended')).message,/disconnected/);
  a.ws.close(); other.ws.close();
});

test('out of order inputs close abusive peer and notify opponent',async t=>{
  const s=createService({port:0,host:'127.0.0.1'});t.after(()=>s.close());await once(s.server,'listening');
  const url=`ws://127.0.0.1:${s.server.address().port}/play`,a=await client(url,'A'),b=await client(url,'B');
  a.send({type:'invite',peer:b.id});await b.take('invite');b.send({type:'accept'});const m=await a.take('match');await b.take('match');
  a.send({type:'ready',match:m.id});b.send({type:'ready',match:m.id});await a.take('go');await b.take('go');
  const closed=once(a.ws,'close');a.send({type:'input',match:m.id,seq:5,bits:0}); assert.equal((await closed)[0],1008);
  assert.match((await b.take('ended')).message,/disconnected/);b.ws.close();
});

test('desynchronization terminates both peers instead of continuing different matches',async t=>{
  const s=createService({port:0,host:'127.0.0.1'});t.after(()=>s.close());await once(s.server,'listening');
  const url=`ws://127.0.0.1:${s.server.address().port}/play`,a=await client(url,'A'),b=await client(url,'B');
  a.send({type:'invite',peer:b.id});await b.take('invite');b.send({type:'accept'});const m=await a.take('match');await b.take('match');
  a.send({type:'ready',match:m.id});b.send({type:'ready',match:m.id});await a.take('go');await b.take('go');
  for(let seq=0;seq<60;seq++){a.send({type:'input',match:m.id,seq,bits:0});b.send({type:'input',match:m.id,seq,bits:0});await a.take('frame');await b.take('frame');}
  a.send({type:'hash',match:m.id,seq:60,hash:'00000001'});b.send({type:'hash',match:m.id,seq:60,hash:'00000002'});
  assert.match((await a.take('ended')).message,/sync lost/);assert.match((await b.take('ended')).message,/sync lost/);a.ws.close();b.ws.close();
});
