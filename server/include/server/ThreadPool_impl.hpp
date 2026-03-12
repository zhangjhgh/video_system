#ifndef THREAD_POOL_IMPL_HPP
#define THREAD_POOL_IMPL_HPP

#include <iostream>
#include <stdexcept>

template <class F, class... Args>
auto ThreadPool::enqueue(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    // 检查线程池是否已停止
    if (stop)
    {
        throw std::runtime_error("enqueue on stopped ThreadPool");
    }
    // 将任务和参数绑定，创建packages_task来获取future
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    // 获取任务的future，用于后续获取结果
    std::future<return_type> res = task->get_future();
    {
        // 锁定线程队列
        std::unique_lock<std::mutex> lock(queue_mutex);

        // 将任务包装为void()类型，添加到任务队列
        tasks.emplace([task]()
                      {
                          try
                          {
                              (*task)(); 
                          }
                          catch (const std::exception &e)
                          {
                              std::cerr << "Task exception: " << e.what() << '\n';
                              throw;    //重新抛出异常，会被future捕获
                          } });
        // 更新等待任务计数
        pending_tasks++;
    }
    // 通知一个等待的线程有新任务
    condition.notify_one();
    return res;
}

#endif // THREAD_POOL_IMPL_HPP