#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <netinet/in.h>

class Socket
{
public:
    Socket();
    ~Socket();

    // 禁用拷贝构造和赋值
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    bool create();                               // 创建socket
    bool bind(int port);                         // 绑定到端口
    bool listen(int backlog = 10);               // 开始监听
    bool setNonBlocking();                       // 设置为非阻塞模式
    bool setNonBlocking(int fd);                 // 有参：设置传入的fd为非阻塞（新增功能）
    int accept(struct sockaddr_in *client_addr); // 接受连接
    void close();                                // 关闭socket

    // 获取socket文件描述符
    int getFd() const { return sock_fd_; }

private:
    int sock_fd_; // socket文件描述符
};

#endif