#ifndef SERVER_HPP
#define SERVER_HPP

#include "server/Socket.hpp"
#include "server/Epoll.hpp"
#include "server/ThreadPool.hpp"
#include "server/Request.hpp"
#include "server/Response.hpp"
#include "server/Connection.hpp"
#include "server/ConnectionManager.hpp"
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <fstream>

class VideoServerAdapter;
// 基础网络层 - 只处理网络IO
class Server
{
public:
    Server(int port, std::weak_ptr<VideoServerAdapter> adapter_, int thread_num = 8);
    ~Server();
    void start();
    void stop();
    // 初始化信号处理
    void initSignalHandler();
    // 信号处理函数（静态）
    static void handleSignal(int sig);

    // 提交任务到线程池
    template <class F, class... Args>
    auto enqueueTask(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
    {
        return thread_pool_.enqueue(std::forward<F>(f), std::forward<Args>(args)...);
    }

private:
    void handleNewConnection();                              // 处理新连接
    void handleReadEvent(std::shared_ptr<Connection> conn);  // 处理读事件
    void handleWriteEvent(std::shared_ptr<Connection> conn); // 处理写事件
    void processRequest(std::shared_ptr<Connection> conn);   // 业务处理

    int port_;                       // 监听端口
    Socket server_socket;            // 监听socket
    Epoll epoll_;                    // epoll实例
    ThreadPool thread_pool_;         // 线程池
    ConnectionManager conn_manager_; // 连接管理器
    std::atomic<bool> is_runing_;    // 服务器运行标志
    std::weak_ptr<VideoServerAdapter> adapter_;
    static Server *instance_; // 静态指针，指向Server实例（供信号处理函数访问）
};

#endif