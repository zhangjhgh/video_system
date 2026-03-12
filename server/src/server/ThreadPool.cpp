#include "server/ThreadPool.hpp"
#include <iostream>
#include <thread>

ThreadPool::ThreadPool(size_t threads) : stop(false), pending_tasks(0) // 初始化成员变量stop和pending_tasks
{
    // 如果未指定线程，使用硬件并发数
    if (threads == 0)
    {
        threads = std::thread::hardware_concurrency();
        if (threads == 0)
            threads = 1;
    }
    std::cout << "Creating thread pool with" << threads << "threads" << std::endl;
    for (size_t i = 0; i < threads; i++)
    {
        workers.emplace_back([this]
                             {
                                 // 线程局部变量，可用于线程特定的初始化
                                 thread_local std::thread::id this_id = std::this_thread::get_id();
                                 std::cout << "Worker thread " << this_id << " started" << std::endl;
                                 for (;;)
                                 {
                                     std::function<void()> task;
                                     {
                                         // 等待任务或停止信号
                                         std::unique_lock<std::mutex> lock(this->queue_mutex);

                                         // 条件变量等待条件：有任务到来或线程池停止
                                         this->condition.wait(lock, [this]
                                                              { return this->stop || !this->tasks.empty(); });

                                         // 如果线程已停止且没有剩余任务，退出线程
                                         if (this->stop && this->tasks.empty())
                                         {
                                             return;
                                         }
                                         // 取一个任务执行
                                         task = std::move(this->tasks.front());
                                         this->tasks.pop();
                                         // 更新等待任务计数
                                         pending_tasks--;
                                     }
                                     //执行任务（不在锁范围内，避免阻塞其他线程）
                                     task();
                                 } });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    // 通知所有等待线程
    condition.notify_all();
    // 等待所有线程完成当前任务
    for (std::thread &worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    std::cout << "Thread pool destroyed" << std::endl;
}

// 获取等待中的任务数量
size_t ThreadPool::pendingTasks() const
{
    return pending_tasks.load();
}
// 获取工作中的线程数量
size_t ThreadPool::threadCount() const
{
    return workers.size();
}
