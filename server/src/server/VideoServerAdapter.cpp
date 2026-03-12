#include "server/VideoServerAdapter.hpp"
#include "server/Server.hpp"

VideoServerAdapter::VideoServerAdapter(int port, int thread_num) : port_(port), thread_num_(thread_num)
{
    // 初始化目录
    upload_dir_ = "uploads";
    output_dir_ = "outputs";
    temp_dir_ = "temp";

    std::filesystem::create_directories(upload_dir_);
    std::filesystem::create_directories(output_dir_);
    std::filesystem::create_directories(temp_dir_);

    // 初始化组件
    video_processor_ = std::make_unique<VideoProcessor>();
    task_manager_ = std::make_unique<TaskManager>();

    // 设置路由
    setupRoutes();

    std::cout << "VideoServerAdapter initialized on port:" << port << std::endl;
}

std::shared_ptr<VideoServerAdapter> VideoServerAdapter::getInstance(int port, int thread_num)
{
    static std::shared_ptr<VideoServerAdapter> instance;
    static std::once_flag init_flag;

    std::call_once(init_flag, [&]()
                   {
        instance = std::shared_ptr<VideoServerAdapter>(new VideoServerAdapter(port,thread_num));
        instance->initServer(port,thread_num); });
    return instance;
}

void VideoServerAdapter::initServer(int port, int thread_num)
{
    auto weak_adapter = std::weak_ptr<VideoServerAdapter>(shared_from_this());
    server_ = std::make_unique<Server>(port, weak_adapter, thread_num);
}

VideoServerAdapter::~VideoServerAdapter()
{
    stop();
}

void VideoServerAdapter::start()
{
    std::cout << "Starting video processing server..." << std::endl;
    std::cout << "Upload directory: " << upload_dir_ << std::endl;
    std::cout << "Output directory: " << output_dir_ << std::endl;
    server_->start();
}

void VideoServerAdapter::stop()
{
    server_->stop();
}

void VideoServerAdapter::get(const std::string &path, RequestHandler handler)
{
    get_routes_[path] = handler;
}

void VideoServerAdapter::post(const std::string &path, RequestHandler handler)
{
    post_routes_[path] = handler;
}

void VideoServerAdapter::setupRoutes()
{
    get_routes_["/api/health"] = std::bind(&VideoServerAdapter::handleHealth,
                                           this, std::placeholders::_1, std::placeholders::_2);

    post_routes_["/api/upload"] = std::bind(&VideoServerAdapter::handleUpload,
                                            this, std::placeholders::_1, std::placeholders::_2);
    post_routes_["/api/process"] = std::bind(&VideoServerAdapter::handleProcess,
                                             this, std::placeholders::_1, std::placeholders::_2);
    get_dynamic_routes_.push_back({"/api/task/", "taskId",
                                   std::bind(&VideoServerAdapter::handleTaskQuery,
                                             this, std::placeholders::_1, std::placeholders::_2)});

    get_dynamic_routes_.push_back({"/api/download", "filePath",
                                   std::bind(&VideoServerAdapter::handleDownload, this, std::placeholders::_1, std::placeholders::_2)});
    get("/api/task", [this](Request &req, Response &resp)
        { this->handleTaskList(req, resp); });

    get("/api/download/:filename", [this](Request &req, Response &resp)
        { this->handleDownload(req, resp); });
};

void VideoServerAdapter::handleHealth(Request &req, Response &resp)
{
    std::cout << "[handleHealth] 已进入健康检查处理逻辑" << std::endl;
    (void)req;
    resp.setStatusCode(200);
    resp.setHeader("Content-Type", "application/json; charset=utf-8");
    resp.setHeader("Content-Security-Policy", "upgrade-insecure-requests;");

    std::string body = R"({"status":"OK","service":"video_processing"})";

    // 再次打印验证（确认 size 变为 45）
    std::cout << "[handleHealth] body: \"" << body << "\"" << std::endl;
    std::cout << "[handleHealth] body.size(): " << body.size() << std::endl;

    resp.setBody(body);

    if (req.isKeepAlive())
    {
        resp.setHeader("Connection", "keep-alive");
    }
    else
    {
        resp.setHeader("Connection", "close");
    }
}

