#ifndef CONNECTIONMANAGER_HPP
#define CONNECTIONMANAGER_HPP

#include <shared_mutex>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "server/Connection.hpp"

class ConnectionManager
{
public:
    ConnectionManager();
    ~ConnectionManager();
    // 添加新连接
    void add(int fd, struct sockaddr_in &client_addr);
    // 获取连接（返回指针，不存在返回 nullptr）
    std::shared_ptr<Connection> get(int fd);
    // 移除连接
    std::shared_ptr<Connection> remove(int fd);
    // 清理超时连接，返回被关闭的 fd 列表
    std::vector<std::shared_ptr<Connection>> clear_timeout_connections(int normal_timeout, int processing_timeout);
    // 获取当前连接总数
    size_t size();
    // 清理所有连接
    void clear();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
};

#endif
