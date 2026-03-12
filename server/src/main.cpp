#include "../include/server/VideoServerAdapter.hpp"
#include <iostream>
#include <csignal>

std::unique_ptr<VideoServerAdapter> server;
void signalHandler(int signal)
{
    std::cout << "\nReceived signal: " << signal << ",shutting down server..." << std::endl;
    if (server)
        server->stop();
}

int main()
{
    std::cout << "=== Video Processing Server ===" << std::endl;

    // 注册信号处理
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try
    {
        // 关键：通过工厂方法 create() 创建，而非直接 new 或栈对象
        auto adapter = VideoServerAdapter::create(8080, 8); // 8080端口，8个线程
        adapter->start();                                   // 启动服务器
    }
    catch (const std::exception &e)
    {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Server stopped." << std::endl;
    return 0;
}