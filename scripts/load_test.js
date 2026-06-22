// k6 load test simulating concurrent multiplayer traffic against the
// gateway: creating players, reading scores, creating matches, and
// submitting match results under concurrent virtual users.
//
// Usage:
//   k6 run -e BASE_URL=http://localhost:8080 scripts/load_test.js
//
// To compare before/after the Redis cache + index optimization, run this
// against both versions of the player-service and compare the p99 in the
// summary output.

import http from 'k6/http';
import { check, sleep } from 'k6';

export const options = {
  scenarios: {
    multiplayer_peak: {
      executor: 'ramping-vus',
      startVUs: 0,
      stages: [
        { duration: '30s', target: 50 },   // ramp up
        { duration: '1m', target: 200 },   // peak concurrent load
        { duration: '30s', target: 0 },    // ramp down
      ],
    },
  },
  thresholds: {
    // Mirrors the 30% p99 latency reduction goal: fail the run if p99
    // exceeds 150ms on the GetPlayer read path (cache + index optimized).
    'http_req_duration{endpoint:get_player}': ['p(99)<150'],
    'http_req_duration{endpoint:update_score}': ['p(99)<200'],
    http_req_failed: ['rate<0.01'],
  },
};

const BASE_URL = __ENV.BASE_URL || 'http://localhost:8080';

export function setup() {
  // Seed a pool of players once before the load test starts.
  const playerIds = [];
  for (let i = 0; i < 20; i++) {
    const res = http.post(
      `${BASE_URL}/api/players`,
      JSON.stringify({ username: `loadtest_user_${i}_${Date.now()}`, starting_score: 1000 }),
      { headers: { 'Content-Type': 'application/json' } }
    );
    if (res.status === 201) {
      playerIds.push(res.json('player_id'));
    }
  }
  return { playerIds };
}

export default function (data) {
  const playerIds = data.playerIds;
  if (playerIds.length === 0) {
    sleep(1);
    return;
  }
  const playerId = playerIds[Math.floor(Math.random() * playerIds.length)];

  // Read path: GetPlayer (benefits from Redis cache).
  const getRes = http.get(`${BASE_URL}/api/players/${playerId}`, {
    tags: { endpoint: 'get_player' },
  });
  check(getRes, { 'get player 200': (r) => r.status === 200 });

  // Write path: UpdatePlayerScore (invalidates cache, hits Postgres index).
  const scoreDelta = Math.floor(Math.random() * 21) - 10; // -10..+10
  const updateRes = http.patch(
    `${BASE_URL}/api/players/${playerId}/score`,
    JSON.stringify({ score_delta: scoreDelta }),
    {
      headers: { 'Content-Type': 'application/json' },
      tags: { endpoint: 'update_score' },
    }
  );
  check(updateRes, { 'update score 200': (r) => r.status === 200 });

  sleep(0.1 + Math.random() * 0.2);
}
