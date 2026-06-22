/* ============================================================
   Arena Console — app logic
   Talks to the gateway's REST API (same origin, /api/*).
   ============================================================ */

const API_BASE = ''; // same-origin

// ---- Local "tracked players" leaderboard state -------------
// The backend doesn't expose a leaderboard endpoint, so the UI
// keeps a lightweight session-local registry of players it has
// seen (created or looked up) and refreshes their scores.
const trackedPlayers = new Map(); // id -> { id, username, score }

// ---- Request-path tracer (signature element) -----------------------
// Lights up the trace-strip nodes/lines as real requests flow through
// browser -> gateway -> service -> datastore, so the diagram in the
// README is also a live readout of what's actually happening.

const traceEls = {
  client: document.querySelector('.trace-node[data-node="client"]'),
  gateway: document.querySelector('.trace-node[data-node="gateway"]'),
  service: document.querySelector('.trace-node[data-node="service"]'),
  store: document.querySelector('.trace-node[data-node="store"]'),
  lineClientGateway: document.querySelector('.trace-line[data-line="client-gateway"]'),
  lineGatewayService: document.querySelector('.trace-line[data-line="gateway-service"]'),
  lineServiceStore: document.querySelector('.trace-line[data-line="service-store"]'),
  serviceLabel: document.getElementById('trace-service-label'),
  storeLabel: document.getElementById('trace-store-label'),
};

let traceResetTimer = null;

/**
 * Animates the trace strip for one request. `meta` describes which
 * downstream service and store the call actually touches, so the
 * labels reflect reality (e.g. a player read touches Redis+Postgres,
 * a match create only touches Postgres).
 */
function runTrace({ service = 'Service', store = 'Postgres', ok = true, durationMs = 900 } = {}) {
  clearTimeout(traceResetTimer);
  const state = ok ? 'is-active' : 'is-error';

  // Reset first so rapid-fire calls restart the animation cleanly.
  Object.values(traceEls).forEach((el) => {
    if (el instanceof Element) el.classList.remove('is-active', 'is-error');
  });

  traceEls.serviceLabel.textContent = service;
  traceEls.storeLabel.textContent = store;

  const steps = [
    () => traceEls.client.classList.add(state),
    () => traceEls.lineClientGateway.classList.add(state),
    () => traceEls.gateway.classList.add(state),
    () => traceEls.lineGatewayService.classList.add(state),
    () => traceEls.service.classList.add(state),
    () => traceEls.lineServiceStore.classList.add(state),
    () => traceEls.store.classList.add(state),
  ];

  const stepDelay = durationMs / steps.length;
  steps.forEach((step, i) => setTimeout(step, i * stepDelay));

  traceResetTimer = setTimeout(() => {
    Object.values(traceEls).forEach((el) => {
      if (el instanceof Element) el.classList.remove('is-active', 'is-error');
    });
  }, durationMs + 1400);
}

// ---- DOM refs -------------------------------------------------
const els = {
  statusStrip: document.getElementById('status-strip'),
  refreshHealthBtn: document.getElementById('refresh-health'),

  formCreatePlayer: document.getElementById('form-create-player'),
  createPlayerHint: document.getElementById('create-player-hint'),

  formCreateMatch: document.getElementById('form-create-match'),
  createMatchHint: document.getElementById('create-match-hint'),

  formSubmitResult: document.getElementById('form-submit-result'),
  submitResultHint: document.getElementById('submit-result-hint'),

  formLookup: document.getElementById('form-lookup'),
  lookupResult: document.getElementById('lookup-result'),
  lookupName: document.getElementById('lookup-name'),
  lookupId: document.getElementById('lookup-id'),
  lookupScore: document.getElementById('lookup-score'),

  leaderboardBody: document.getElementById('leaderboard-body'),
  activityLog: document.getElementById('activity-log'),
};

// ---- Helpers ----------------------------------------------------

