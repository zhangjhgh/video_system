#include "server/ConnectionManager.hpp"
#include <unistd.h>
#include <iostream>

ConnectionManager::ConnectionManager()
{
    std::cout << "[ConnectionManager] created.\n";
}

ConnectionManager::~ConnectionManager()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    std::cout << "[ConnectionManager] destroyed, active connections=" << connections_.size() << "\n";
    connections_.clear(); // 自动销毁所有连接
}

void ConnectionManager::add(int fd, struct sockaddr_in &client_addr)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    connections_[fd] = std::make_shared<Connection>(fd, client_addr);
    connections_[fd]->setState(ConnectionState::READING);
}

// 修改 remove 方法，返回被移除的 shared_ptr
std::shared_ptr<Connection> ConnectionManager::remove(int fd)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = connections_.find(fd);
    if (it != connections_.end())
    {
        auto conn = it->second;
        connections_.erase(it);
        return conn; // 返回 shared_ptr，延长生命周期用于清理
    }
    return nullptr;
}

// 添加安全获取方法，在获取时检查连接是否即将被移除
std::shared_ptr<Connection> ConnectionManager::get(int fd)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = connections_.find(fd);
    if (it != connections_.end() && it->second->isValid())
    {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Connection>> ConnectionManager::clear_timeout_connections(int normal_timeout, int processing_timeout)
{
    std::vector<std::shared_ptr<Connection>> close_conns;
    std::unique_lock<std::shared_mutex> lock(mutex_);

    for (auto it = connections_.begin(); it != connections_.end();)
    {
        // 1. 先检查连接指针是否有效（避免空指针访问）
        if (!it->second)
        {
            it = connections_.erase(it); // 清理空指针，迭代器指向 next
            continue;                    // 跳过后续逻辑，不执行 ++it
        }

        // 2. 获取状态（此时 it->second 一定非空，可安全访问）
        ConnectionState state = it->second->getState();
        bool is_timeout = false;

        // 3. 根据状态判断超时
        if (state == ConnectionState::PROCESSING)
        {
            is_timeout = it->second->isTimeout(processing_timeout);
        }
        else
        {
            is_timeout = it->second->isTimeout(normal_timeout);
        }

        // 4. 处理超时连接
        if (is_timeout)
        {
            close_conns.push_back(it->second); // 保存连接
            it = connections_.erase(it);       // 迭代器指向 next
        }
        else
        {
            ++it; // 未超时，手动递增迭代器
        }
    }
    return close_conns;
}

size_t ConnectionManager::size()
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return connections_.size();
}

void ConnectionManager::clear()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    connections_.clear(); // 自动销毁所有连接
}