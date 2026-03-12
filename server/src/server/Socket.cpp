#include "server/Socket.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <stdexcept>
#include <netinet/in.h>
#include <iostream>
#include <arpa/inet.h>

Socket::Socket() : sock_fd_(-1) {} // 初始化为无效值
Socket::~Socket()
{
    close();
}

bool Socket::create()
{
    // 创建IPv4 TCP socket
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0)
    {
        return false; // 创建失败
    }

    // 设置SO_REUSEADDR选项，避免重启服务器时地址被占用
    int optval = 1;
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
    {
        close(); // 设置失败，清理资源
        return false;
    }
    std::cout << "sock_fd_:" << sock_fd_ << std::endl;
    return true;
}

bool Socket::bind(int port)
{
    if (sock_fd_ < 0)
    {
        return false; // socket未创建
    }

    // 初始化服务器地址结构
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));    // 清零
    server_addr.sin_family = AF_INET;                // IPv4
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网络接口
    server_addr.sin_port = htons(port);              // 端口号，转换为网络字节序

    // 打印 server_addr 内容
    std::cout << "Server address info:" << std::endl;
    std::cout << "  sin_family: " << server_addr.sin_family << std::endl;
    std::cout << "  IP: " << inet_ntoa(server_addr.sin_addr) << std::endl;
    std::cout << "  Port: " << ntohs(server_addr.sin_port) << std::endl;

    // 绑定socket到指定地址和端口
    if (::bind(sock_fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(); // 绑定失败，清理资源
        return false;
    }
    return true; // 绑定成功
}

bool Socket::listen(int backlog)
{
    if (sock_fd_ < 0)
    {
        return false; // socket未创建
    }
    // 开始监听连接请求
    if (::listen(sock_fd_, backlog) < 0)
    {
        close(); // 监听失败清理资源
        return false;
    }
    return true; // 监听成功
}

bool Socket::setNonBlocking()
{
    if (sock_fd_ < 0)
    {
        std::cerr << "setNonBlocking failed: invalid sock_fd_=" << sock_fd_ << "\n";
        return false; // socket无效
    }
    // 获取当前文件状态标志
    int flags = fcntl(sock_fd_, F_GETFL, 0);
    if (flags < 0)
    {
        std::cerr << "setNonBlocking failed: fcntl(F_GETFL) error, errno=" << errno << "\n";
        return false;
    }
    // 添加非阻塞标志
    if (fcntl(sock_fd_, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        std::cerr << "setNonBlocking failed: fcntl(F_SETFL) error, errno=" << errno << "\n";
        return false;
    }
    std::cout << "setNonBlocking success: sock_fd_=" << sock_fd_ << " (non-blocking enabled)\n";
    return true;
}
bool Socket::setNonBlocking(int fd)
{
    // 第一步：检查fd有效性（避免无效fd导致fcntl失败）
    if (fd < 0)
    {
        std::cerr << "setNonBlocking failed: invalid fd=" << fd << "\n";
        return false;
    }

    // 第二步：获取fd当前的文件状态标志（F_GETFL = get file status flags）
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    { // fcntl失败返回-1，注意判断“== -1”（避免flags=0时误判）
        std::cerr << "setNonBlocking failed: fcntl(F_GETFL) error, errno=" << errno << "\n";
        return false;
    }

    // 第三步：添加非阻塞标志（O_NONBLOCK），并设置回fd（F_SETFL = set file status flags）
    // 用“|= ”确保不覆盖原有标志（如O_RDWR等），只新增O_NONBLOCK
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        std::cerr << "setNonBlocking failed: fcntl(F_SETFL) error, errno=" << errno << "\n";
        return false;
    }

    // 第四步：新增 SO_LINGER 选项配置（核心：强制close时立即发送FIN包，无延迟）
    struct linger ling_opt;
    ling_opt.l_onoff = 1;  // 启用 SO_LINGER 选项（必须设为1才生效）
    ling_opt.l_linger = 0; // 延迟时间0秒 → close时立即发送FIN/RST，不等待内核延迟
    if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling_opt, sizeof(ling_opt)) == -1)
    {
        std::cerr << "setNonBlocking failed: setsockopt(SO_LINGER) error, errno=" << errno << "\n";
        // 注意：SO_LINGER配置失败不影响非阻塞功能，可返回true（或根据需求返回false）
        // 此处返回true，因为非阻塞是核心需求，SO_LINGER是优化项
        std::cerr << "Warning: SO_LINGER config failed, but non-blocking is enabled\n";
    }

    // 第五步：可选优化：增大发送缓冲区（减少高并发下数据堆积）
    int sndbuf_size = 4096; // 4KB 发送缓冲区（可根据需求调整，如8192）
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size)) == -1)
    {
        std::cerr << "setNonBlocking failed: setsockopt(SO_SNDBUF) error, errno=" << errno << "\n";
        // 同样作为优化项，失败不影响核心功能
    }

    std::cout << "setNonBlocking success: fd=" << fd << " (non-blocking + SO_LINGER enabled)\n";
    return true;
}

int Socket::accept(struct sockaddr_in *client_addr)
{
    if (sock_fd_ < 0)
    {
        return -1; // socket无效
    }
    // 接收客户端连接
    socklen_t client_len = sizeof(*client_addr);
    int client_fd = ::accept(sock_fd_, (struct sockaddr *)client_addr, &client_len);
    if (client_fd < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("accept failed");
        return -1;
    }
    return client_fd; // 返回新的客户端连接
}

void Socket::close()
{
    if (sock_fd_ >= 0)
    {
        ::close(sock_fd_); // 关闭socket
        sock_fd_ = -1;     // 标记为无效
    }
}