void VideoServerAdapter::handleUpload(Request &req, Response &resp)
{
    if (req.getMethod() != "POST")
    {
        resp.setStatusCode(405);
        resp.setHeader("Content-Type", "application/json");
        resp.setBody(json({{"code", 405},
                           {"msg", "Method Not Allowed"},
                           {"data", json::object()}})
                         .dump());
        std::cout << "[Upload] 错误：请求方法不是POST（实际：" << req.getMethod() << "）" << std::endl;
        return;
    }

    const std::string contentType = req.getHeader("Content-Type");
    if (contentType.find("multipart/form-data") == std::string::npos)
    {
        resp.setStatusCode(415);
        resp.setHeader("Content-Type", "application/json");
        resp.setBody(json({{"code", 415},
                           {"msg", "不支持的媒体类型，仅接受multipart/form-data格式上传"},
                           {"data", json{"required_content_type", "multipart/form-data"}}})
                         .dump());
        std::cout << "[Upload] 错误：Content-Type不符合要求（实际：" << contentType << "）" << std::endl;
        return;
    }

    std::string fileExt;
    std::string uploadedFilePath = req.getUploadedFilePath(); // 服务器保存的文件路径（关键！后续处理用）
    std::string uploadedFilename = req.getUploadedFilename(); // 客户端原始文件名
    size_t fileSize = req.getBodyReceived();                  // 文件大小（字节）
    const std::string &fileBody = req.getBody();              // 文件二进制内容（调试用）
    std::cout << "[Upload] 接收的原始文件名：" << uploadedFilename << std::endl;
    size_t dotPos = uploadedFilename.find_last_of(".");
    if (dotPos != std::string::npos && dotPos < uploadedFilename.size() - 1)
    {
        fileExt = uploadedFilename.substr(dotPos + 1);
        std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);
        // 清理后缀中的非法字符（避免不可见字符干扰）
        fileExt.erase(
            remove_if(fileExt.begin(), fileExt.end(),
                      [](char c)
                      { return !isalnum(c); }), // 只保留字母数字
            fileExt.end());
    }
    else
    {
        fileExt = "";
    }
    std::cout << "[Upload] 提取到的文件后缀：" << (fileExt.empty() ? "空" : fileExt) << std::endl;

    // 校验后缀
    const std::unordered_set<std::string> allowedExts = {"mp4", "avi", "mov", "flv"};
    if (allowedExts.find(fileExt) == allowedExts.end())
    {
        // 日志打印后缀的十六进制值，确认是否有不可见字符
        std::cout << "[Upload] 后缀校验失败，实际后缀（十六进制）：";
        for (unsigned char c : fileExt)
        {
            std::cout << std::hex << static_cast<int>(c) << " ";
        }
        std::cout << std::dec << std::endl;

        // 清理文件并返回错误
        if (!uploadedFilePath.empty() && std::filesystem::exists(uploadedFilePath))
        {
            std::filesystem::remove(uploadedFilePath);
            std::cout << "[Upload] 清理无效文件：" << uploadedFilePath << std::endl;
        }
        resp.setStatusCode(400);
        resp.setBody(json{{"code", 400},
                          {"msg", "不支持的文件类型，仅允许MP4/AVI/MOV/FLV格式"},
                          {"data", json{{"allowed_extensions", {"mp4", "avi", "mov", "flv"}}, {"your_extension", fileExt}}}}
                         .dump());
        return;
    }

    // 校验文件实际存在且大小合理
    std::error_code ec; // 避免filesystem抛出异常
    size_t actualFileSize = std::filesystem::file_size(uploadedFilePath, ec);
    if (ec || actualFileSize == 0)
    {
        resp.setStatusCode(400);
        resp.setBody(json{{"code", 400},
                          {"msg", "文件不存在或为空"},
                          {"data", uploadedFilePath}}
                         .dump());
        return;
    }

    std::cout << "[Upload] Content-Type: " << req.getHeader("Content-Type") << std::endl;
    std::cout << "[Upload] Content-Length: " << req.getHeader("Content-Length") << std::endl;
    // 在 processRequest 中修改调试信息
    std::cout << "[Upload] File uploaded: " << req.getUploadedFilePath()
              << ", size: " << req.getBodyReceived() << " bytes" << std::endl;

    // 临时保存原始请求体用于调试
    std::ofstream debug_file("/tmp/upload_debug.bin", std::ios::binary);
    debug_file.write(req.getBody().data(), req.getBody().size());
    debug_file.close();

    std::cout << "[Upload] Debug data saved to /tmp/upload_debug.bin" << std::endl;

    resp.setStatusCode(200);
    json responseJson = json({{"filePath", uploadedFilePath},
                              {"filename", uploadedFilename},
                              {"fileSize", std::to_string(std::filesystem::file_size(uploadedFilePath))},
                              {"fileExtension", fileExt},
                              {"nextStep", "调用 /api/process接口传入filePath进行视频处理"}});
    resp.setBody(json({{"code", 200},
                       {"msg", "上传成功"},
                       {"data", responseJson}})
                     .dump());
    // 日志：记录成功上传信息
    std::cout << "[Upload] 成功：文件=" << uploadedFilename << "（" << fileSize << "字节），保存路径=" << uploadedFilePath << std::endl;
}

