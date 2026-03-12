#include "server/Connection.hpp"
#include <unistd.h>
#include <utility>
#include <iostream>
#include <cstring>

Connection::Connection(int fd, const struct sockaddr_in &client_addr)
    : fd_(fd),
      keep_alive_(false),
      client_addr_(client_addr),
      state_(ConnectionState::NEW),
      request_(std::make_unique<Request>()),
      response_(std::make_unique<Response>()),
      continue_sent_(false)
{
    updateActiveTime();
}

Connection::~Connection()
{
    close();
}

// 移动构造：把 fd 从 other 中“拿走”
Connection::Connection(Connection &&other) noexcept
    : fd_(other.fd_.exchange(-1)),
      keep_alive_(other.keep_alive_.load(std::memory_order_relaxed)),
      client_addr_(other.client_addr_),
      state_(other.state_.load(std::memory_order_relaxed)),
      recv_buf_(),
      send_buf_(),
      request_(std::move(other.request_)),
      response_(std::move(other.response_)),
      last_active_()
{
    {
        std::lock_guard<std::mutex> lg(other.recv_mutex_);
        recv_buf_ = std::move(other.recv_buf_);
    }
    {
        std::lock_guard<std::mutex> lg(other.send_mutex_);
        send_buf_ = std::move(other.send_buf_);
    }
    {
        std::lock_guard<std::mutex> lg(other.last_active_mutex_);
        last_active_ = other.last_active_;
    }
}

// 移动赋值
Connection &Connection::operator=(Connection &&other) noexcept
{
    if (this != &other)
    {
        close();

        fd_.store(other.fd_.exchange(-1));
        keep_alive_.store(other.keep_alive_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        client_addr_ = other.client_addr_;
        state_.store(other.state_.load(std::memory_order_relaxed), std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lg(other.recv_mutex_);
            std::lock_guard<std::mutex> lg2(recv_mutex_);
            recv_buf_ = std::move(other.recv_buf_);
        }
        {
            std::lock_guard<std::mutex> lg(other.send_mutex_);
            std::lock_guard<std::mutex> lg2(send_mutex_);
            send_buf_ = std::move(other.send_buf_);
        }
        {
            std::lock_guard<std::mutex> lg(other.last_active_mutex_);
            std::lock_guard<std::mutex> lg2(last_active_mutex_);
            last_active_ = other.last_active_;
        }

        request_ = std::move(other.request_);
        response_ = std::move(other.response_);
    }
    return *this;
}

// recv buffer
void Connection::append_recv(const char *data, size_t len)
{
    std::lock_guard<std::mutex> lg(recv_mutex_);
    recv_buf_.append(data, len);
    updateActiveTime();
}

// 返回副本（线程安全）
std::string Connection::recv_buf_copy() const
{
    std::lock_guard<std::mutex> lg(recv_mutex_);
    return recv_buf_;
}

void Connection::clear_recv_buf()
{
    std::lock_guard<std::mutex> lg(recv_mutex_);
    recv_buf_.clear();
}

// send buffer
void Connection::append_send(const char *data, size_t len)
{
    std::lock_guard<std::mutex> lg(send_mutex_);
    send_buf_.append(data, len);
    updateActiveTime();
}

ssize_t Connection::safeSend(const char *data, size_t len)
{
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (fd_ == -1)
        return -1;
    ssize_t n = ::send(fd_, data, len, 0);
    if (n < 0 && (errno == EPIPE || errno == ECONNRESET))
    {
        close();
        return -1;
    }
    return n;
}

std::string Connection::send_buf_copy() const
{
    std::lock_guard<std::mutex> lg(send_mutex_);
    return send_buf_;
}

void Connection::clear_send_buf()
{
    std::lock_guard<std::mutex> lg(send_mutex_);
    send_buf_.clear();
}

void Connection::erase_sent(size_t len)
{
    std::lock_guard<std::mutex> lg(send_mutex_);
    if (len >= send_buf_.size())
        send_buf_.clear();
    else
        send_buf_.erase(0, len);
}

void Connection::resetRequestResponse()
{
    request_ = std::make_unique<Request>();
    response_ = std::make_unique<Response>();
}

// 活跃时间（线程安全）
void Connection::updateActiveTime()
{
    std::lock_guard<std::mutex> lg(last_active_mutex_);
    last_active_ = std::chrono::steady_clock::now();
}

bool Connection::isTimeout(std::chrono::steady_clock::time_point now, int timeout_sec) const
{
    std::lock_guard<std::mutex> lg(last_active_mutex_);
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_active_);
    return duration.count() > timeout_sec;
}

bool Connection::isTimeout(int timeout_sec) const
{
    return isTimeout(std::chrono::steady_clock::now(), timeout_sec);
}

// close()：保证只 close 一次（atomic CAS）
void Connection::close()
{
    int expected = fd_.load(std::memory_order_relaxed);
    // 如果 expected == -1，说明已经关闭或未被初始化
    while (expected != -1)
    {
        if (fd_.compare_exchange_strong(expected, -1))
        {
            ::close(expected);
            setState(ConnectionState::CLOSED);
            return;
        }
        // compare_exchange 更新 expected 到当前值，若为 -1 则 loop ends
    }
}

// 添加连接重置方法，用于 Keep-Alive
void Connection::resetForKeepAlive()
{
    std::lock_guard<std::mutex> recv_lock(recv_mutex_);
    std::lock_guard<std::mutex> send_lock(send_mutex_);

    recv_buf_.clear();
    send_buf_.clear();
    if (request_)
        request_->reset();
    if (response_)
        response_->reset();
    setState(ConnectionState::READING);
    updateActiveTime();

    std::cout << "[FD=" << getFd() << "] 重置后状态：multipart_state=" << static_cast<int>(request_->getMultipartState())
              << ", route_checked=" << request_->isRouteChecked() << std::endl;
}
