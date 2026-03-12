#ifndef CONNECTION_HPP
#define CONNECTION_HPP
#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <string>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include "server/Request.hpp"
#include "server/Response.hpp"

enum class ConnectionState
{
    NEW,
    READING,
    PROCESSING,
    WRITING,
    CLOSING,
    CLOSED
};

class Request;
class Response;
class Connection
{
public:
    Connection() = default;
    Connection(int fd, const struct sockaddr_in &client_addr);
    ~Connection();

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    // 移动构造/赋值
    Connection(Connection &&) noexcept;
    Connection &operator=(Connection &&) noexcept;

    // 基本信息
    int getFd() const { return fd_.load(std::memory_order_relaxed); }
    const struct sockaddr_in &getClientAddr() const { return client_addr_; }

    // 状态（atomic）
    ConnectionState getState() const { return state_.load(std::memory_order_relaxed); }
    void setState(ConnectionState s) { state_.store(s, std::memory_order_relaxed); }

    // keep-alive（atomic）
    void setKeepAlive(bool v) { keep_alive_.store(v, std::memory_order_relaxed); }
    bool getKeepAlive() const { return keep_alive_.load(std::memory_order_relaxed); }
    void resetForKeepAlive();

    // 缓冲区操作（线程安全）
    void append_recv(const char *data, size_t len);
    std::string recv_buf_copy() const; // 返回副本（线程安全）
    void clear_recv_buf();

    void append_send(const char *data, size_t len);
    ssize_t safeSend(const char *data, size_t len);
    std::string send_buf_copy() const; // 返回副本（线程安全）
    void clear_send_buf();
    void erase_sent(size_t len);

    // 请求/响应对象访问（非线程安全指针访问，仅供工作线程使用）
    Request *request() const { return request_.get(); }
    Response *response() const { return response_.get(); }
    void resetRequestResponse();

    // 活跃时间（线程安全）
    void updateActiveTime();
    bool isTimeout(std::chrono::steady_clock::time_point now, int timeout_sec) const;
    bool isTimeout(int timeout_sec) const;

    // fd 有效性
    bool isValid() const { return getFd() != -1; }

    // 关闭 fd（线程安全，保证只 close 一次）
    void close();

    // 添加状态验证
    bool canProcess() const
    {
        return isValid() && getState() == ConnectionState::READING;
    }

    bool canWrite() const
    {
        return isValid() && getState() == ConnectionState::WRITING;
    }
    bool is100ContinueSent() const { return continue_sent_; }
    void set100ContinueSent(bool sent) { continue_sent_ = sent; }

    void markForCloseAfterWrite()
    {
        close_after_write_ = true;
    }

    bool closeAfterWrite() const { return close_after_write_; }

    // 每次发送成功后，累加全局发送量
    void addTotalSent(size_t bytes)
    {
        total_sent_global_ += bytes;
    }
    // 获取全局累计发送量
    size_t getTotalSent() const
    {
        return total_sent_global_;
    }

    // 重置累计发送量（用于新请求）
    void resetTotalSent()
    {
        total_sent_global_ = 0;
    }

private:
    std::atomic<int> fd_{-1}; // 使用 atomic 防并发读写
    std::atomic<bool> keep_alive_{false};
    struct sockaddr_in client_addr_;
    std::atomic<ConnectionState> state_{ConnectionState::NEW};

    // buffers + mutexes
    mutable std::mutex recv_mutex_;
    std::string recv_buf_;

    mutable std::mutex send_mutex_;
    std::string send_buf_;

    // 请求/响应对象，仅由工作线程使用
    std::unique_ptr<Request> request_;
    std::unique_ptr<Response> response_;

    // 活跃时间（受 mutex 保护）
    mutable std::mutex last_active_mutex_;
    std::chrono::steady_clock::time_point last_active_;

    bool continue_sent_;             // 标记是否已发送100 Continue
    bool close_after_write_ = false; // 标记是否写完关闭连接

    size_t total_sent_global_ = 0; // 全局累计发送量（跨多次发送事件）
};

#endif