#include "cache_client.h"
#include <iostream>

CacheClient::CacheClient(const std::string& host, int port)
    : host_(host), port_(port), ctx_(nullptr) {
    reconnectIfNeeded();
}

CacheClient::~CacheClient() {
    if (ctx_) {
        redisFree(ctx_);
    }
}

void CacheClient::reconnectIfNeeded() {
    if (ctx_ && !ctx_->err) {
        return;
    }
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
    struct timeval timeout = {1, 0}; // 1 second connect timeout
    ctx_ = redisConnectWithTimeout(host_.c_str(), port_, timeout);
    if (ctx_ == nullptr || ctx_->err) {
        std::cerr << "[CacheClient] connection error: "
                  << (ctx_ ? ctx_->errstr : "allocation failed") << std::endl;
        if (ctx_) {
            redisFree(ctx_);
            ctx_ = nullptr;
        }
    }
}

bool CacheClient::isConnected() const {
    return ctx_ != nullptr && !ctx_->err;
}

std::optional<std::string> CacheClient::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    reconnectIfNeeded();
    if (!ctx_) return std::nullopt;

    redisReply* reply = (redisReply*)redisCommand(ctx_, "GET %s", key.c_str());
    if (!reply) {
        // Connection likely dropped; force reconnect next call.
        redisFree(ctx_);
        ctx_ = nullptr;
        return std::nullopt;
    }

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }
    freeReplyObject(reply);
    return result;
}

void CacheClient::set(const std::string& key, const std::string& value, int ttl_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    reconnectIfNeeded();
    if (!ctx_) return;

    redisReply* reply = (redisReply*)redisCommand(
        ctx_, "SET %s %s EX %d", key.c_str(), value.c_str(), ttl_seconds);
    if (reply) {
        freeReplyObject(reply);
    } else {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

void CacheClient::invalidate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    reconnectIfNeeded();
    if (!ctx_) return;

    redisReply* reply = (redisReply*)redisCommand(ctx_, "DEL %s", key.c_str());
    if (reply) {
        freeReplyObject(reply);
    } else {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}