void VideoServerAdapter::handleProcess(Request &req, Response &resp)
{
    try
    {
        // 是否为JSON格式且已解析
        if (!req.isJsonParsed())
        {
            resp.setStatusCode(400);
            resp.setBody(json{{"code", 400}, {"msg", "请求格式错误，需要为application/json"}, {"data", json::object()}}.dump());
            return;
        }

        // 获取一级参数
        std::string filePath = req.getJsonString("filePath");
        std::string operation = req.getJsonString("operation");
        if (filePath.empty() || operation.empty())
        {
            resp.setStatusCode(400);
            resp.setBody(json{{"code:", 400},
                              {"msg", "缺少参数filePath或operation"},
                              {"data", json::object()}}
                             .dump());
            return;
        }
        // 校验文件是否存在
        if (!std::filesystem::exists(filePath))
        {
            resp.setStatusCode(404);
            resp.setBody(json{{"code", 404},
                              {"msg", "文件不存在: " + filePath},
                              {"data", json::object()}}
                             .dump());
            return; // 直接返回，不进入后续处理
        }

        // 校验文件是否可访问
        if (!std::filesystem::is_regular_file(filePath) ||
            (std::filesystem::status(filePath).permissions() & std::filesystem::perms::owner_read) == std::filesystem::perms::none)
        {
            resp.setStatusCode(403);
            resp.setBody(json{{"code", 403},
                              {"msg", "无权限访问文件: " + filePath},
                              {"data", json::object()}}
                             .dump());
            return;
        }

        // 获取嵌套参数
        nlohmann::json params = req.getJsonObject("parameters");
        std::string outputPath;
        std::string taskParams;

        if (operation == "watermark")
        {
            // 提取加水印参数
            std::string watermarkText = params.value("watermarkText", "默认水印");
            int position = params.value("position", 0);
            std::cout << "[Debug] 从请求解析到的 position = " << position
                      << "（0=右下，1=左上，2=中间）" << std::endl;
            int fontSize = params.value("fontSize", 20);
            std::string fontColor = params.value("fontColor", "#FFFFFF");
            float opacity = params.value("opacity", 0.7f);

            // 构造任务参数JSON（完整传递给TaskManager）
            taskParams = json({{"watermarkText", watermarkText},
                               {"position", position},
                               {"fontSize", fontSize},
                               {"fontColor", fontColor},
                               {"opacity", opacity}})
                             .dump(); // 生成输出路径（基于输入路径+操作类型+时间戳，避免特殊字符）
            outputPath = generateOutputPath(filePath, operation);
            std::cout << "[handleProcess] 加水印任务：输入=" << filePath
                      << "，输出=" << outputPath << std::endl;
        }
        else
        {
            resp.setStatusCode(400);
            resp.setBody(json{{"code", 400}, {"msg", "不支持的操作：" + operation}}.dump());
            return;
        }
        std::string taskId = task_manager_->createTask(filePath,    // 输入文件路径
                                                       outputPath,  // 输出文件路径
                                                       operation,   // 操作类型（如"watermark"）
                                                       taskParams); // 任务参数（JSON字符串）);
        if (!task_manager_->startTask(taskId))
        {
            resp.setStatusCode(500);
            resp.setBody(json{{"code", 500},
                              {"msg", "任务启动失败"},
                              {"data", json::object()}}
                             .dump());
            return;
        }
        // 构造成功响应（包含taskId供客户端查询）
        json responseData = {
            {"taskId", taskId},
            {"status", "processing"},
            {"inputFile", filePath},
            {"outputFile", outputPath}};

        resp.setStatusCode(200);
        resp.setBody(json({{"code", 200},
                           {"msg", operation + "任务已提交"},
                           {"data", responseData}})
                         .dump());
        // 日志：记录成功上传信息
        std::cout << "[Process] 任务提交成功：taskId=" << taskId << "，操作=" << operation << std::endl;
    }
    catch (const std::exception &e)
    {
        // 统一异常处理
        resp.setStatusCode(500);
        resp.setBody(json{{"code", 500}, {"msg", "服务器错误：" + std::string(e.what())}}.dump());
        std::cerr << "[handleProcess] 异常：" << e.what() << std::endl;
    }
}
void VideoServerAdapter::handleTaskQuery(Request &req, Response &resp)
{
    try
    {
        std::cout << "[handleTaskQuery] 进度查询，path：" << req.getPath() << std::endl;
        std::string path = req.getPath();
        const std::string prefix = "/api/task/";
        if (path.substr(0, prefix.size()) != prefix)
        {
            resp.setStatusCode(400);
            resp.setBody(json{{"code", 400}, {"msg", "路径格式错误：/api/task/{taskId}"}}.dump());
            return;
        }
        std::string taskId = path.substr(prefix.size());
        if (taskId.empty())
        {
            resp.setStatusCode(400);
            resp.setBody(json{{"code", 400}, {"msg", "缺少taskId"}}.dump());
            return;
        }

        // 调用优化后的getTaskInfo（返回optional）
        auto taskOpt = task_manager_->getTaskInfo(taskId);
        if (!taskOpt.has_value())
        {
            resp.setStatusCode(404);
            resp.setBody(json{{"code", 404}, {"msg", "任务不存在：" + taskId}}.dump());
            return;
        }
        TaskInfo taskInfo = taskOpt.value();

        // 构造响应（使用修正后的errorMessage）
        json data = {
            {"taskId", taskInfo.taskId},
            {"status", taskStatusToString(taskInfo.status)},
            {"progress", std::round(taskInfo.progress * 100) / 100}, // 保留2位小数
            {"inputFile", taskInfo.inputFile},
            {"outputFile", taskInfo.outputFile},
            {"errorMsg", taskInfo.errorMessage},                            // 修正后字段
            {"createTime", taskInfo.createTime.time_since_epoch().count()}, // 可选：格式化时间（如"2024-05-20 10:30:00"）
            {"updateTime", taskInfo.updateTime.time_since_epoch().count()}};
        resp.setStatusCode(200);
        resp.setBody(json{{"code", 200}, {"msg", "查询成功"}, {"data", data}}.dump());
    }
    catch (const std::exception &e)
    {
        resp.setStatusCode(500);
        resp.setBody(json{{"code", 500}, {"msg", "服务器错误：" + std::string(e.what())}}.dump());
        std::cerr << "[handleTaskQuery] 异常：" << e.what() << std::endl;
    }
}

