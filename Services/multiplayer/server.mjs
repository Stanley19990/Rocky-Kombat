import http from 'node:http';
import { randomUUID } from 'node:crypto';
import { pathToFileURL } from 'node:url';
import { WebSocketServer, WebSocket } from 'ws';

// Internal, unranked guest sessions. No accounts, rankings or durable identity.
// The relay orders inputs; both clients run the existing 60 Hz boxing simulator.
export function createService({ port = Number(process.env.PORT || 8080), host = process.env.HOST || '0.0.0.0' } = {}) {
  const peers = new Map(), matches = new Map();
  const server = http.createServer((req, res) => {
    if (req.url !== '/health') { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
    res.end(JSON.stringify({ service: 'rocky-kombat', protocol: 1, status: 'ok' }));
  });
  const wss = new WebSocketServer({ server, path: '/play', maxPayload: 2048, perMessageDeflate: false });
  const send = (p, msg) => {
    if (p?.ws.readyState !== WebSocket.OPEN) return;
    if (p.ws.bufferedAmount > 65536) { p.ws.close(1008, 'Slow connection'); return; }
    p.ws.send(JSON.stringify(msg));
  };
  const presence = () => {
    for (const p of peers.values()) if (p.hello) send(p, { type: 'players', players: [...peers.values()]
      .filter(q => q.hello && q.id !== p.id && q.build === p.build)
      .map(q => ({ id: q.id, name: q.name, fighter: q.fighter, busy: Boolean(q.match || q.invite) })) });
  };
  const cancelInvite = p => {
    if (!p.invite) return;
    const q = peers.get(p.invite.peer); p.invite = null;
    if (q?.invite?.peer === p.id) { q.invite = null; send(q, { type: 'notice', message: 'Invitation closed.' }); }
  };
  const finish = (m, reason) => {
    if (!m || !matches.has(m.id)) return;
    matches.delete(m.id);
    for (const id of m.ids) { const p = peers.get(id); if (p) { p.match = null; send(p, { type: 'ended', message: reason }); } }
    presence();
  };
  const announce = m => {
    m.ready = [false, false]; m.rematch = [false, false]; m.queues = [[], []];
    m.next = [0, 0]; m.frame = 0; m.hashes = new Map(); m.running = false; m.last = Date.now();
    m.ids.forEach((id, side) => send(peers.get(id), { type: 'match', id: m.id, side,
      fighters: m.ids.map(i => peers.get(i).fighter), names: m.ids.map(i => peers.get(i).name) }));
  };
  wss.on('connection', (ws, req) => {
    // Bound unauthenticated guest resources per address and in total.
    const ip = req.socket.remoteAddress;
    if (peers.size >= 200 || [...peers.values()].filter(p => p.ip === ip).length >= 12) { ws.close(1013, 'Server full'); return; }
    const p = { ws, ip, id: randomUUID(), hello: false, name: '', fighter: 0, build: '', match: null,
      invite: null, alive: true, window: Date.now(), count: 0, connected: Date.now() };
    peers.set(p.id, p);
    ws.on('pong', () => { p.alive = true; });
    ws.on('message', (data, binary) => {
      try {
        if (binary) throw Error('Text protocol required');
        if (Date.now() - p.window >= 1000) { p.window = Date.now(); p.count = 0; }
        if (++p.count > 160) throw Error('Rate limit');
        const msg = JSON.parse(data.toString());
        if (!msg || typeof msg !== 'object' || Array.isArray(msg)) throw Error('Invalid message');
        if (!p.hello) {
          if (msg.type !== 'hello' || msg.protocol !== 1 || typeof msg.build !== 'string' || !/^[a-zA-Z0-9._-]{1,80}$/.test(msg.build)) throw Error('Incompatible build');
          p.hello = true; p.build = msg.build;
          p.name = typeof msg.name === 'string' ? msg.name.replace(/[^a-zA-Z0-9 _-]/g, '').trim().slice(0, 20) : '';
          if (!p.name) p.name = `Boxer-${p.id.slice(0, 4)}`;
          send(p, { type: 'welcome', id: p.id, name: p.name }); presence(); return;
        }
        if (msg.type === 'fighter' && !p.match && !p.invite && (msg.fighter === 0 || msg.fighter === 1)) { p.fighter = msg.fighter; presence(); return; }
        if (msg.type === 'invite' && !p.match && !p.invite) {
          const q = peers.get(msg.peer);
          if (!q || q === p || !q.hello || q.build !== p.build || q.match || q.invite) { send(p, { type: 'notice', message: 'That player is unavailable.' }); return; }
          const expires = Date.now() + 30000;
          p.invite = { peer: q.id, incoming: false, expires }; q.invite = { peer: p.id, incoming: true, expires };
          send(q, { type: 'invite', peer: p.id, name: p.name }); send(p, { type: 'notice', message: 'Invitation sent. Waiting for acceptance.' }); presence(); return;
        }
        if (msg.type === 'decline') { cancelInvite(p); presence(); return; }
        if (msg.type === 'accept' && p.invite?.incoming) {
          const q = peers.get(p.invite.peer);
          if (!q?.invite || q.invite.peer !== p.id || p.invite.expires < Date.now()) { cancelInvite(p); presence(); return; }
          const m = { id: randomUUID(), ids: [q.id, p.id] };
          p.invite = q.invite = null; p.match = q.match = m.id; matches.set(m.id, m); announce(m); presence(); return;
        }
        const m = matches.get(p.match), side = m?.ids.indexOf(p.id);
        if (msg.type === 'leave') { cancelInvite(p); finish(m, 'A player left the match.'); presence(); return; }
        if (!m || msg.match !== m.id) return;
        if (msg.type === 'ready') { m.ready[side] = true; if (!m.running && m.ready.every(Boolean)) { m.running = true; m.last = Date.now(); m.ids.forEach(id => send(peers.get(id), { type: 'go' })); } return; }
        if (msg.type === 'rematch') { m.rematch[side] = true; send(p, { type: 'notice', message: 'Rematch ready. Waiting for opponent.' }); if (m.rematch.every(Boolean)) announce(m); return; }
        if (msg.type === 'input' && m.running) {
          if (!Number.isInteger(msg.seq) || msg.seq !== m.next[side] || !Number.isInteger(msg.bits) || msg.bits < 0 || msg.bits > 63 || m.queues[side].length >= 8) throw Error('Invalid input order');
          m.next[side]++; m.queues[side].push(msg.bits);
          while (m.queues[0].length && m.queues[1].length) {
            const packet = { type: 'frame', seq: m.frame++, a: m.queues[0].shift(), b: m.queues[1].shift() };
            m.ids.forEach(id => send(peers.get(id), packet)); m.last = Date.now();
          }
          return;
        }
        if (msg.type === 'hash' && Number.isInteger(msg.seq) && msg.seq > 0 && msg.seq % 60 === 0 && msg.seq <= m.frame && msg.seq >= m.frame - 16 && typeof msg.hash === 'string' && /^[0-9a-f]{8}$/.test(msg.hash)) {
          let pair = m.hashes.get(msg.seq); if (!pair) { pair = []; m.hashes.set(msg.seq, pair); }
          pair[side] = msg.hash;
          if (pair[0] && pair[1]) { m.hashes.delete(msg.seq); if (pair[0] !== pair[1]) finish(m, 'Match stopped: simulation sync lost.'); }
        }
      } catch { ws.close(1008, 'Invalid or excessive messages'); }
    });
    ws.on('error', () => {});
    ws.on('close', () => { cancelInvite(p); finish(matches.get(p.match), 'Opponent disconnected.'); peers.delete(p.id); presence(); });
  });
  const timer = setInterval(() => {
    for (const p of peers.values()) {
      if (!p.alive || (!p.hello && Date.now() - p.connected > 10000)) { p.ws.terminate(); continue; }
      p.alive = false; p.ws.ping();
      if (p.invite?.expires < Date.now()) { cancelInvite(p); send(p, { type: 'notice', message: 'Invitation expired.' }); presence(); }
    }
    for (const m of matches.values()) if (Date.now() - m.last > (m.running ? 15000 : 45000)) finish(m, 'Match connection timed out.');
  }, 5000);
  server.listen(port, host);
  return { server, wss, close: async () => { clearInterval(timer); for (const p of peers.values()) p.ws.terminate(); await new Promise(resolve => wss.close(resolve)); await new Promise(resolve => server.close(resolve)); } };
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const service = createService({ port: Number(process.argv[2] || process.env.PORT || 8080) });
  service.server.on('listening', () => console.log(`Rocky Kombat relay listening on ${service.server.address().port}`));
  for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, async () => { await service.close(); process.exit(0); });
}
