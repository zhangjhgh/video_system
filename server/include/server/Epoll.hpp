#ifndef EPOLL_HPP
#define EPOLL_HPP

#include <sys/epoll.h>
#include <vector>

class Epoll
{
public:
    Epoll();
    ~Epoll();
    // 禁用拷贝构造和赋值
    Epoll(const Epoll &) = delete;
    Epoll &operator=(const Epoll &) = delete;

    bool create();                                                   // 创建epoll实例
    bool addFd(int fd, uint32_t events);                             // 添加要监控的文件描述符
    bool modFd(int fd, uint32_t events);                             // 修改要监控的文件描述符
    bool delFd(int fd);                                              // 删除要监控的文件描述符
    std::vector<epoll_event> wait(int max_events, int timeout = -1); // 等待事件发生
    void close();
    int getFd() const
    {
        return epoll_fd_;
    }

private:
    int epoll_fd_; // epoll文件描述符
};

#endif