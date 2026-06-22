#pragma once

#include "match.grpc.pb.h"
#include "db_pool.h"
#include "player_client.h"
#include <memory>

class MatchServiceImpl final : public game::match::MatchService::Service {
public:
    MatchServiceImpl(std::shared_ptr<DbPool> db_pool,
                      std::shared_ptr<PlayerClient> player_client);

    grpc::Status CreateMatch(grpc::ServerContext* context,
                              const game::match::CreateMatchRequest* request,
                              game::match::MatchResponse* response) override;

    grpc::Status GetMatch(grpc::ServerContext* context,
                          const game::match::GetMatchRequest* request,
                          game::match::MatchResponse* response) override;

    grpc::Status SubmitMatchResult(grpc::ServerContext* context,
                                    const game::match::SubmitResultRequest* request,
                                    game::match::MatchResponse* response) override;

    grpc::Status HealthCheck(grpc::ServerContext* context,
                              const game::match::HealthCheckRequest* request,
                              game::match::HealthCheckResponse* response) override;

private:
    std::shared_ptr<DbPool> db_pool_;
    std::shared_ptr<PlayerClient> player_client_;
};
