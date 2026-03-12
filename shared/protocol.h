#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <map>

// 任务状态
enum class TaskStatus
{
    PENDING = 0,
    PROCESSING = 1,
    COMPLETED = 2,
    FAILED = 3
};

// 处理类型
enum class ProcessingType
{
    ADD_WATERMARK = 0,
    EXTRACT_FRAMES = 1
};

// 通信协议结构
struct UploadResponse
{
    bool success;
    std::string task_id;
    std::string message;

    UploadResponse() : success(false) {}
};

struct StatusResponse
{
    TaskStatus status;
    int progress; // 0-100
    std::string result_url;
    std::string error_message;

    StatusResponse() : status(TaskStatus::PENDING), progress(0) {}
};

// 简单的JSON序列化（我们先用简单实现，后续可以引入json库）
inline std::string to_json(const UploadResponse &response)
{
    return "{\"success\":" + std::string(response.success ? "true" : "false") +
           ",\"task_id\":\"" + response.task_id +
           "\",\"message\":\"" + response.message + "\"}";
}

inline std::string to_json(const StatusResponse &response)
{
    std::string status_str;
    switch (response.status)
    {
    case TaskStatus::PENDING:
        status_str = "pending";
        break;
    case TaskStatus::PROCESSING:
        status_str = "processing";
        break;
    case TaskStatus::COMPLETED:
        status_str = "completed";
        break;
    case TaskStatus::FAILED:
        status_str = "failed";
        break;
    }

    return "{\"status\":\"" + status_str +
           "\",\"progress\":" + std::to_string(response.progress) +
           ",\"result_url\":\"" + response.result_url +
           "\",\"error\":\"" + response.error_message + "\"}";
}

#endif // PROTOCOL_H