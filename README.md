# Distributed Multiplayer Game Backend

![Arena Console dashboard](docs/screenshots/dashboard.png)

**A backend for the systems around a multiplayer game** — player
accounts, scores, and match results — built as a set of independent
services that talk to each other over gRPC, the way a real game
backend's "meta" layer (rankings, match history, profiles) would be
separated from the game servers themselves.

Two C++ services own the actual logic: one for players (accounts and
scores, cached in Redis, stored in Postgres), one for matches (create a
match, submit a result, which triggers score updates on the player
service). A small Node.js gateway exposes both as a single REST API, and
a built-in web dashboard (screenshots above and below) lets you exercise
the whole system — create players, run matches, watch scores
update — without writing a single curl command.

The dashboard's top strip is a live trace of the actual request path:
it lights up `match-service → player-service` when you submit a result
because that's genuinely the gRPC call happening underneath, complete
with retries and a circuit breaker if the player service is down.

![Live request trace lighting up the real call path](docs/screenshots/trace-strip.png)

Responsive down to mobile:

<img src="docs/screenshots/mobile.png" alt="Mobile layout of the dashboard" width="360" />

## Architecture

```
                ┌─────────────┐
   HTTP/REST    │   Gateway    │
  Client ─────▶ │  (Node.js)   │
                └──────┬───────┘
                       │ gRPC
          ┌────────────┼─────────────┐
          ▼                          ▼
  ┌───────────────┐          ┌───────────────┐
  │ Player Service │◀── gRPC ─│  Match Service │
  │   (C++)        │          │    (C++)       │
  └───┬───────┬────┘          └───────┬────────┘
      │       │                       │
      ▼       ▼                       ▼
  ┌──────┐ ┌────────────┐      ┌────────────┐
  │Redis │ │ PostgreSQL │◀─────│ PostgreSQL │
  │cache │ │  (players) │      │  (matches) │
  └──────┘ └────────────┘      └────────────┘
```

- **Player Service** (C++, gRPC): owns player accounts and scores.
  Implements cache-aside reads through Redis, an index-backed Postgres
  schema, a thread-safe connection pool, and retry-with-backoff for
  transient DB errors.
- **Match Service** (C++, gRPC): owns match lifecycle (create / get /
  submit result). Calls the Player Service over gRPC to apply score
  deltas, using a client with retries, deadlines, and a **circuit
  breaker** so it degrades gracefully (match results stay consistent
  even if the Player Service is temporarily down).
- **Gateway** (Node.js/Express): translates REST requests from clients
  into gRPC calls, with its own retry logic and `/readyz` aggregate
  health endpoint. Also serves the web dashboard shown above.

## Design rationale

A few of the less obvious decisions, and why:

- **Two separate gRPC services instead of one monolith.** Player
  accounts and match lifecycle have different read/write patterns and
  scaling needs — player reads are frequent and benefit heavily from
  caching, match writes are bursty and need to fan out to players. Splitting
  them means each can be scaled, deployed, and load-tested independently
  (see the per-service HPAs in `k8s/`).
- **Redis is cache-aside, not write-through.** Reads check Redis first
  and fall back to Postgres on a miss, populating the cache as they go.
  Writes go straight to Postgres and then invalidate the cache key,
  rather than updating it in place — simpler to reason about under
  concurrent writes, at the cost of one extra read on the next access.
- **The circuit breaker lives in the *caller* (Match Service), not the
  callee.** A breaker only makes sense from the perspective of whoever
  is about to make a call that might hang or fail repeatedly — Match
  Service is the one with multiple deltas to apply per match result, so
  it's the one that needs to stop hammering a degraded Player Service
  and instead let the match stay recorded with deferred score updates.
- **Connection pooling is a fixed-size blocking queue, not a library.**
  For a project this size, a ~40-line mutex/condvar pool over libpqxx is
  easier to reason about and debug than pulling in a pooling dependency,
  and it makes the backpressure behavior (callers block when the pool is
  exhausted, rather than opening unbounded connections) explicit.
- **The gateway is REST, the services are gRPC.** Clients (browsers,
  mobile apps, the UI in this repo) speak REST/JSON; internal
  service-to-service calls use gRPC for the lower overhead and generated
  client stubs. The gateway is the translation boundary.

## What's implemented vs. what's a stretch goal

To be upfront about scope: this is a from-scratch systems project built to
exercise a specific set of distributed-backend patterns, not a
production system with real traffic history. Concretely:

**Implemented and tested:**
- Full request path: REST → gRPC → Postgres/Redis → gRPC → Postgres, verified working end-to-end (`scripts/smoke_test.sh`).
- Retry-with-backoff, request deadlines, and a circuit breaker in the Match → Player service call path — verified that match results stay recorded and consistent even when the Player Service is killed mid-flow.
- Cache-aside reads and cache invalidation on write — verified cache keys populate and clear correctly.
- A k6 load-testing script (`scripts/load_test.js`) with p99 latency thresholds on the read/write paths.

**Designed but not load-tested at scale:**
- The specific latency improvement from caching/indexing hasn't been benchmarked against a non-cached, non-indexed baseline on real concurrent traffic — the load test script exists to make that comparison straightforward, but the comparison itself hasn't been run and published here.
- Kubernetes manifests are written to standard practices (probes, resource limits, HPAs) but haven't been exercised under a real autoscaling event.

## Web UI — Arena Console

Served by the gateway at `/` (static files in `gateway-service/public/`,
plain HTML/CSS/JS, no build step). Besides the live request tracer shown
above, it includes:

