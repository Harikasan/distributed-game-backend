#include "db_pool.h"
#include <iostream>

DbPool::DbPool(const std::string& conninfo, size_t pool_size)
    : conninfo_(conninfo) {
    for (size_t i = 0; i < pool_size; ++i) {
        try {
            auto conn = std::make_shared<pqxx::connection>(conninfo_);
            pool_.push(conn);
        } catch (const std::exception& e) {
            std::cerr << "[DbPool] failed to open connection " << i
                      << ": " << e.what() << std::endl;
        }
    }
}

void DbPool::ensureSchema(const std::string& ddl) {
    auto guard = acquire();
    pqxx::work txn(guard.get());
    txn.exec(ddl);
    txn.commit();
}

DbPool::ConnectionGuard DbPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (pool_.empty()) {
        cv_.wait(lock);
    }
    auto conn = pool_.front();
    pool_.pop();
    return ConnectionGuard(this, conn);
}

void DbPool::release(std::shared_ptr<pqxx::connection> conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cv_.notify_one();
}

DbPool::ConnectionGuard::ConnectionGuard(DbPool* pool, std::shared_ptr<pqxx::connection> conn)
    : pool_(pool), conn_(std::move(conn)) {}

DbPool::ConnectionGuard::~ConnectionGuard() {
    if (pool_ && conn_) {
        pool_->release(conn_);
    }
}

pqxx::connection& DbPool::ConnectionGuard::get() {
    return *conn_;
}
