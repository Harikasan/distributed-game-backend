#pragma once

#include "player.grpc.pb.h"
#include "db_pool.h"
#include "cache_client.h"
#include <memory>

class PlayerServiceImpl final : public game::player::PlayerService::Service {
public:
    PlayerServiceImpl(std::shared_ptr<DbPool> db_pool,
                       std::shared_ptr<CacheClient> cache);

    grpc::Status GetPlayer(grpc::ServerContext* context,
                            const game::player::GetPlayerRequest* request,
                            game::player::PlayerResponse* response) override;

    grpc::Status CreatePlayer(grpc::ServerContext* context,
                               const game::player::CreatePlayerRequest* request,
                               game::player::PlayerResponse* response) override;

    grpc::Status UpdatePlayerScore(grpc::ServerContext* context,
                                    const game::player::UpdateScoreRequest* request,
                                    game::player::PlayerResponse* response) override;

    grpc::Status HealthCheck(grpc::ServerContext* context,
                              const game::player::HealthCheckRequest* request,
                              game::player::HealthCheckResponse* response) override;

private:
    // Runs a DB operation with retry + exponential backoff to tolerate
    // transient connection failures (part of fault-tolerance design).
    template <typename Fn>
    auto withRetry(Fn fn, int max_attempts = 3) -> decltype(fn());

    std::string cacheKeyFor(int64_t player_id) const;

    std::shared_ptr<DbPool> db_pool_;
    std::shared_ptr<CacheClient> cache_;
};