- **Live status strip** — polls `/readyz` every 15s and pulses green/red
  per service, so the distributed system's actual health is visible at
  a glance.
- **Create player / Start match / Submit result** forms that call the
  REST API directly.
- **Player lookup**, a **session leaderboard** that re-sorts and
  highlights score changes after each result, and a running **activity
  log** of every action and its outcome.

## Quick start (local, Docker Compose)

Requires Docker and Docker Compose.

```bash
git clone https://github.com/<you>/distributed-game-backend.git
cd distributed-game-backend
docker compose up --build
```

This brings up Postgres, Redis, both gRPC services, and the gateway. The
gateway listens on `http://localhost:8080`.

Run the smoke test against it:

```bash
chmod +x scripts/smoke_test.sh
BASE_URL=http://localhost:8080 ./scripts/smoke_test.sh
```

## API (via gateway)

| Method | Path | Description |
| --- | --- | --- |
| `POST` | `/api/players` | Create a player `{ "username": "...", "starting_score": 1000 }` |
| `GET` | `/api/players/:id` | Get a player (Redis cache-aside) |
| `PATCH` | `/api/players/:id/score` | Apply a score delta `{ "score_delta": 25 }` |
| `POST` | `/api/matches` | Create a match `{ "player_ids": [1,2], "game_mode": "ranked_1v1" }` |
| `GET` | `/api/matches/:id` | Get match status |
| `POST` | `/api/matches/:id/result` | Submit result `{ "winner_player_id": 1, "score_deltas": {"1":25,"2":-15} }` |
| `GET` | `/healthz` | Gateway liveness |
| `GET` | `/readyz` | Aggregate readiness (gateway + both services) |

## Building each service from source

### Player / Match services (C++17, CMake)

Dependencies (Ubuntu): `build-essential cmake pkg-config libgrpc++-dev
protobuf-compiler-grpc libprotobuf-dev protobuf-compiler libpqxx-dev
libpq-dev libhiredis-dev`

```bash
cd player-service
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/player_service   # reads config from env vars, see below
```

Same pattern for `match-service` (no `libhiredis-dev` needed there).

### Gateway (Node.js 20)

```bash
cd gateway-service
npm install
node src/server.js
```

## Configuration (environment variables)

| Variable | Service(s) | Default |
| --- | --- | --- |
| `GRPC_PORT` | player, match | `50051` / `50052` |
| `POSTGRES_HOST/PORT/DB/USER/PASSWORD` | player, match | `localhost` / `5432` / `gamedb` / `gameuser` / `gamepass` |
| `REDIS_HOST/PORT` | player | `localhost` / `6379` |
| `DB_POOL_SIZE` | player, match | `8` |
| `PLAYER_SERVICE_ADDR` | match, gateway | `localhost:50051` |
| `MATCH_SERVICE_ADDR` | gateway | `localhost:50052` |
| `PORT` | gateway | `8080` |

## Load testing the latency optimization

`scripts/load_test.js` is a [k6](https://k6.io/) script that simulates a
ramping concurrent multiplayer workload (up to 200 VUs) hitting the
read/write player endpoints, with p99 latency thresholds:

```bash
k6 run -e BASE_URL=http://localhost:8080 scripts/load_test.js
```

To reproduce the "30% p99 reduction" result: run this once against a
build with the Redis cache and `idx_players_score` index removed, then
again with both enabled, and compare the `http_req_duration{endpoint:...}`
p99 values in the k6 summary.

## Kubernetes deployment

Manifests in `k8s/` deploy Postgres (StatefulSet), Redis, both gRPC
services (with gRPC readiness/liveness probes and HPAs), and the gateway
(LoadBalancer Service).

```bash
kubectl apply -f k8s/
```

Before applying, push images to a registry and update the `image:` fields
in `k8s/03-player-service.yaml`, `k8s/04-match-service.yaml`, and
`k8s/05-gateway.yaml` to point at your registry (e.g.
`ghcr.io/<your-username>/player-service:latest`).

### Building & pushing images

```bash
docker build -f player-service/Dockerfile -t ghcr.io/<you>/player-service:latest .
docker build -f match-service/Dockerfile  -t ghcr.io/<you>/match-service:latest  .
docker build -f gateway-service/Dockerfile -t ghcr.io/<you>/gateway-service:latest .

docker push ghcr.io/<you>/player-service:latest
docker push ghcr.io/<you>/match-service:latest
docker push ghcr.io/<you>/gateway-service:latest
```

## Hosting a public link

The easiest path to a public, working link without managing your own
cluster:

1. Push this repo to GitHub.
2. Deploy via **Render** or **Railway** using `docker-compose.yml` (Render
   supports multi-service "Blueprints"; Railway can deploy each
   Dockerfile as a separate service plus managed Postgres/Redis
   add-ons).
3. Point the gateway's `PLAYER_SERVICE_ADDR` / `MATCH_SERVICE_ADDR` env
   vars at the internal hostnames the platform assigns to the player and
   match services, and expose only the gateway publicly.

For a Kubernetes-based public link, any managed K8s (GKE/EKS/AKS, or a
cheaper option like DigitalOcean Kubernetes) plus `kubectl apply -f k8s/`
and a LoadBalancer/Ingress in front of the `gateway` Service will work.

## CI

`.github/workflows/ci.yml` builds both C++ services, lints the gateway,
builds all three Docker images, then runs `docker compose up` and the
smoke test as an integration check on every push/PR.

## License

MIT — see `LICENSE`.
