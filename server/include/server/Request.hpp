#ifndef REQUEST_HPP
#define REQUEST_HPP

#pragma once
#include <string>
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include <mutex>
#include <iostream>
#include "nlohmann/json.hpp"

enum class MultipartState
{
    START,           // 寻找起始边界
    READING_HEADERS, // 读取部分头部
    READING_BODY,    // 读取文件数据
    END              // 解析完成
};

class Request
{
public:
    Request();

    // 流式追加数据
    bool appendToBuffer(const char *data, size_t len);
    // 是否保持连接
    bool isKeepAlive() const;
    // 请求是否完整
    bool isRequestComplete() const { return state_.request_complete; }
    void reset();
    // 解析请求行（内部调用，外部无需关心）
    bool parseRequestLine();
    bool isJsonParsed() const;

    void extractPathParam(const std::string &prefix, const std::string &param_name);

    // 获取基础信息
    std::string getMethod() const { return basic_.method; }
    std::string getPath() const { return basic_.path; }
    std::string getVersion() const { return basic_.version; }

    // 获取参数
    std::string getRecvBuffer() const { return recv_buffer; }
    std::string getHeader(const std::string &key) const; // 替代原getHeaders
    std::string getBody() const { return content_.body; }
    std::string getPathParam(const std::string &param_name) const; // 路径参数（如taskId）
    std::string getQueryParam(const std::string &key) const;       // 查询参数
    std::string getJsonString(const std::string &key, const std::string &defaultVal = "") const;
    int getJsonInt(const std::string &key, int defaultVal = 0) const;
    nlohmann::json getJsonObject(const std::string &key) const;

    // 文件上传相关
    std::string getUploadedFilePath() const;
    std::string getUploadedFilename() const;
    MultipartState getMultipartState() const { return multipart_.state; }

    // 状态控制
    size_t getTotalReceived() const { return state_.total_received; }
    size_t getBodyReceived() const { return state_.body_received; }
    void addReceivedSize(size_t size) { state_.total_received += size; }
    bool isRequestLineParsed() const;
    bool isRouteChecked() const { return state_.route_checked; }
    void setRouteChecked(bool checked) { state_.route_checked = checked; }
    void resetRequestLineParsed();

private:
    // 1. 基础请求信息
    struct BasicInfo
    {
        std::string method;
        std::string path;
        std::string version;
        std::string query_string;
        std::unordered_map<std::string, std::string> query_params;
    } basic_;

    // 2. 接收与解析状态
    struct ParseState
    {
        bool request_line_parsed = false;
        bool route_checked = false;
        bool request_complete = false;
        size_t total_received = 0;
        size_t expected_body_size = 0;
        size_t body_received = 0;
        std::unordered_map<std::string, std::string> params_;
    } state_;

    // 3. 内容存储（头、body、JSON）
    struct Content
    {
        std::unordered_map<std::string, std::string> headers;
        std::string body;
        nlohmann::json json_body;
    } content_;

    // 4. 文件上传上下文
    struct MultipartContext
    {
        MultipartState state = MultipartState::START;
        std::string raw_boundary;
        std::string boundary_marker;
        std::string end_boundary_marker;
        std::string uploaded_filename;
        std::string upload_file_path;
        std::ofstream ofs;
        size_t file_data_start = 0;
        bool in_file_data = false;
    } multipart_;

    // 5. 缓存与同步
    std::string recv_buffer;
    mutable std::mutex buffer_mutex;

    // 内部解析方法
    bool parseHeadersFromBuffer();
    void parseMultipartData();
    bool handleMultipartStart();
    bool handleMultipartHeaders();
    bool handleMultipartBody();
    void finalizeMultipart();
    void parseQueryParams();

    // 辅助方法
    void writeFileData(const char *data, size_t len);
    void parsePartHeaders(const std::string &headers);
    void clearProcessed(size_t pos);
    void setBoundary(const std::string &boundary);
    size_t findBoundaryOptimized(const std::string &buffer, const std::string &pattern);
    static std::string toLowerCopy(const std::string &s);
    void setParam(const std::string &param_name, const std::string &param_value);
};

#endif