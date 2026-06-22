#include "player_service_impl.h"
#include <chrono>
#include <thread>
#include <sstream>
#include <iostream>

PlayerServiceImpl::PlayerServiceImpl(std::shared_ptr<DbPool> db_pool,
                                       std::shared_ptr<CacheClient> cache)
    : db_pool_(std::move(db_pool)), cache_(std::move(cache)) {}

std::string PlayerServiceImpl::cacheKeyFor(int64_t player_id) const {
    return "player:" + std::to_string(player_id);
}

template <typename Fn>
auto PlayerServiceImpl::withRetry(Fn fn, int max_attempts) -> decltype(fn()) {
    int attempt = 0;
    int backoff_ms = 20;
    while (true) {
        try {
            return fn();
        } catch (const std::exception& e) {
            attempt++;
            if (attempt >= max_attempts) {
                throw;
            }
            std::cerr << "[PlayerService] transient error (attempt " << attempt
                      << "/" << max_attempts << "): " << e.what()
                      << " — retrying in " << backoff_ms << "ms" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms *= 2; // exponential backoff
        }
    }
}

grpc::Status PlayerServiceImpl::GetPlayer(grpc::ServerContext* context,
                                           const game::player::GetPlayerRequest* request,
                                           game::player::PlayerResponse* response) {
    const int64_t player_id = request->player_id();
    const std::string key = cacheKeyFor(player_id);

    // 1. Try Redis cache first (cache-aside pattern).
    if (auto cached = cache_->get(key)) {
        std::istringstream iss(*cached);
        std::string username;
        int32_t score = 0;
        if (std::getline(iss, username, '|') && (iss >> score)) {
            response->set_player_id(player_id);
            response->set_username(username);
            response->set_score(score);
            response->set_found(true);
            return grpc::Status::OK;
        }
    }

    // 2. Cache miss: fall back to Postgres, with retry on transient errors.
    try {
        bool found = withRetry([&]() {
            auto guard = db_pool_->acquire();
            pqxx::work txn(guard.get());
            pqxx::result r = txn.exec_params(
                "SELECT username, score FROM players WHERE player_id = $1",
                player_id);
            txn.commit();

            if (r.empty()) {
                return false;
            }
            response->set_player_id(player_id);
            response->set_username(r[0]["username"].as<std::string>());
            response->set_score(r[0]["score"].as<int32_t>());
            response->set_found(true);

            // Populate cache for subsequent reads (60s TTL).
            std::string cache_val = r[0]["username"].as<std::string>() + "|" +
                                     std::to_string(r[0]["score"].as<int32_t>());
            cache_->set(key, cache_val, 60);
            return true;
        });

        if (!found) {
            response->set_found(false);
            response->set_error("player not found");
        }
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_found(false);
        response->set_error(std::string("internal error: ") + e.what());
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, e.what());
    }
}

grpc::Status PlayerServiceImpl::CreatePlayer(grpc::ServerContext* context,
                                              const game::player::CreatePlayerRequest* request,
                                              game::player::PlayerResponse* response) {
    try {
        withRetry([&]() {
            auto guard = db_pool_->acquire();
            pqxx::work txn(guard.get());
            pqxx::result r = txn.exec_params(
                "INSERT INTO players (username, score) VALUES ($1, $2) "
                "RETURNING player_id",
                request->username(), request->starting_score());
            txn.commit();

            int64_t new_id = r[0]["player_id"].as<int64_t>();
            response->set_player_id(new_id);
            response->set_username(request->username());
            response->set_score(request->starting_score());
            response->set_found(true);
            return true;
        });
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_found(false);
        response->set_error(std::string("create failed: ") + e.what());
        return grpc::Status(grpc::StatusCode::ALREADY_EXISTS, e.what());
    }
}

grpc::Status PlayerServiceImpl::UpdatePlayerScore(grpc::ServerContext* context,
                                                   const game::player::UpdateScoreRequest* request,
                                                   game::player::PlayerResponse* response) {
    const int64_t player_id = request->player_id();
    try {
        bool found = withRetry([&]() {
            auto guard = db_pool_->acquire();
            pqxx::work txn(guard.get());
            // Atomic update guards against lost updates under concurrent
            // writes (multiple match results landing simultaneously).
            pqxx::result r = txn.exec_params(
                "UPDATE players SET score = score + $1, updated_at = now() "
                "WHERE player_id = $2 RETURNING username, score",
                request->score_delta(), player_id);
            txn.commit();

            if (r.empty()) {
                return false;
            }
            response->set_player_id(player_id);
            response->set_username(r[0]["username"].as<std::string>());
            response->set_score(r[0]["score"].as<int32_t>());
            response->set_found(true);
            return true;
        });

        if (!found) {
            response->set_found(false);
            response->set_error("player not found");
            return grpc::Status::OK;
        }

        // Invalidate cache so subsequent reads see the fresh score.
        cache_->invalidate(cacheKeyFor(player_id));
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_found(false);
        response->set_error(std::string("update failed: ") + e.what());
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, e.what());
    }
}

grpc::Status PlayerServiceImpl::HealthCheck(grpc::ServerContext* context,
                                             const game::player::HealthCheckRequest* request,
                                             game::player::HealthCheckResponse* response) {
    bool db_ok = false;
    try {
        auto guard = db_pool_->acquire();
        pqxx::work txn(guard.get());
        txn.exec("SELECT 1");
        txn.commit();
        db_ok = true;
    } catch (...) {
        db_ok = false;
    }

    bool cache_ok = cache_->isConnected();

    response->set_healthy(db_ok); // service is usable as long as DB is up
    std::ostringstream msg;
    msg << "db=" << (db_ok ? "ok" : "down")
        << " cache=" << (cache_ok ? "ok" : "down (degraded)");
    response->set_status_message(msg.str());
    return grpc::Status::OK;
}
