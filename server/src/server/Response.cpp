#include "server/Response.hpp"

std::string Response::toString() const
{
    std::string resp;

    // 1. 响应行（必须是 "HTTP/1.1 状态码 描述\r\n"）
    auto it = STATUS_MAP.find(status_code);
    if (it == STATUS_MAP.end())
    {
        // 如果状态码不在映射中，使用默认描述
        resp += "HTTP/1.1 " + std::to_string(status_code) + " Unknown Status\r\n";
    }
    else
    {
        resp += "HTTP/1.1 " + std::to_string(status_code) + " " + it->second + "\r\n";
    }

    // 2. 响应头（每个头格式："Key: Value\r\n"）
    for (const auto &[key, value] : headers)
    {
        resp += key + ": " + value + "\r\n";
    }

    std::cout << "[Response] 响应：" << resp << std::endl;
    // 3. 响应头和响应体之间必须加一个空行（\r\n）
    resp += "\r\n";

    // 4. 响应体
    if (!body.empty())
    {
        resp.append(body.data(), body.size());
    }
    return resp;
}

void Response::setBody(const std::string &b)
{
    body = b;
    setHeader("Content-Length", std::to_string(b.size()));
}

void Response::appendToBody(const char *data, size_t size)
{
    body.append(data, size);
    setHeader("Content-Length", std::to_string(body.size()));
}

void Response::appendToBody(const std::vector<char> &data)
{
    body.append(data.data(), data.size());
    setHeader("Content-Length", std::to_string(body.size()));
}

void Response::reset()
{
    status_code = 200; // 默认状态码
    headers.clear();   // 清空 headers
    body.clear();      // 清空 body
    size_t file_total_size_ = 0;
}