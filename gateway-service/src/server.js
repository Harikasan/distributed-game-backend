const express = require('express');
const cors = require('cors');
const path = require('path');
const {
  playerClient,
  matchClient,
  callWithDeadline,
  PLAYER_SERVICE_ADDR,
  MATCH_SERVICE_ADDR,
} = require('./grpc_clients');

const app = express();
app.use(cors());
app.use(express.json());

// Serve the Arena Console UI (static files in ../public).
app.use(express.static(path.join(__dirname, '..', 'public')));

const PORT = process.env.PORT || 8080;

// Retries a gRPC call a few times with short backoff on UNAVAILABLE /
// DEADLINE_EXCEEDED, which are typically transient (pod restarts, brief
// network blips). Other errors (NOT_FOUND, ALREADY_EXISTS) fail fast.
async function withRetry(fn, maxAttempts = 3) {
  let attempt = 0;
  let backoffMs = 50;
  while (true) {
    try {
      return await fn();
    } catch (err) {
      attempt++;
      const transient = err.code === 14 /* UNAVAILABLE */ || err.code === 4 /* DEADLINE_EXCEEDED */;
      if (!transient || attempt >= maxAttempts) throw err;
      await new Promise((r) => setTimeout(r, backoffMs));
      backoffMs *= 2;
    }
  }
}

function grpcErrorToHttp(err) {
  switch (err.code) {
    case 5: // NOT_FOUND
      return 404;
    case 6: // ALREADY_EXISTS
      return 409;
    case 14: // UNAVAILABLE
    case 4: // DEADLINE_EXCEEDED
      return 503;
    default:
      return 500;
  }
}

// --- Player endpoints -------------------------------------------------

app.post('/api/players', async (req, res) => {
  const { username, starting_score = 0 } = req.body;
  if (!username) return res.status(400).json({ error: 'username is required' });

  try {
    const response = await withRetry(() =>
      callWithDeadline(playerClient, 'CreatePlayer', { username, starting_score })
    );
    res.status(201).json(response);
  } catch (err) {
    res.status(grpcErrorToHttp(err)).json({ error: err.details || err.message });
  }
});

app.get('/api/players/:id', async (req, res) => {
  const playerId = req.params.id;
  try {
    const response = await withRetry(() =>
      callWithDeadline(playerClient, 'GetPlayer', { player_id: playerId })
    );
    if (!response.found) return res.status(404).json({ error: response.error || 'not found' });
    res.json(response);
  } catch (err) {
    res.status(grpcErrorToHttp(err)).json({ error: err.details || err.message });
  }
});

app.patch('/api/players/:id/score', async (req, res) => {
  const playerId = req.params.id;
  const { score_delta } = req.body;
  if (score_delta === undefined) return res.status(400).json({ error: 'score_delta is required' });

  try {
    const response = await withRetry(() =>
      callWithDeadline(playerClient, 'UpdatePlayerScore', { player_id: playerId, score_delta })
    );
    if (!response.found) return res.status(404).json({ error: response.error || 'not found' });
    res.json(response);
  } catch (err) {
    res.status(grpcErrorToHttp(err)).json({ error: err.details || err.message });
  }
});

// --- Match endpoints ----------------------------------------------------

app.post('/api/matches', async (req, res) => {
  const { player_ids, game_mode = 'default' } = req.body;
  if (!Array.isArray(player_ids) || player_ids.length === 0) {
    return res.status(400).json({ error: 'player_ids must be a non-empty array' });
  }

  try {
    const response = await withRetry(() =>
      callWithDeadline(matchClient, 'CreateMatch', { player_ids, game_mode })
    );
    res.status(201).json(response);
  } catch (err) {
    res.status(grpcErrorToHttp(err)).json({ error: err.details || err.message });
  }
});

app.get('/api/matches/:id', async (req, res) => {
  try {
    const response = await withRetry(() =>
      callWithDeadline(matchClient, 'GetMatch', { match_id: req.params.id })
    );
    res.json(response);
  } catch (err) {
    res.status(grpcErrorToHttp(err)).json({ error: err.details || err.message });
  }
});

app.post('/api/matches/:id/result', async (req, res) => {
  const { winner_player_id, score_deltas = {} } = req.body;
  if (winner_player_id === undefined) {
    return res.status(400).json({ error: 'winner_player_id is required' });
  }

  try {
    const response = await withRetry(() =>
      callWithDeadline(matchClient, 'SubmitMatchResult', {
        match_id: req.params.id,
        winner_player_id,
        score_deltas,
      })
    );
    res.json(response);
  } catch (err) {
    res.status(grpcErrorToHttp(err)).json({ error: err.details || err.message });
  }
});

// --- Health / readiness --------------------------------------------------

app.get('/healthz', (req, res) => res.json({ status: 'ok' }));

app.get('/readyz', async (req, res) => {
  const result = { gateway: 'ok', player_service: 'unknown', match_service: 'unknown' };
  let overallOk = true;

  try {
    const p = await callWithDeadline(playerClient, 'HealthCheck', {}, 1000);
    result.player_service = p.status_message;
    if (!p.healthy) overallOk = false;
  } catch (err) {
    result.player_service = `unreachable (${err.message})`;
    overallOk = false;
  }

  try {
    const m = await callWithDeadline(matchClient, 'HealthCheck', {}, 1000);
    result.match_service = m.status_message;
    if (!m.healthy) overallOk = false;
  } catch (err) {
    result.match_service = `unreachable (${err.message})`;
    overallOk = false;
  }

  res.status(overallOk ? 200 : 503).json(result);
});

app.listen(PORT, () => {
  console.log(`[gateway] listening on port ${PORT}`);
  console.log(`[gateway] player service -> ${PLAYER_SERVICE_ADDR}`);
  console.log(`[gateway] match service  -> ${MATCH_SERVICE_ADDR}`);
});