void VideoServerAdapter::handleTaskList(Request &req, Response &resp)
{
    (void)req;
    resp.setStatusCode(200);
    resp.setBody(R"({"message": "Task list endpoint - TODO: implement"})");
}
void VideoServerAdapter::handleDownload(Request &req, Response &resp)
{
    try
    {
        std::string path = req.getPath();
        std::string prefix = "/api/download";
        std::cout << "[handleDownload] 下载文件" << path.substr(prefix.size()) << std::endl;
        if (path.substr(0, prefix.size()) != prefix)
        {
            resp.setStatusCode(400);
            resp.setBody(json({{"code", 400},
                               {"msg", "路径格式错误：/api/download/{outputPath}"},
                               {"data", json::object()}})
                             .dump());
            return;
        }

        std::string outputPath = path.substr(prefix.size());
        if (outputPath.empty() || outputPath == "/")
        {
            resp.setStatusCode(400);
            resp.setBody(json({{"code", 400},
                               {"msg", "缺少下载路径"},
                               {"data", json::object()}})
                             .dump());
            return;
        }

        if (outputPath.front() == '/')
            outputPath = outputPath.substr(1);
        if (outputPath.find("../") != std::string::npos || outputPath.find("..\\") != std::string::npos)
        {
            resp.setStatusCode(403);
            resp.setBody(json{{"code", 403},
                              {"msg", "非法下载路径"},
                              {"data", json::object()}}
                             .dump());
            return;
        }
        // 拼接真实路径并粘贴
        fs::path realFilePath = fs::path(OUTPUT_DIR) / outputPath;
        fs::path absRealPath = fs::absolute(realFilePath);
        if (absRealPath.string().find(OUTPUT_DIR) != 0)
        {
            resp.setStatusCode(403);
            resp.setBody(json{
                {"code", 403},
                {"msg", "非法下载路径"},
                {"data", json::object()}}
                             .dump());
            return;
        }

        // 检验文件合法性
        if (!fs::exists(absRealPath))
        {
            resp.setStatusCode(404);
            resp.setBody(json{
                {"code", 404},
                {"msg", "文件不存在：" + outputPath},
                {"data", json{
                             {"requested_path", outputPath},
                             {"real_path", absRealPath.string()}}}}
                             .dump());
            return;
        }

        if ((fs::status(absRealPath).permissions() & fs::perms::owner_read) == fs::perms::none)
        {
            resp.setStatusCode(403);
            resp.setBody(json{
                {"code", 403},
                {"msg", "无文件读取权限：" + outputPath},
                {"data", json::object()}}
                             .dump());
            return;
        }

        // 设置响应头
        uint64_t fileSize = fs::file_size(absRealPath);
        std::string fileExt = absRealPath.extension().string();
        std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);

        std::string contentType = "application/octet-stream";
        if (VIDEO_MIME_TYPES.count(fileExt))
            contentType = VIDEO_MIME_TYPES.at(fileExt);
        resp.setHeader("Content-Type", contentType);
        resp.setHeader("Content-Disposition", "attachment;filename=\"" + absRealPath.filename().string() + "\"");
        resp.setHeader("Content-Length", std::to_string(fileSize));
        resp.setHeader("Accept-Ranges", "bytes");
        resp.setStatusCode(200);

        bool writeSuccess = writeFileToResponse(absRealPath, resp, 4096);
        if (!writeSuccess)
        {
            resp.setStatusCode(500);
            resp.setBody(json{
                {"code", 500},
                {"msg", "服务器错误：文件读取或写入响应失败"},
                {"data", json::object()}}
                             .dump());
            return;
        }
        resp.setFileTotalSize(fileSize);
        // 下载成功日志
        std::cout << "[下载接口] 成功：文件=" << absRealPath.string() << "，大小=" << fileSize << "字节" << std::endl;
    }
    catch (const fs::filesystem_error &e) // 文件系统相关异常（如权限、路径错误）
    {
        resp.setStatusCode(500);
        resp.setBody(json{
            {"code", 500},
            {"msg", "服务器文件操作异常：" + std::string(e.what())},
            {"data", json::object()}}
                         .dump());
        std::cerr << "[下载接口] 文件异常：" << e.what() << std::endl;
    }
    catch (const std::exception &e) // 其他通用异常
    {
        resp.setStatusCode(500);
        resp.setBody(json{
            {"code", 500},
            {"msg", "服务器内部异常：" + std::string(e.what())},
            {"data", json::object()}}
                         .dump());
        std::cerr << "[下载接口] 通用异常：" << e.what() << std::endl;
    }
    catch (...) // 捕获所有未预料的异常
    {
        resp.setStatusCode(500);
        resp.setBody(json{
            {"code", 500},
            {"msg", "服务器未知异常"},
            {"data", json::object()}}
                         .dump());
        std::cerr << "[下载接口] 未知异常" << std::endl;
    }
}

