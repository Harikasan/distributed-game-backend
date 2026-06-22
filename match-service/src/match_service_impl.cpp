#include "match_service_impl.h"
#include <sstream>
#include <iostream>

MatchServiceImpl::MatchServiceImpl(std::shared_ptr<DbPool> db_pool,
                                    std::shared_ptr<PlayerClient> player_client)
    : db_pool_(std::move(db_pool)), player_client_(std::move(player_client)) {}

grpc::Status MatchServiceImpl::CreateMatch(grpc::ServerContext* context,
                                            const game::match::CreateMatchRequest* request,
                                            game::match::MatchResponse* response) {
    try {
        auto guard = db_pool_->acquire();
        pqxx::work txn(guard.get());

        // Serialize player_ids as a comma-separated string for simplicity.
        std::ostringstream ids;
        for (int i = 0; i < request->player_ids_size(); ++i) {
            if (i > 0) ids << ",";
            ids << request->player_ids(i);
        }

        pqxx::result r = txn.exec_params(
            "INSERT INTO matches (player_ids, game_mode, status) "
            "VALUES ($1, $2, 'in_progress') RETURNING match_id",
            ids.str(), request->game_mode());
        txn.commit();

        int64_t match_id = r[0]["match_id"].as<int64_t>();
        response->set_match_id(match_id);
        for (int i = 0; i < request->player_ids_size(); ++i) {
            response->add_player_ids(request->player_ids(i));
        }
        response->set_game_mode(request->game_mode());
        response->set_status("in_progress");
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_error(std::string("create match failed: ") + e.what());
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, e.what());
    }
}

grpc::Status MatchServiceImpl::GetMatch(grpc::ServerContext* context,
                                         const game::match::GetMatchRequest* request,
                                         game::match::MatchResponse* response) {
    try {
        auto guard = db_pool_->acquire();
        pqxx::work txn(guard.get());
        pqxx::result r = txn.exec_params(
            "SELECT match_id, player_ids, game_mode, status, winner_player_id "
            "FROM matches WHERE match_id = $1",
            request->match_id());
        txn.commit();

        if (r.empty()) {
            response->set_error("match not found");
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "match not found");
        }

        response->set_match_id(r[0]["match_id"].as<int64_t>());
        response->set_game_mode(r[0]["game_mode"].as<std::string>());
        response->set_status(r[0]["status"].as<std::string>());
        if (!r[0]["winner_player_id"].is_null()) {
            response->set_winner_player_id(r[0]["winner_player_id"].as<int64_t>());
        }

        std::string ids_str = r[0]["player_ids"].as<std::string>();
        std::istringstream iss(ids_str);
        std::string token;
        while (std::getline(iss, token, ',')) {
            if (!token.empty()) {
                response->add_player_ids(std::stoll(token));
            }
        }
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_error(std::string("get match failed: ") + e.what());
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, e.what());
    }
}

grpc::Status MatchServiceImpl::SubmitMatchResult(grpc::ServerContext* context,
                                                  const game::match::SubmitResultRequest* request,
                                                  game::match::MatchResponse* response) {
    try {
        // 1. Mark the match complete in Postgres.
        {
            auto guard = db_pool_->acquire();
            pqxx::work txn(guard.get());
            pqxx::result r = txn.exec_params(
                "UPDATE matches SET status = 'completed', winner_player_id = $1 "
                "WHERE match_id = $2 RETURNING match_id, player_ids, game_mode",
                request->winner_player_id(), request->match_id());
            txn.commit();

            if (r.empty()) {
                response->set_error("match not found");
                return grpc::Status(grpc::StatusCode::NOT_FOUND, "match not found");
            }

            response->set_match_id(r[0]["match_id"].as<int64_t>());
            response->set_game_mode(r[0]["game_mode"].as<std::string>());
            response->set_status("completed");
            response->set_winner_player_id(request->winner_player_id());

            std::istringstream iss(r[0]["player_ids"].as<std::string>());
            std::string token;
            while (std::getline(iss, token, ',')) {
                if (!token.empty()) response->add_player_ids(std::stoll(token));
            }
        }

        // 2. Propagate score deltas to the Player Service via gRPC.
        //    If the Player Service is degraded, the match result remains
        //    recorded (consistent state) but score updates are skipped
        //    gracefully rather than failing the whole request.
        std::vector<std::string> failed_updates;
        for (const auto& kv : request->score_deltas()) {
            std::string err;
            if (!player_client_->updateScore(kv.first, kv.second, &err)) {
                failed_updates.push_back(std::to_string(kv.first) + ": " + err);
            }
        }

        if (!failed_updates.empty()) {
            std::ostringstream oss;
            oss << "match recorded, but " << failed_updates.size()
                << " score update(s) deferred (player service degraded)";
            response->set_error(oss.str());
            for (const auto& f : failed_updates) {
                std::cerr << "[MatchService] deferred score update — " << f << std::endl;
            }
        }

        return grpc::Status::OK;
    } catch (const std::exception& e) {
        response->set_error(std::string("submit result failed: ") + e.what());
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, e.what());
    }
}

grpc::Status MatchServiceImpl::HealthCheck(grpc::ServerContext* context,
                                            const game::match::HealthCheckRequest* request,
                                            game::match::HealthCheckResponse* response) {
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

    bool player_service_ok = player_client_->isAvailable();

    response->set_healthy(db_ok);
    std::ostringstream msg;
    msg << "db=" << (db_ok ? "ok" : "down")
        << " player_service=" << (player_service_ok ? "ok" : "circuit_open");
    response->set_status_message(msg.str());
    return grpc::Status::OK;
}
