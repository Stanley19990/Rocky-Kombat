# Rocky Kombat multiplayer alpha

This standalone service supplies real guest presence, invitations, opponent
acceptance, selected fighters, a ready barrier, ordered 60 Hz inputs, rematch
agreement and disconnect handling. It does not create fake opponents or CPU
replacements. Sessions are unranked and have no durable accounts or records.

## Run and test

Use Node 22 or newer:

```
npm ci
npm test
npm start
```

Health: `http://127.0.0.1:8080/health`. WebSocket: `ws://127.0.0.1:8080/play`.
For a local desktop test start two instances of the same game build with:

```
-rkc.OnlineUrl=ws://127.0.0.1:8080/play -rkc.PlayerName=Alice
-rkc.OnlineUrl=ws://127.0.0.1:8080/play -rkc.PlayerName=Bob
```

In each instance choose Play > Rocky Boxing Arena > Online Versus, select your
fighter, then invite an available player. The other player accepts. Both must
finish loading before either simulation starts. Both must request a rematch.
Escape/back during online play leaves the match; it cannot pause the opponent.

## Deploy when hosting is connected

Build the provided Dockerfile from this directory and run one instance with the
host's `PORT` environment variable (default 8080). No persistent volume or secrets
are required. Route HTTPS and WebSocket upgrades to that instance through the
host's TLS proxy. Verify `/health`, then use `wss://YOUR-HOST/play`.

Set `ServerUrl` in `[RockyKombat.Online]` in `Config/DefaultGame.ini` before
packaging the Android build; leave `PlayerName` empty for an anonymous guest name.
Never put hosting tokens or signing secrets in the game configuration. Internet
connections require WSS; plain WS is permitted only on desktop loopback.

Run two game builds on separate internet connections and verify selection,
invitations, both fighters' input, completion, rematch and reconnect before
calling public internet play verified. Hosting is intentionally not provisioned
in this change; the default game endpoint is empty and the lobby reports this.

## Limits

This alpha uses delay-based lockstep, not rollback. Latency or a slow peer can
slow the match. A 15-second stall ends it. Same-build peers are paired and state
checksums are compared each second; a mismatch stops the match instead of
continuing divergent simulations. Cross-platform determinism and Android network
performance need device testing. The relay does not validate combat outcomes and
must not be used for ranked play, money, wagers or championship records.

Guest names are display labels, not verified identities. The process bounds
connections, message sizes, queue lengths and message rates. Deploy one replica:
presence and matches are in memory. A restart disconnects clients. Production
accounts, moderation, abuse protection and persistence need a separate increment.
