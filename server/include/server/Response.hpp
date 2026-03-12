#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#pragma once
#include <string>
#include <unordered_map>
#include <sstream>
#include <vector>
#include <iostream>

// Http状态码
const std::unordered_map<int, std::string> STATUS_MAP = {
    // 成功响应
    {200, "OK"},
    {201, "Created"},    // 资源创建成功（如视频上传完成）
    {204, "No Content"}, // 成功但无响应体（如删除视频）

    // 客户端错误
    {400, "Bad Request"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {413, "Payload Too Large"},      // 请求体过大（如视频超过大小限制）
    {415, "Unsupported Media Type"}, // 不支持的格式（如上传非视频文件）

    // 服务器错误
    {500, "Internal Server Error"},
    {503, "Service Unavailable"}}; // 服务暂时不可用（如任务队列满

class Response
{
public:
    Response() : status_code(200),
                 version("HTTP/1.1")
    {
        setHeader("Content-Security-Policy", "upgrade-insecure-requests;");
    };

    // 设置响应属性
    int getStatusCode() const { return status_code; }
    void setStatusCode(int code) { status_code = code; }
    void setVersion(std::string v) { version = v; }
    void setHeader(const std::string &key, const std::string &value) { headers[key] = value; }
    void setBody(const std::string &b);
    void appendToBody(const char *data, size_t size);
    void appendToBody(const std::vector<char> &data);
    // 生成最终的响应字符串（供 Socket 发送）
    std::string toString() const;

    // 设置文件总大小（供handleDownload调用）
    void setFileTotalSize(size_t size)
    {
        file_total_size_ = size;
    }
    // 获取文件总大小（供发送逻辑调用）
    size_t getFileTotalSize() const
    {
        return file_total_size_;
    }
    void reset();

private:
    int status_code;                                      // 状态码
    std::string version;                                  // HTTP版本
    std::unordered_map<std::string, std::string> headers; // 响应头
    std::string body;                                     // 响应体
    size_t file_total_size_ = 0;                          // 存储下载文件的总大小（默认0，非下载场景无需设置）
};
#endif