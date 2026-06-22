#!/usr/bin/env bash
# End-to-end smoke test against a running gateway (default: localhost:8080).
# Exercises the full flow: create players -> create match -> submit result
# -> verify updated scores. Useful for validating a fresh docker-compose
# or Kubernetes deployment.

set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8080}"
echo "Running smoke test against $BASE_URL"

req() {
  local method="$1" path="$2" data="${3:-}"
  if [[ -n "$data" ]]; then
    curl -sS -X "$method" "$BASE_URL$path" -H "Content-Type: application/json" -d "$data"
  else
    curl -sS -X "$method" "$BASE_URL$path"
  fi
}

echo "1. Health check..."
req GET /readyz | tee /tmp/ready.json
echo

echo "2. Creating player A..."
PLAYER_A=$(req POST /api/players '{"username":"alice_'"$RANDOM"'","starting_score":1000}')
echo "$PLAYER_A"
PLAYER_A_ID=$(echo "$PLAYER_A" | grep -o '"player_id":"\?[0-9]*' | grep -o '[0-9]*')

echo "3. Creating player B..."
PLAYER_B=$(req POST /api/players '{"username":"bob_'"$RANDOM"'","starting_score":1000}')
echo "$PLAYER_B"
PLAYER_B_ID=$(echo "$PLAYER_B" | grep -o '"player_id":"\?[0-9]*' | grep -o '[0-9]*')

echo "Player A ID: $PLAYER_A_ID, Player B ID: $PLAYER_B_ID"

echo "4. Creating a match between A and B..."
MATCH=$(req POST /api/matches '{"player_ids":['"$PLAYER_A_ID"','"$PLAYER_B_ID"'],"game_mode":"ranked_1v1"}')
echo "$MATCH"
MATCH_ID=$(echo "$MATCH" | grep -o '"match_id":"\?[0-9]*' | grep -o '[0-9]*')

echo "5. Submitting match result (A wins, +25/-15)..."
RESULT=$(req POST "/api/matches/$MATCH_ID/result" '{"winner_player_id":'"$PLAYER_A_ID"',"score_deltas":{"'"$PLAYER_A_ID"'":25,"'"$PLAYER_B_ID"'":-15}}')
echo "$RESULT"

echo "6. Verifying player A score increased..."
req GET "/api/players/$PLAYER_A_ID"
echo

echo "7. Verifying player B score decreased..."
req GET "/api/players/$PLAYER_B_ID"
echo

echo "Smoke test completed."
