#include "tasks/TaskManager.hpp"
#include "server/VideoServerAdapter.hpp"

TaskManager::TaskManager() : taskCounter_(0)
{
    std::cout << "TaskManager initialized" << std::endl;
}

TaskManager::~TaskManager()
{
    std::cout << "TaskManager destroyed" << std::endl;
}

std::string TaskManager::createTask(const std::string &inputFile,
                                    const std::string &outputFile,
                                    const std::string &operation,
                                    const std::string &parameters)
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    std::string taskId = generateTaskId(operation);
    TaskInfo task;
    task.taskId = taskId;
    task.outputFile = outputFile;
    task.inputFile = inputFile;
    task.operation = operation;
    task.progress = 0.0;
    task.parameters = parameters;
    task.status = TaskStatus::PENDING;
    task.createTime = std::chrono::system_clock::now();
    task.updateTime = task.createTime;

    tasks_[taskId] = task;

    std::cout << "Task Created: " << taskId << "-" << operation << std::endl;

    return taskId;
}

bool TaskManager::startTask(const std::string &taskId)
{
    std::lock_guard<std::mutex> lock(taskMutex_);

    auto it = tasks_.find(taskId);
    if (it == tasks_.end())
    {
        std::cerr << "Task not found:" << taskId << std::endl;
        return false;
    }

    if (it->second.status != TaskStatus::PENDING)
    {
        std::cerr << "Task cannot be started: " << taskId << std::endl;
        return false;
    }
    TaskInfo task = it->second;
    it->second.status = TaskStatus::PROCESSING;
    it->second.updateTime = std::chrono::system_clock::now();

    auto adapter = VideoServerAdapter::getInstance();
    adapter->submitTask([this, task]()
                        {
                            try
                            {
                                // 解析JSON参数（统一格式）
                                nlohmann::json params = nlohmann::json::parse(task.parameters);
                                if (task.operation == "watermark")
                                {
                                    std::string text = params.value("watermarkText", "默认水印");
                                    int position = params.value("position", 0);
                                    int fontSize = params.value("fontSize", 20);
                                    std::string color = params.value("fontColor", "#FFFFF");
                                    float opacity = params.value("opacity", 0.7f);
                                    VideoProcessor::ProgressCallback callback = [this,task](double progress){
                                        this->updateProgress(task.taskId,progress);
                                    };
                                    VideoProcessor processor;
                                    bool success = processor.addWatermark(task.inputFile, task.outputFile, text, position, fontSize, color, opacity,callback);
                                    if (success)
                                        this->markCompleted(task.taskId, task.outputFile);
                                    else
                                        this->markFailed(task.taskId, "处理失败");
                                }
                            }
                            catch (const std::exception &e)
                            {
                                this->markFailed(task.taskId, e.what());
                            } });
    std::cout << "Task Started : " << taskId << std::endl;
    return true;
}

std::optional<TaskInfo> TaskManager::getTaskInfo(const std::string &taskId)
{
    std::lock_guard<std::mutex> lock(taskMutex_);

    auto it = tasks_.find(taskId);
    if (it != tasks_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::vector<TaskInfo> TaskManager::getAllTasks()
{
    std::lock_guard<std::mutex> lock(taskMutex_);

    std::vector<TaskInfo> result;
    for (const auto &pair : tasks_)
    {
        result.push_back(pair.second);
    }
    return result;
}

void TaskManager::updateProgress(const std::string &taskId, double progress)
{
    std::lock_guard<std::mutex> lock(taskMutex_);

    auto it = tasks_.find(taskId);
    if (it != tasks_.end())
    {
        it->second.progress = std::clamp(progress, 0.0, 100.0);
        it->second.updateTime = std::chrono::system_clock::now();
        if (it->second.progress == 100.0 && it->second.status == TaskStatus::PROCESSING)
        {
            it->second.status = TaskStatus::COMPLETED;
        }
    }
}

void TaskManager::markCompleted(const std::string &taskId, const std::string &outputFile)
{
    std::lock_guard<std::mutex> lock(taskMutex_);

    auto it = tasks_.find(taskId);
    if (it != tasks_.end())
    {
        it->second.status = TaskStatus::COMPLETED;
        it->second.progress = 100.0;
        it->second.outputFile = outputFile;
        it->second.updateTime = std::chrono::system_clock::now();
        std::cout << "Task Completed : " << taskId << std::endl;
    }
}

void TaskManager::markFailed(const std::string &taskId, const std::string &errorMessage)
{
    std::lock_guard<std::mutex> lock(taskMutex_);

    auto it = tasks_.find(taskId);
    if (it != tasks_.end())
    {
        it->second.status = TaskStatus::FAILED;
        it->second.errorMessage = errorMessage;
        it->second.updateTime = std::chrono::system_clock::now();

        std::cout << "Task failed: " << taskId << std::endl;
    }
}

std::string TaskManager::generateTaskId(const std::string &operation)
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "task_" << time_t << "_" << ++taskCounter_ << "_" << operation;
    return ss.str();
}

std::vector<TaskInfo> TaskManager::getTaskByStatus(TaskStatus status)
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    std::vector<TaskInfo> statusTasks;
    std::vector<TaskInfo> tasks = getAllTasks();
    if (tasks.empty())
    {
        for (auto task : tasks)
        {
            if (task.status == status)
            {
                statusTasks.push_back(task);
            }
        }
    }
    return statusTasks;
}