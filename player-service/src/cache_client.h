#pragma once

#include <hiredis/hiredis.h>
#include <mutex>
#include <optional>
#include <string>

// Thin wrapper around hiredis providing a simple get/set/invalidate
// interface used to cache player lookups and reduce read load on Postgres.
class CacheClient {
public:
    CacheClient(const std::string& host, int port);
    ~CacheClient();

    // Returns std::nullopt on cache miss or connection error.
    std::optional<std::string> get(const std::string& key);

    // TTL in seconds.
    void set(const std::string& key, const std::string& value, int ttl_seconds);

    void invalidate(const std::string& key);

    bool isConnected() const;

private:
    void reconnectIfNeeded();

    std::string host_;
    int port_;
    redisContext* ctx_;
    std::mutex mutex_;
};
