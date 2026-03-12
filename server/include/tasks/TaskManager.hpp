#ifndef TASKMANAGER_HPP
#define TASKMANAGER_HPP

#include "../video/VideoProcessor.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>
#include <optional>

// 任务状态枚举
enum class TaskStatus
{
    PENDING,    // 等待中
    PROCESSING, // 运行中
    COMPLETED,  // 完成
    FAILED,     // 失败
    CANCELLED   // 取消
};

// 任务信息结构
struct TaskInfo
{
    TaskInfo() : status(TaskStatus::PENDING), progress(0.0) {}

    std::string taskId;
    std::string inputFile;
    std::string outputFile;
    std::string operation;  // 操作
    std::string parameters; // JSON 字符串或键值对，存储任务参数
    TaskStatus status;
    double progress;
    std::string errorMessage;
    std::chrono::system_clock::time_point createTime;
    std::chrono::system_clock::time_point updateTime;
};

class VideoServerAdapter; // 前向声明，无需知道具体实现
// 任务管理
class TaskManager
{
public:
    TaskManager();
    ~TaskManager();
    static TaskManager &getInstance();

    // 任务管理
    std::string createTask(const std::string &inputFile,
                           const std::string &outputPath,
                           const std::string &operation,
                           const std::string &parameters = "");

    bool startTask(const std::string &TaskId);
    bool cancelTask(const std::string &taskId);
    bool deleteTask(const std::string &taskId);

    // 任务查询
    std::optional<TaskInfo> getTaskInfo(const std::string &taskId);
    std::vector<TaskInfo> getAllTasks();
    std::vector<TaskInfo> getTaskByStatus(TaskStatus status);

    // 进度更新
    void updateProgress(const std::string &taskId, double progress);
    void markCompleted(const std::string &taskId, const std::string &outputFile = "");
    void markFailed(const std::string &taskId, const std::string &errorMessage);

private:
    std::unordered_map<std::string, TaskInfo> tasks_;
    std::mutex taskMutex_;
    std::atomic<int> taskCounter_ = 0;

    // 生成唯一任务ID
    std::string generateTaskId(const std::string &operation);
};

#endif