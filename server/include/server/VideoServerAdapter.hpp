// server/include/server/VideoServerAdapter.hpp
#ifndef VIDEOSERVERADAPTER_HPP
#define VIDEOSERVERADAPTER_HPP

#include "Server.hpp"
#include "Request.hpp"
#include "Response.hpp"
#include "tasks/TaskManager.hpp"
#include "video/VideoProcessor.hpp"
#include "video/VideoFormatUnifier.hpp"
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "nlohmann/json.hpp"
#include <future>

using json = nlohmann::json;
namespace fs = std::filesystem;

class Server;
class TaskManager;

struct DynamicRoute
{
    std::string prefix;
    std::string param_name;
    std::function<void(Request &, Response &)> handler;
};

// 业务逻辑层 - 处理视频处理业务
class VideoServerAdapter : public std::enable_shared_from_this<VideoServerAdapter>
{
public:
    using RequestHandler = std::function<void(Request &, Response &)>;

    static std::shared_ptr<VideoServerAdapter> getInstance(int port = 8080, int thread_num = 8);
    static std::shared_ptr<VideoServerAdapter> create(int port, int thread_num)
    {
        return getInstance(port, thread_num);
    }

    ~VideoServerAdapter();

    void start();
    void stop();

    void submitTask(std::function<void()> task);
    // 路由注册
    void get(const std::string &path, RequestHandler handler);
    void post(const std::string &path, RequestHandler handler);
    const std::unordered_map<std::string, RequestHandler> &getGetRoutes() const { return get_routes_; };
    const std::unordered_map<std::string, RequestHandler> &getPostRoutes() const { return post_routes_; };

    bool dispatchRequest(Request &req, Response &res, bool check_only);
    // 转发任务到Server的线程池
    template <class F, class... Args>
    auto submitTask(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
    {
        if (server_)
        {
            return server_->enqueueTask(std::forward<F>(f), std::forward<Args>(args)...);
        }
        throw std::runtime_error("Server Not Initialized");
    }

private:
    // 构造函数改为私有，强制通过工厂方法创建
    VideoServerAdapter(int port, int thread_num);
    std::string generateOutputPath(const std::string &inputPath, const std::string &suffix);
    // 延迟初始化 Server（避免构造函数中调用 shared_from_this()）
    void initServer(int port, int thread_num);
    void setupRoutes();
    std::string taskStatusToString(TaskStatus status);

    // 路由处理函数
    void handleHealth(Request &req, Response &resp);
    void handleUpload(Request &req, Response &resp);
    void handleProcess(Request &req, Response &resp);
    void handleTaskQuery(Request &req, Response &resp);
    void handleTaskList(Request &req, Response &resp);
    void handleDownload(Request &req, Response &resp);

    bool writeFileToResponse(const fs::path &filePath, Response &resp, size_t buffer_size = 4096);

    std::unique_ptr<Server> server_;
    std::unique_ptr<VideoProcessor> video_processor_;
    std::unique_ptr<TaskManager> task_manager_;

    std::unordered_map<std::string, RequestHandler> get_routes_;
    std::unordered_map<std::string, RequestHandler> post_routes_;

    // 动态路由
    std::vector<DynamicRoute> get_dynamic_routes_;
    std::vector<DynamicRoute> post_dynamic_routes_;
    mutable std::mutex route_mutex_;

    std::string upload_dir_;
    std::string output_dir_;
    std::string temp_dir_;

    int port_;       // 端口号
    int thread_num_; // 线程数

    // 文件下载
    const std::string OUTPUT_DIR = "/home/zhangsan/~projects/video_system/server/outputs";
    const std::unordered_map<std::string, std::string> VIDEO_MIME_TYPES = {
        {".mp4", "video/mp4"},
        {".avi", "video/x-msvideo"},
        {".mov", "video/quicktime"},
        {".flv", "video/x-flv"},
        {".mkv", "video/x-matroska"}};
};

#endif