function timeNow() {
  return new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function logActivity(message, kind = 'info') {
  const empty = els.activityLog.querySelector('.log-empty');
  if (empty) empty.remove();

  const li = document.createElement('li');
  li.className = `log-${kind}`;

  const time = document.createElement('span');
  time.className = 'log-time';
  time.textContent = timeNow();

  const text = document.createElement('span');
  text.className = 'log-text';
  text.textContent = message;

  li.appendChild(time);
  li.appendChild(text);
  els.activityLog.prepend(li);

  // Cap log length
  while (els.activityLog.children.length > 30) {
    els.activityLog.removeChild(els.activityLog.lastChild);
  }
}

function setHint(el, message, kind) {
  el.textContent = message;
  el.classList.remove('is-success', 'is-error');
  if (kind) el.classList.add(`is-${kind}`);
}

function setButtonLoading(form, loading) {
  const btn = form.querySelector('button[type="submit"]');
  if (!btn) return;
  btn.disabled = loading;
  btn.classList.toggle('is-loading', loading);
}

async function apiRequest(path, options = {}) {
  const res = await fetch(`${API_BASE}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });

  let body = null;
  try {
    body = await res.json();
  } catch (_) {
    // no/invalid JSON body
  }

  if (!res.ok) {
    const message = (body && body.error) || `Request failed (${res.status})`;
    const err = new Error(message);
    err.status = res.status;
    err.body = body;
    throw err;
  }
  return body;
}

// ---- Leaderboard ----------------------------------------------

function upsertTrackedPlayer(player, { highlight = false } = {}) {
  const id = String(player.player_id ?? player.id);
  const prev = trackedPlayers.get(id);
  trackedPlayers.set(id, {
    id,
    username: player.username ?? prev?.username ?? `player_${id}`,
    score: player.score ?? prev?.score ?? 0,
    prevScore: prev?.score,
  });
  renderLeaderboard(highlight ? id : null);
}

function renderLeaderboard(highlightId) {
  const players = Array.from(trackedPlayers.values()).sort((a, b) => b.score - a.score);

  if (players.length === 0) {
    els.leaderboardBody.innerHTML = `
      <tr class="empty-row">
        <td colspan="4">No players tracked yet — create one to begin.</td>
      </tr>`;
    return;
  }

  els.leaderboardBody.innerHTML = '';
  players.forEach((p, idx) => {
    const tr = document.createElement('tr');
    if (String(p.id) === String(highlightId)) tr.classList.add('is-new');

    const rankTd = document.createElement('td');
    const badge = document.createElement('span');
    badge.className = `rank-badge${idx === 0 ? ' rank-1' : ''}`;
    badge.textContent = idx + 1;
    rankTd.appendChild(badge);

    const nameTd = document.createElement('td');
    nameTd.textContent = p.username;

    const idTd = document.createElement('td');
    idTd.className = 'col-id';
    idTd.textContent = `#${p.id}`;

    const scoreTd = document.createElement('td');
    scoreTd.className = 'col-score';
    scoreTd.textContent = p.score;

    if (p.prevScore !== undefined && p.score !== p.prevScore) {
      scoreTd.classList.add(p.score > p.prevScore ? 'score-up' : 'score-down');
    }

    tr.append(rankTd, nameTd, idTd, scoreTd);
    els.leaderboardBody.appendChild(tr);
  });
}

// ---- Health / status strip --------------------------------------

function setPillState(svc, state, title) {
  const pill = els.statusStrip.querySelector(`[data-svc="${svc}"]`);
  if (!pill) return;
  pill.classList.remove('is-ok', 'is-down', 'is-checking');
  pill.classList.add(`is-${state}`);
  if (title) pill.title = title;
}

async function refreshHealth() {
  els.refreshHealthBtn.classList.add('is-spinning');
  ['gateway', 'player', 'match'].forEach((svc) => setPillState(svc, 'checking', 'Checking…'));

  try {
    const data = await apiRequest('/readyz');
    setPillState('gateway', data.gateway === 'ok' ? 'ok' : 'down', data.gateway);

    const playerOk = typeof data.player_service === 'string' && data.player_service.startsWith('db=ok');
    setPillState('player', playerOk ? 'ok' : 'down', data.player_service);

    const matchOk = typeof data.match_service === 'string' && data.match_service.startsWith('db=ok');
    setPillState('match', matchOk ? 'ok' : 'down', data.match_service);
  } catch (err) {
    ['gateway', 'player', 'match'].forEach((svc) => setPillState(svc, 'down', err.message));
    logActivity(`Health check failed: ${err.message}`, 'error');
  } finally {
    setTimeout(() => els.refreshHealthBtn.classList.remove('is-spinning'), 400);
  }
}

// ---- Form: create player ------------------------------------------

els.formCreatePlayer.addEventListener('submit', async (e) => {
  e.preventDefault();
  const form = e.target;
  const data = new FormData(form);
  const username = data.get('username').trim();
  const starting_score = Number(data.get('starting_score')) || 0;

  if (!username) {
    setHint(els.createPlayerHint, 'Username is required.', 'error');
    return;
  }

  setButtonLoading(form, true);
  setHint(els.createPlayerHint, '');
  runTrace({ service: 'player-service', store: 'Postgres' });

  try {
    const player = await apiRequest('/api/players', {
      method: 'POST',
      body: JSON.stringify({ username, starting_score }),
    });
    setHint(els.createPlayerHint, `Created #${player.player_id} — ${player.username} (score ${player.score})`, 'success');
    logActivity(`Created player "${player.username}" (#${player.player_id}) with score ${player.score}`, 'ok');
    upsertTrackedPlayer(player, { highlight: true });
    form.reset();
    form.querySelector('input[name="starting_score"]').value = 1000;
  } catch (err) {
    setHint(els.createPlayerHint, err.message, 'error');
    logActivity(`Create player failed: ${err.message}`, 'error');
    runTrace({ service: 'player-service', store: 'Postgres', ok: false, durationMs: 500 });
  } finally {
    setButtonLoading(form, false);
  }
});

// ---- Form: create match --------------------------------------------

