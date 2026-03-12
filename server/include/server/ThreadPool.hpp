#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <cstddef>
#include <future>
#include <vector>
#include <queue>
#include <functional>

class ThreadPool
{
public:
    // 构造函数：创建指定数量的工作线程
    ThreadPool(size_t threads = std::thread::hardware_concurrency());
    // 析构函数，停止所有线程
    ~ThreadPool();

    // 向线程池里添加任务，返回一个future用于获取结果
    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>;

    // 获取等待中的任务数量
    size_t pendingTasks() const;
    // 获取工作中的线程数量
    size_t threadCount() const;
    // 禁用拷贝
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete; // 运算符重载

private:
    // 工作线程数组
    std::vector<std::thread> workers;
    // 任务队列
    std::queue<std::function<void()>> tasks;
    // 同步原语
    mutable std::mutex queue_mutex;
    std::condition_variable condition;

    // 停止标志
    std::atomic<bool> stop;
    // 等待任务计数
    std::atomic<size_t> pending_tasks;
};

#include "ThreadPool_impl.hpp"
#endif