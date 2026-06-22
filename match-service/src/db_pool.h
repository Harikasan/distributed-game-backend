#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <pqxx/pqxx>

// Simple thread-safe connection pool for PostgreSQL using libpqxx.
// Keeps a fixed number of live connections and hands them out/returns them,
// blocking briefly under contention instead of opening a new connection
// per request (which is the dominant latency cost under concurrent load).
class DbPool {
public:
    DbPool(const std::string& conninfo, size_t pool_size);

    // RAII wrapper that returns the connection to the pool on destruction.
    class ConnectionGuard {
    public:
        ConnectionGuard(DbPool* pool, std::shared_ptr<pqxx::connection> conn);
        ~ConnectionGuard();

        pqxx::connection& get();

        ConnectionGuard(const ConnectionGuard&) = delete;
        ConnectionGuard& operator=(const ConnectionGuard&) = delete;
        ConnectionGuard(ConnectionGuard&&) = default;

    private:
        DbPool* pool_;
        std::shared_ptr<pqxx::connection> conn_;
    };

    ConnectionGuard acquire();
    void ensureSchema(const std::string& ddl);

private:
    void release(std::shared_ptr<pqxx::connection> conn);

    std::string conninfo_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::shared_ptr<pqxx::connection>> pool_;
};
