#include "player_service_impl.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

std::string getEnvOrDefault(const char* name, const std::string& def) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : def;
}

} // namespace

int main(int argc, char** argv) {
    std::string grpc_port = getEnvOrDefault("GRPC_PORT", "50051");
    std::string pg_host = getEnvOrDefault("POSTGRES_HOST", "localhost");
    std::string pg_port = getEnvOrDefault("POSTGRES_PORT", "5432");
    std::string pg_db = getEnvOrDefault("POSTGRES_DB", "gamedb");
    std::string pg_user = getEnvOrDefault("POSTGRES_USER", "gameuser");
    std::string pg_password = getEnvOrDefault("POSTGRES_PASSWORD", "gamepass");
    std::string redis_host = getEnvOrDefault("REDIS_HOST", "localhost");
    int redis_port = std::stoi(getEnvOrDefault("REDIS_PORT", "6379"));
    int pool_size = std::stoi(getEnvOrDefault("DB_POOL_SIZE", "8"));

    std::string conninfo = "host=" + pg_host + " port=" + pg_port +
                            " dbname=" + pg_db + " user=" + pg_user +
                            " password=" + pg_password;

    std::cout << "[player_service] connecting to Postgres at " << pg_host
              << ":" << pg_port << " and Redis at " << redis_host << ":"
              << redis_port << std::endl;

    auto db_pool = std::make_shared<DbPool>(conninfo, pool_size);
    db_pool->ensureSchema(R"sql(
        CREATE TABLE IF NOT EXISTS players (
            player_id BIGSERIAL PRIMARY KEY,
            username  TEXT NOT NULL UNIQUE,
            score     INTEGER NOT NULL DEFAULT 0,
            updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
        );
        CREATE INDEX IF NOT EXISTS idx_players_score ON players (score DESC);
    )sql");

    auto cache = std::make_shared<CacheClient>(redis_host, redis_port);

    PlayerServiceImpl service(db_pool, cache);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    std::string server_address = "0.0.0.0:" + grpc_port;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "[player_service] listening on " << server_address << std::endl;
    server->Wait();

    return 0;
}