els.formCreateMatch.addEventListener('submit', async (e) => {
  e.preventDefault();
  const form = e.target;
  const data = new FormData(form);
  const playerA = Number(data.get('player_a'));
  const playerB = Number(data.get('player_b'));
  const game_mode = data.get('game_mode');

  if (!playerA || !playerB) {
    setHint(els.createMatchHint, 'Both player IDs are required.', 'error');
    return;
  }
  if (playerA === playerB) {
    setHint(els.createMatchHint, 'Player A and B must be different.', 'error');
    return;
  }

  setButtonLoading(form, true);
  setHint(els.createMatchHint, '');
  runTrace({ service: 'match-service', store: 'Postgres' });

  try {
    const match = await apiRequest('/api/matches', {
      method: 'POST',
      body: JSON.stringify({ player_ids: [playerA, playerB], game_mode }),
    });
    setHint(els.submitResultHint, '');
    setHint(els.createMatchHint, `Match #${match.match_id} created (${match.status})`, 'success');
    logActivity(`Started match #${match.match_id}: #${playerA} vs #${playerB} (${game_mode})`, 'ok');

    // Pre-fill the submit-result form for convenience.
    els.formSubmitResult.match_id.value = match.match_id;
    els.formSubmitResult.winner_id.value = playerA;
    els.formSubmitResult.loser_id.value = playerB;
  } catch (err) {
    setHint(els.createMatchHint, err.message, 'error');
    logActivity(`Create match failed: ${err.message}`, 'error');
    runTrace({ service: 'match-service', store: 'Postgres', ok: false, durationMs: 500 });
  } finally {
    setButtonLoading(form, false);
  }
});

// ---- Form: submit result -------------------------------------------

els.formSubmitResult.addEventListener('submit', async (e) => {
  e.preventDefault();
  const form = e.target;
  const data = new FormData(form);
  const matchId = Number(data.get('match_id'));
  const winnerId = Number(data.get('winner_id'));
  const loserId = Number(data.get('loser_id'));
  const winnerDelta = Number(data.get('winner_delta'));
  const loserDelta = Number(data.get('loser_delta'));

  if (!matchId || !winnerId || !loserId) {
    setHint(els.submitResultHint, 'Match ID, winner ID, and loser ID are required.', 'error');
    return;
  }

  setButtonLoading(form, true);
  setHint(els.submitResultHint, '');
  runTrace({ service: 'match-service → player-service', store: 'Postgres', durationMs: 1100 });

  try {
    const score_deltas = {
      [String(winnerId)]: winnerDelta,
      [String(loserId)]: loserDelta,
    };

    const result = await apiRequest(`/api/matches/${matchId}/result`, {
      method: 'POST',
      body: JSON.stringify({ winner_player_id: winnerId, score_deltas }),
    });

    if (result.error) {
      setHint(els.submitResultHint, result.error, 'error');
      logActivity(`Match #${matchId} result recorded with warning: ${result.error}`, 'error');
    } else {
      setHint(els.submitResultHint, `Match #${matchId} completed — winner #${winnerId}`, 'success');
      logActivity(`Match #${matchId} completed — winner #${winnerId}`, 'ok');
    }

    // Refresh both players' scores from the player service.
    await Promise.all(
      [winnerId, loserId].map(async (id) => {
        try {
          const player = await apiRequest(`/api/players/${id}`);
          upsertTrackedPlayer(player, { highlight: true });
        } catch (_) {
          // player may not exist locally; ignore
        }
      })
    );
  } catch (err) {
    setHint(els.submitResultHint, err.message, 'error');
    logActivity(`Submit result failed: ${err.message}`, 'error');
    runTrace({ service: 'match-service → player-service', store: 'Postgres', ok: false, durationMs: 500 });
  } finally {
    setButtonLoading(form, false);
  }
});

// ---- Form: player lookup --------------------------------------------

els.formLookup.addEventListener('submit', async (e) => {
  e.preventDefault();
  const form = e.target;
  const data = new FormData(form);
  const playerId = Number(data.get('player_id'));

  if (!playerId) return;

  setButtonLoading(form, true);
  runTrace({ service: 'player-service', store: 'Redis → Postgres', durationMs: 700 });

  try {
    const player = await apiRequest(`/api/players/${playerId}`);
    els.lookupName.textContent = player.username;
    els.lookupId.textContent = `#${player.player_id}`;
    els.lookupScore.textContent = player.score;
    els.lookupResult.hidden = false;
    logActivity(`Looked up #${player.player_id} — ${player.username} (score ${player.score})`, 'info');
    upsertTrackedPlayer(player, { highlight: true });
  } catch (err) {
    els.lookupResult.hidden = true;
    logActivity(`Lookup #${playerId} failed: ${err.message}`, 'error');
    runTrace({ service: 'player-service', store: 'Redis → Postgres', ok: false, durationMs: 500 });
  } finally {
    setButtonLoading(form, false);
  }
});

// ---- Manual refresh button -------------------------------------------

els.refreshHealthBtn.addEventListener('click', () => refreshHealth());

// ---- Init -------------------------------------------------------------

refreshHealth();
setInterval(refreshHealth, 15000);
