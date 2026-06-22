#pragma once

#include "player.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

// gRPC client used by the Match Service to call the Player Service.
// Implements:
//  - retry with backoff on UNAVAILABLE (transient failures)
//  - a lightweight circuit breaker: after repeated failures, short-circuits
//    calls for a cooldown window instead of hammering a downed dependency
//  - graceful degradation: callers can check isAvailable() and skip
//    non-critical score updates if the breaker is open
class PlayerClient {
public:
    explicit PlayerClient(const std::string& target);

    // Applies a score delta to a player. Returns true on success.
    bool updateScore(int64_t player_id, int32_t score_delta, std::string* err);

    // Whether the circuit breaker currently allows calls through.
    bool isAvailable() const;

private:
    bool circuitOpen() const;
    void recordFailure();
    void recordSuccess();

    std::unique_ptr<game::player::PlayerService::Stub> stub_;

    mutable std::mutex breaker_mutex_;
    int consecutive_failures_ = 0;
    std::chrono::steady_clock::time_point open_until_;

    static constexpr int kFailureThreshold = 5;
    static constexpr int kCooldownSeconds = 10;
};
