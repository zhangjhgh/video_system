#include "server/Epoll.hpp"
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <cerrno>
#include <iostream>

Epoll::Epoll() : epoll_fd_(-1) {} // 初始为无效值
Epoll::~Epoll()
{
    if (epoll_fd_ >= 0)
    {
        close(); // 自动关闭
    }
}

bool Epoll::create() // 创建epoll实例
{
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0)
    {
        perror("epoll_create1 failed"); //  打印失败原因（关键！）
        std::cerr << "Error: epoll_create1 return " << epoll_fd_ << std::endl;
        return false;
    }
    std::cout << "Epoll created successfully, epfd = " << epoll_fd_ << std::endl;
    return true;
}

bool Epoll::addFd(int fd, uint32_t events) // 添加要监控的文件描述符
{

    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    std::cout << "[Epoll::addFd] Adding fd=" << fd << ", events=0x" << std::hex << events << std::dec << std::endl;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        perror("epoll_ctl add");
        return false;
    }
    std::cout << "[Epoll::addFd] Added fd=" << fd << " successfully" << std::endl;
    return true;
}

bool Epoll::modFd(int fd, uint32_t events) // 修改要监控的文件描述符
{
    // 先检查 fd 是否有效
    if (epoll_fd_ < 0)
    {
        std::cerr << "modFd failed: epoll not created (epfd = " << epoll_fd_ << ")" << std::endl;
        return false;
    }
    if (fcntl(fd, F_GETFL) == -1 && errno == EBADF)
    {
        std::cerr << "modFd: invalid fd " << fd << std::endl;
        return false;
    }
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
    {
        perror("epoll_ctl mod");
        return false;
    }
    std::cout << "[Epoll] modFd success: fd=" << fd << ", events=0x" << std::hex << events << std::dec << std::endl;
    return true;
}

bool Epoll::delFd(int fd) // 删除要监控的文件描述符
{
    if (epoll_fd_ < 0)
    {
        std::cerr << "delFd failed: epoll not created (epfd = " << epoll_fd_ << ")" << std::endl;
        return false;
    }

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0)
    {
        // 如果 fd 已被关闭，delFd 会失败，忽略这个错误
        if (errno != EBADF)
            perror("epoll_ctl del");
        return false;
    }
    return true;
}

std::vector<epoll_event> Epoll::wait(int max_events, int timeout) // 等待事件发生
{
    if (epoll_fd_ < 0)
    {
        std::cerr << "wait failed: epoll not created (epfd = " << epoll_fd_ << ")" << std::endl;
        return {};
    }

    std::vector<epoll_event> events(max_events); // 申请一个大容器
    int n = epoll_wait(epoll_fd_, events.data(), max_events, timeout);
    if (n < 0)
    {
        perror("epoll_wait failed");
        if (errno == EBADF)
        {
            std::cerr << "Recreate epoll instance" << std::endl;
            close();
            epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
            if (epoll_fd_ < 0)
            {
                perror("epoll_create1 failed");
                exit(EXIT_FAILURE);
            }
        }
        return {};
    }
    events.resize(n); // 裁剪容器
    return events;
}

void Epoll::close()
{
    if (epoll_fd_ >= 0)
    {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
}