std::string VideoServerAdapter::generateOutputPath(const std::string &inputPath, const std::string &operation)
{
    std::filesystem::path input(inputPath);
    // 生成时间戳（避免文件名冲突）
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                         now.time_since_epoch())
                         .count();
    // 输出路径：output_dir/文件名_操作类型_时间戳.扩展名
    return output_dir_ + "/" +
           input.stem().string() + "_" +
           operation + "_" +
           std::to_string(timestamp) +
           input.extension().string();
}

// 辅助函数：将TaskStatus枚举转为字符串（方便客户端理解）
std::string VideoServerAdapter::taskStatusToString(TaskStatus status)
{
    switch (status)
    {
    case TaskStatus::PENDING:
        return "pending";
    case TaskStatus::PROCESSING:
        return "processing";
    case TaskStatus::COMPLETED:
        return "completed";
    case TaskStatus::FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

// 分发请求并构建响应
bool VideoServerAdapter::dispatchRequest(Request &req, Response &resp, bool check_only)
{
    std::string method = req.getMethod();
    std::string path = req.getPath();

    if (method == "GET")
    {
        // 优先静态路由匹配
        auto static_it = get_routes_.find(path);
        if (static_it != get_routes_.end())
        {
            if (!check_only) // 非检查模式执行
                static_it->second(req, resp);
            return true;
        }

        // 动态路由匹配
        std::lock_guard<std::mutex> lock(route_mutex_);
        for (const auto &dynamic_route : get_dynamic_routes_)
        {
            const std::string &prefix = dynamic_route.prefix;
            if (path.size() >= prefix.size() && path.substr(0, dynamic_route.prefix.size()) == dynamic_route.prefix)
            {
                if (!check_only)
                {
                    req.extractPathParam(prefix, dynamic_route.param_name);
                    dynamic_route.handler(req, resp);
                }
                return true;
            }
        }
        // 未匹配路由
        resp.setStatusCode(404);
        resp.setBody(json{{"code", 404}, {"msg", "GET Route not found: " + path}}.dump());
        return false;
    }
    if (method == "POST")
    {
        // 优先静态路由匹配
        auto static_it = post_routes_.find(path);
        if (static_it != post_routes_.end())
        {
            if (!check_only)
                static_it->second(req, resp);
            return true;
        }

        // 动态路由匹配
        std::lock_guard<std::mutex> lock(route_mutex_);
        for (const auto &dynamic_route : post_dynamic_routes_)
        {
            const std::string &prefix = dynamic_route.prefix;
            if (path.size() >= prefix.size() && path.substr(0, dynamic_route.prefix.size()) == dynamic_route.prefix)
            {
                if (!check_only)
                {
                    req.extractPathParam(prefix, dynamic_route.param_name);
                    dynamic_route.handler(req, resp);
                }
                return true;
            }
        }
        // 未匹配路由
        resp.setStatusCode(404);
        resp.setBody(json{{"code", 404}, {"msg", "POST Route not found: " + path}}.dump());
        return false;
    }
}

bool VideoServerAdapter::writeFileToResponse(const fs::path &filePath, Response &resp, size_t bufferSize)
{
    std::cout << "[下载] 开始传输文件: " << filePath << std::endl;
    // 以二进制打开文件
    std::ifstream fileStream(filePath, std::ios::binary);

    if (!fileStream.is_open())
    {
        std::cerr << "[writeFileToResponse]失败，无法打开文件" << filePath.string() << std::endl;
        return false;
    }

    // 获取文件大小
    fileStream.seekg(0, std::ios::end);
    size_t fileSize = fileStream.tellg();
    fileStream.seekg(0, std::ios::beg);

    std::cout << "[下载] 文件大小: " << fileSize << " 字节" << std::endl;

    if (fileSize == 0)
    {
        std::cerr << "[下载] 错误：文件为空" << std::endl;
        fileStream.close();
        return false;
    }

    resp.setBody("");
    // 分块读取写入响应体
    std::vector<char> buffer(bufferSize);
    size_t totalRead = 0;
    while (fileStream.read(buffer.data(), buffer.size()) || fileStream.gcount() > 0)
    {
        size_t bytesRead = fileStream.gcount();
        if (bytesRead <= 0)
            break;

        resp.appendToBody(buffer.data(), bytesRead);
        totalRead += bytesRead;

        // 显示进度
        if (totalRead % (5 * 1024 * 1024) < bufferSize)
        {
            std::cout << "[下载]进度：" << totalRead << "/" << fileSize << "(" << (totalRead * 100 / fileSize) << "%)" << std::endl;
        }
    }

    fileStream.close();

    std::cout << "[下载] 完成: " << totalRead << "/" << fileSize << " 字节" << std::endl;

    if (totalRead != fileSize)
    {
        std::cerr << "[下载] 警告：读取字节数不匹配" << std::endl;
        return false;
    }

    return true;
}