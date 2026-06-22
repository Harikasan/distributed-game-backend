#include "player_client.h"
#include <thread>
#include <iostream>

PlayerClient::PlayerClient(const std::string& target) {
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    stub_ = game::player::PlayerService::NewStub(channel);
}

bool PlayerClient::circuitOpen() const {
    std::lock_guard<std::mutex> lock(breaker_mutex_);
    if (consecutive_failures_ < kFailureThreshold) {
        return false;
    }
    return std::chrono::steady_clock::now() < open_until_;
}

void PlayerClient::recordFailure() {
    std::lock_guard<std::mutex> lock(breaker_mutex_);
    consecutive_failures_++;
    if (consecutive_failures_ >= kFailureThreshold) {
        open_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(kCooldownSeconds);
        std::cerr << "[PlayerClient] circuit breaker OPEN for " << kCooldownSeconds
                  << "s after " << consecutive_failures_ << " consecutive failures" << std::endl;
    }
}

void PlayerClient::recordSuccess() {
    std::lock_guard<std::mutex> lock(breaker_mutex_);
    if (consecutive_failures_ > 0) {
        std::cerr << "[PlayerClient] circuit breaker CLOSED (recovered)" << std::endl;
    }
    consecutive_failures_ = 0;
}

bool PlayerClient::isAvailable() const {
    return !circuitOpen();
}

bool PlayerClient::updateScore(int64_t player_id, int32_t score_delta, std::string* err) {
    if (circuitOpen()) {
        if (err) *err = "player service circuit breaker open; degrading gracefully";
        return false;
    }

    game::player::UpdateScoreRequest request;
    request.set_player_id(player_id);
    request.set_score_delta(score_delta);

    const int max_attempts = 3;
    int backoff_ms = 25;

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        game::player::PlayerResponse response;
        grpc::ClientContext context;
        // Deadline guards against a hung downstream call blocking this
        // thread indefinitely (part of fault-tolerance design).
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(500));

        grpc::Status status = stub_->UpdatePlayerScore(&context, request, &response);

        if (status.ok()) {
            if (!response.found()) {
                recordSuccess(); // service is healthy, just no such player
                if (err) *err = response.error();
                return false;
            }
            recordSuccess();
            return true;
        }

        if (status.error_code() == grpc::StatusCode::UNAVAILABLE ||
            status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            if (attempt < max_attempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                backoff_ms *= 2;
                continue;
            }
        }

        recordFailure();
        if (err) *err = status.error_message();
        return false;
    }

    recordFailure();
    if (err) *err = "exhausted retries";
    return false;
}
