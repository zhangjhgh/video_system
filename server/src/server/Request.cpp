#include "server/Request.hpp"

Request::Request()
{
    state_.expected_body_size = 0;
    state_.body_received = 0;
    state_.request_complete = false;
    state_.total_received = 0;
    state_.request_line_parsed = false;
    state_.route_checked = false;

    multipart_.state = MultipartState::START;
    multipart_.in_file_data = false;
    multipart_.file_data_start = 0;
}
void Request::reset()
{
    std::lock_guard<std::mutex> lock(buffer_mutex);
    basic_.method.clear();
    basic_.path.clear();
    basic_.version.clear();
    basic_.query_string.clear();
    basic_.query_params.clear();

    // 2. 重置接收与解析状态（恢复初始值）
    state_.total_received = 0;
    state_.route_checked = false;
    state_.request_complete = false;
    state_.expected_body_size = 0;
    state_.body_received = 0;
    state_.request_line_parsed = false; // 非原子变量，直接赋值

    // 3. 重置内容存储（清空头、body、JSON体）
    content_.headers.clear();
    content_.body.clear();
    content_.json_body = nlohmann::json(); // 重置为空JSON

    // 4. 重置文件上传上下文（释放资源+清空状态）
    multipart_.state = MultipartState::START;
    multipart_.in_file_data = false;
    multipart_.file_data_start = 0;
    if (multipart_.ofs.is_open())
    { // 关闭可能打开的文件流
        multipart_.ofs.close();
    }
    multipart_.uploaded_filename.clear();
    multipart_.upload_file_path.clear();
    multipart_.raw_boundary.clear();
    multipart_.boundary_marker.clear();
    multipart_.end_boundary_marker.clear();

    // 5. 重置缓存（清空接收缓冲区）
    recv_buffer.clear();

    // 调试日志（按新结构调整输出）
    std::cout << "[Request] 重置完成："
              << " method=" << (basic_.method.empty() ? "空" : basic_.method)
              << ", path=" << (basic_.path.empty() ? "空" : basic_.path)
              << ", boundary=" << (multipart_.boundary_marker.empty() ? "空" : multipart_.boundary_marker)
              << ", recv_buffer_size=" << recv_buffer.size()
              << ", route_checked=" << std::boolalpha << state_.route_checked
              << std::endl;
}

bool Request::isKeepAlive() const
{
    std::string conn_header = getHeader("Connection");

    // 将 Connection 头转换为小写进行比较
    std::string lower_conn;
    for (char c : conn_header)
    {
        lower_conn += std::tolower(c);
    }

    if (basic_.version == "HTTP/1.1")
    {
        // HTTP/1.1 默认保持连接，除非明确声明 "close"（任何大小写）
        return lower_conn != "close";
    }
    else if (basic_.version == "HTTP/1.0")
    {
        // HTTP/1.0 默认关闭连接，除非明确声明 "keep-alive"（任何大小写）
        return lower_conn == "keep-alive";
    }
    return false;
}

std::string Request::getHeader(const std::string &key) const
{
    std::string lower_key = key;
    for (char &c : lower_key)
        c = std::tolower(c);
    auto it = content_.headers.find(lower_key);
    return (it != content_.headers.end()) ? it->second : "";
}

bool Request::appendToBuffer(const char *data, size_t len)
{
    std::lock_guard<std::mutex> lock(buffer_mutex);
    recv_buffer.append(data, len);
    state_.request_complete = false;
    bool header_parsed = !content_.headers.empty();

    std::cout << "[Request] appendToBuffer: 新增数据 " << len << " 字节，累计 recv_buffer 长度 = " << recv_buffer.size() << std::endl;

    // 1. 检查请求行是否已解析
    if (!state_.request_line_parsed)
    {
        std::cout << "[Request] appendToBuffer: 请求行未解析，直接返回" << std::endl;
        return false;
    }
    else
    {
        std::cout << "[Request] appendToBuffer: 请求行已解析，当前 recv_buffer 长度 = " << recv_buffer.size() << " 字节" << std::endl;
    }

    // 2. 解析请求头（若未解析）
    if (!header_parsed)
    {
        size_t header_end = recv_buffer.find("\r\n\r\n");
        if (header_end != std::string::npos)
        {
            std::cout << "[Request] appendToBuffer: 找到请求头结束标记，触发解析请求头" << std::endl;
            if (parseHeadersFromBuffer())
            {
                header_parsed = true;
            }
            else
            {
                std::cout << "[Request] appendToBuffer: 请求头解析失败" << std::endl;
                return false;
            }
        }
        else
        {
            std::cout << "[Request] appendToBuffer: 未找到请求头结束标记，等待更多数据" << std::endl;
            return false;
        }
    }

    if (header_parsed)
    {
        std::string contentType = getHeader("content-type");
        // 处理文件上传请求体
        if (contentType.find("multipart/form-data") != std::string::npos)
        {
            std::cout << "[Request] appendToBuffer: 处理 Multipart 上传，当前 recv_buffer 长度 = " << recv_buffer.size() << std::endl;
            parseMultipartData();
            if (state_.request_complete)
            {
                std::cout << "[Request] appendToBuffer: Multipart 上传完成，标记 request_complete = true" << std::endl;
            }
            else
            {
                std::cout << "[Request] appendToBuffer: Multipart 上传未完成，继续接收数据" << std::endl;
            }
        }
        // 处理 JSON 请求体
        else if (contentType.find("application/json") != std::string::npos)
        {
            content_.body = recv_buffer;
            state_.body_received = content_.body.size();
            // 关键日志：输出 body_received 和 expected_body_size 的对比
            std::cout << "[Request] appendToBuffer: 处理 JSON 体，body_received = " << state_.body_received
                      << "，expected_body_size = " << state_.expected_body_size << std::endl;

            if (state_.body_received >= state_.expected_body_size)
            {
                state_.request_complete = true;
                std::cout << "[Request] appendToBuffer: 请求体已完整（" << state_.body_received << " >= " << state_.expected_body_size << "），标记 request_complete = true" << std::endl;
                // 尝试解析 JSON（日志辅助定位）
                try
                {
                    content_.json_body = nlohmann::json::parse(content_.body);
                    std::cout << "[Request] appendToBuffer: JSON 解析成功" << std::endl;
                }
                catch (const nlohmann::json::parse_error &e)
                {
                    std::cout << "[Request] appendToBuffer: JSON 解析失败：" << e.what() << std::endl;
                }
            }
            else
            {
                std::cout << "[Request] appendToBuffer: 请求体不完整（" << state_.body_received << " < " << state_.expected_body_size << "）" << std::endl;
            }
        }
        else
        {
            state_.body_received = recv_buffer.size();
            if (state_.body_received >= state_.expected_body_size)
            {
                state_.request_complete = true;
                content_.body = recv_buffer;
                std::cout << "[Request] appendToBuffer: 其他类型请求体完整，标记 request_complete = true" << std::endl;
            }
        }
    }

    std::cout << "[Request] appendToBuffer: 最终 request_complete = " << (state_.request_complete ? "true" : "false") << std::endl;
    return state_.request_complete;
}

bool Request::parseRequestLine()
{
    if (state_.request_line_parsed)
        return true;
    std::lock_guard<std::mutex> lock(buffer_mutex);
    // 检查请求行长度（防止恶意超长）
    const size_t MAX_REQUEST_LINE_LEN = 64 * 1024;
    if (recv_buffer.size() > MAX_REQUEST_LINE_LEN)
    {
        std::cout << "[Request] Request line too long (> " << MAX_REQUEST_LINE_LEN << " bytes)" << std::endl;
        return false;
    }

    // 检查缓冲区是否包含请求行结束符（"\r\n"），数据不足则返回false
    size_t crlf_pos = recv_buffer.find("\r\n");
    if (crlf_pos == std::string::npos)
    {
        std::cout << "[Request] 请求行数据不足，等待更多数据" << std::endl;
        return false;
    }
    std::string request_line = recv_buffer.substr(0, crlf_pos);
    std::istringstream iss(request_line);
    if (!(iss >> basic_.method >> basic_.path >> basic_.version))
    {
        std::cout << "[Request] 请求行格式错误：" << request_line << std::endl;
        return false;
    }
    // 解析出 version 后，校验格式
    if (basic_.version.substr(0, 5) != "HTTP/" || basic_.version.size() < 6)
    {
        std::cout << "[Request] 无效的HTTP版本：" << basic_.version << std::endl;
        return false;
    }

    size_t anchor_pos = basic_.path.find("#");
    if (anchor_pos != std::string::npos)
        basic_.path = basic_.path.substr(0, anchor_pos);

    size_t query_pos = basic_.path.find("?");
    if (query_pos != std::string::npos)
    {
        basic_.path = basic_.path.substr(0, query_pos);
        basic_.query_string = basic_.path.substr(query_pos + 1);
    }
    else
        basic_.query_string.clear();

    // 从缓冲区删除请求行（包含 \r\n，共 crlf_pos+2 字节）
    recv_buffer.erase(0, crlf_pos + 2);
    std::cout << "[Request] parseRequestLine: 删除请求行后，recv_buffer 剩余长度 = " << recv_buffer.size() << " 字节" << std::endl;

    std::cout << "[Request] 请求行解析成功：" << basic_.method << " " << basic_.path << " " << basic_.version << std::endl;
    // 标记请求行已解析（后续不再重复解析）
    state_.request_line_parsed = true;
    return true;
}

bool Request::parseHeadersFromBuffer()
{
    size_t header_end = recv_buffer.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        std::cout << "[Request] parseHeaders: 未找到请求头结束标记（\\r\\n\\r\\n）" << std::endl;
        return false;
    }

    std::istringstream stream(recv_buffer.substr(0, header_end));
    std::string line;
    std::getline(stream, line); // 跳过请求行

    while (std::getline(stream, line))
    {
        if (line.back() == '\r')
            line.pop_back();
        size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos)
            continue;

        std::string key = toLowerCopy(line.substr(0, colon_pos));
        std::string val = line.substr(colon_pos + 1);
        val.erase(0, val.find_first_not_of(" "));
        content_.headers[key] = val;

        // 关键日志：输出解析到的 Content-Length
        if (key == "content-length")
        {
            try
            {
                state_.expected_body_size = std::stoul(val);
                std::cout << "[Request] parseHeaders: 解析到 Content-Length = " << state_.expected_body_size << "（原始值：" << val << "）" << std::endl;
            }
            catch (...)
            {
                state_.expected_body_size = 0;
                std::cout << "[Request] parseHeaders: Content-Length 解析失败（原始值：" << val << "）" << std::endl;
            }
        }
    }

    // 确认清理请求头后的 recv_buffer 长度
    clearProcessed(header_end + 4);
    std::cout << "[Request] parseHeaders: 清理请求头后，recv_buffer 长度 = " << recv_buffer.size() << std::endl;
    return true;
}

void Request::parseMultipartData()
{
    std::cout << "[Multipart] processing, state=" << static_cast<int>(multipart_.state)
              << ", buffer size: " << recv_buffer.size() << std::endl;

    bool progress = true;
    int loop_count = 0;
    while (progress && multipart_.state != MultipartState::END && loop_count < 10) // 防止无限循环
    {
        loop_count++;
        std::cout << "[Multipart] Loop " << loop_count << ", state: " << static_cast<int>(multipart_.state) << std::endl;

        switch (multipart_.state)
        {
        case MultipartState::START:
            progress = handleMultipartStart();
            break;
        case MultipartState::READING_HEADERS:
            progress = handleMultipartHeaders();
            break;
        case MultipartState::READING_BODY:
            progress = handleMultipartBody();
            break;
        case MultipartState::END:
            progress = false;
            break;
        }

        std::cout << "[Multipart] Buffer size after loop " << loop_count << ": " << recv_buffer.size() << std::endl;
    }

    if (loop_count >= 10)
    {
        std::cout << "[Multipart] WARNING: Exited loop due to count limit" << std::endl;
    }
}

bool Request::handleMultipartStart()
{
    if (multipart_.raw_boundary.empty())
    {
        std::string contentType = getHeader("content-type");
        size_t pos = contentType.find("boundary=");
        if (pos != std::string::npos)
        {
            std::string boundary_str = contentType.substr(pos + 9);

            // 清理边界字符串
            size_t end_pos = multipart_.raw_boundary.find(';');
            if (end_pos != std::string::npos)
            {
                boundary_str = boundary_str.substr(0, end_pos);
            }

            // 去除可能的引号
            if (boundary_str.size() >= 2 && boundary_str[0] == '"' && boundary_str[boundary_str.size() - 1] == '"')
            {
                boundary_str = boundary_str.substr(1, boundary_str.size() - 2);
            }
            setBoundary(boundary_str);
            std::cout << "[Multipart] Extracted boundary: '" << boundary_str << "'" << std::endl;
        }
        else
        {
            std::cout << "[Multipart] ERROR: No boundary found" << std::endl;
            return false;
        }
    }

    // 直接切换到HEADERS状态，让HEADERS状态处理边界
    multipart_.state = MultipartState::READING_HEADERS;
    std::cout << "[Multipart] Switching to HEADERS state" << std::endl;
    return true;
}

// 处理上传文件请求头
bool Request::handleMultipartHeaders()
{
    // 修复：检查起始边界（没有\r\n前缀）
    std::string start_boundary = multipart_.boundary_marker; // "--boundary"

    if (recv_buffer.compare(0, start_boundary.size(), start_boundary) == 0)
    {
        std::cout << "[Multipart] Found START boundary at beginning of buffer" << std::endl;
        clearProcessed(start_boundary.size());

        // 跳过可能的换行
        while (!recv_buffer.empty() && (recv_buffer[0] == '\r' || recv_buffer[0] == '\n'))
        {
            recv_buffer.erase(0, 1);
        }
    }
    // 检查普通边界（有\r\n前缀）
    else
    {
        std::string normal_boundary = "\r\n" + multipart_.boundary_marker;
        if (recv_buffer.compare(0, normal_boundary.size(), normal_boundary) == 0)
        {
            std::cout << "[Multipart] Found NORMAL boundary at start of buffer" << std::endl;
            clearProcessed(normal_boundary.size());

            // 跳过可能的换行
            while (!recv_buffer.empty() && (recv_buffer[0] == '\r' || recv_buffer[0] == '\n'))
            {
                recv_buffer.erase(0, 1);
            }
        }
    }

    // 现在查找头部结束标记
    size_t end = recv_buffer.find("\r\n\r\n");
    std::cout << "[Multipart] Looking for header end in " << recv_buffer.size() << " bytes" << std::endl;

    if (end == std::string::npos)
    {
        std::cout << "[Multipart] Headers not complete yet" << std::endl;
        return false;
    }

    std::string headers_part = recv_buffer.substr(0, end);
    std::cout << "[Multipart] Complete headers:\n"
              << headers_part << std::endl;

    parsePartHeaders(headers_part);
    clearProcessed(end + 4);

    if (multipart_.ofs.is_open())
    {
        std::cout << "[Multipart] File opened successfully, switching to READING_BODY" << std::endl;
        multipart_.state = MultipartState::READING_BODY;
    }
    else
    {
        std::cout << "[Multipart] ERROR: File failed to open" << std::endl;
        return false;
    }
    return true;
}

// 解析请求头
void Request::parsePartHeaders(const std::string &headers_part)
{
    std::istringstream ss(headers_part);
    std::string line;
    while (std::getline(ss, line))
    {
        // 找到包含 filename= 的行（Content-Disposition 头）
        size_t filename_pos = line.find("filename=");
        if (filename_pos != std::string::npos)
        {
            // 1. 正确计算起始位置：filename= 长度为9，从 pos+9 开始
            size_t val_start = filename_pos + 9;
            if (val_start >= line.size())
            {
                std::cout << "[Multipart] 警告：filename= 后无内容" << std::endl;
                multipart_.uploaded_filename.clear();
                continue;
            }

            // 2. 处理引号（大部分客户端会用双引号包裹文件名）
            std::string filename_val = line.substr(val_start);
            size_t quote_start = filename_val.find('"');
            size_t quote_end = filename_val.rfind('"');

            // 若有引号，提取引号之间的内容；若无，直接取整行
            if (quote_start != std::string::npos && quote_end != std::string::npos && quote_start < quote_end)
            {
                filename_val = filename_val.substr(quote_start + 1, quote_end - quote_start - 1);
            }

            // 3. 清理非法字符（只删路径相关非法字符，保留 . 和后缀）
            filename_val.erase(
                remove_if(filename_val.begin(), filename_val.end(),
                          [](char c)
                          {
                              // 非法路径字符：/ \ : * ? " < > | ，保留 . _ 等
                              return c == '/' || c == '\\' || c == ':' || c == '*' ||
                                     c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
                          }),
                filename_val.end());

            // 4. 清理不可见字符（\r \n 等）
            filename_val.erase(
                remove_if(filename_val.begin(), filename_val.end(),
                          [](char c)
                          { return c == '\r' || c == '\n'; }),
                filename_val.end());

            // 5. 赋值并打印日志（验证提取结果）
            multipart_.uploaded_filename = filename_val;
            std::cout << "[Multipart] 提取到原始文件名：" << multipart_.uploaded_filename << std::endl;

            // 后续生成唯一文件名、创建路径的逻辑不变（复用原有代码）
            std::string clean_filename = multipart_.uploaded_filename;
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now.time_since_epoch())
                                 .count();
            std::string unique_name = std::to_string(timestamp) + "_" + clean_filename;

            std::string base_path = "/home/zhangsan/~projects/video_system/server";
            std::string uploads_dir = base_path + "/uploads";
            std::filesystem::create_directories(uploads_dir);

            multipart_.upload_file_path = uploads_dir + "/" + unique_name;

            multipart_.ofs.open(multipart_.upload_file_path, std::ios::binary);
            if (multipart_.ofs.is_open())
            {
                std::cout << "[Multipart] 打开文件成功：" << multipart_.upload_file_path
                          << "（原始文件名：" << clean_filename << "）" << std::endl;
            }
            else
            {
                std::cout << "[Multipart] 错误：打开文件失败：" << multipart_.upload_file_path
                          << "，错误信息：" << strerror(errno) << std::endl;
            }
        }
    }
}

// 处理请求数据
bool Request::handleMultipartBody()
{
    std::cout << "[Multipart] handleMultipartBody called, buffer size: " << recv_buffer.size() << std::endl;

    // 修复：使用正确的边界标记格式
    // 在body中，边界前面有 \r\n
    std::string boundary_marker = "\r\n" + multipart_.boundary_marker;
    std::string end_boundary_marker = "\r\n" + multipart_.end_boundary_marker;

    const size_t boundary_len = boundary_marker.size();
    const size_t end_boundary_len = end_boundary_marker.size();
    const size_t max_boundary_len = std::max(boundary_len, end_boundary_len);

    std::cout << "[Multipart] Looking for boundary: " << boundary_marker << std::endl;

    // 优化：使用高效的边界查找
    size_t boundaryPos = findBoundaryOptimized(recv_buffer, boundary_marker);
    size_t endBoundaryPos = findBoundaryOptimized(recv_buffer, end_boundary_marker);

    // 优先检查结束边界
    if (endBoundaryPos != std::string::npos)
    {
        std::cout << "[Multipart] Found END boundary at position: " << endBoundaryPos << std::endl;

        // 写入结束边界之前的数据
        if (multipart_.ofs.is_open() && endBoundaryPos > 0)
        {
            writeFileData(recv_buffer.data(), endBoundaryPos);
        }

        // 关闭文件并完成请求
        if (multipart_.ofs.is_open())
        {
            multipart_.ofs.close();
        }

        multipart_.state = MultipartState::END;
        finalizeMultipart();
        clearProcessed(endBoundaryPos + end_boundary_len);
        return true;
    }

    // 检查普通边界
    if (boundaryPos != std::string::npos)
    {
        std::cout << "[Multipart] Found boundary at position: " << boundaryPos << std::endl;

        // 写入边界之前的数据
        if (multipart_.ofs.is_open() && boundaryPos > 0)
        {
            writeFileData(recv_buffer.data(), boundaryPos);
        }

        // 清掉到边界为止的数据
        clearProcessed(boundaryPos + boundary_len);

        // 切换到HEADERS状态处理下一个部分
        multipart_.state = MultipartState::READING_HEADERS;
        return true;
    }

    // 没有找到边界，写入可安全写入的数据
    // 优化：保留足够的尾部数据用于边界检查
    size_t keep_tail = max_boundary_len + 32; // 多保留一些字节
    size_t writable_size = recv_buffer.size() > keep_tail ? recv_buffer.size() - keep_tail : 0;

    std::cout << "[Multipart] No boundary found, writable_size: " << writable_size
              << ", keeping tail: " << keep_tail << std::endl;

    if (multipart_.ofs.is_open() && writable_size > 0)
    {
        writeFileData(recv_buffer.data(), writable_size);
        clearProcessed(writable_size);
    }
    else if (writable_size == 0 && recv_buffer.size() > 0)
    {
        // 缓冲区太小，无法安全写入，等待更多数据
        std::cout << "[Multipart] Buffer too small for safe write, waiting for more data" << std::endl;
    }

    return false;
}

size_t Request::findBoundaryOptimized(const std::string &buffer, const std::string &pattern)
{
    if (buffer.empty() || pattern.empty())
    {
        return std::string::npos;
    }

    size_t n = buffer.size();
    size_t m = pattern.size();

    if (n < m)
    {
        return std::string::npos;
    }

    // Boyer-Moore 坏字符表
    std::vector<int> badChar(256, -1);
    for (size_t i = 0; i < m; i++)
    {
        badChar[static_cast<unsigned char>(pattern[i])] = i;
    }

    size_t s = 0;
    while (s <= n - m)
    {
        int j = m - 1;

        // 从右向左匹配
        while (j >= 0 && pattern[j] == buffer[s + j])
        {
            j--;
        }

        if (j < 0)
        {
            return s; // 找到匹配
        }
        else
        {
            // 使用坏字符规则跳过
            char mismatch_char = buffer[s + j];
            int char_pos = badChar[static_cast<unsigned char>(mismatch_char)];
            s += std::max(1, j - char_pos);
        }
    }

    return std::string::npos;
}

void Request::writeFileData(const char *data, size_t len)
{
    multipart_.ofs.write(data, len);
    state_.body_received += len;
    std::cout << "[Multipart] Wrote " << len << " bytes, total: " << state_.body_received << " bytes" << std::endl;
}

void Request::finalizeMultipart()
{
    state_.request_complete = true;
    if (multipart_.ofs.is_open())
    {
        multipart_.ofs.close();
    }
    multipart_.raw_boundary.clear(); // 重置边界，避免影响下一次请求（如果是Keep-Alive）
    std::cout << "[Multipart] Finalized, all parts processed" << std::endl;
}

void Request::clearProcessed(size_t pos)
{
    if (pos > 0 && pos <= recv_buffer.size())
        recv_buffer.erase(0, pos);
}

void Request::setBoundary(const std::string &boundary_str)
{
    multipart_.raw_boundary = boundary_str;

    // 修复：正确的边界标记格式
    multipart_.boundary_marker = "--" + boundary_str;            // 用于分隔部分
    multipart_.end_boundary_marker = "--" + boundary_str + "--"; // 用于结束

    std::cout << "[Multipart] Boundary set: raw='" << multipart_.raw_boundary
              << "', marker='" << multipart_.boundary_marker
              << "', end_marker='" << multipart_.end_boundary_marker << "'" << std::endl;
}

void Request::resetRequestLineParsed()
{
    state_.request_line_parsed = false;
}

void Request::setParam(const std::string &param_name, const std::string &param_value)
{
    state_.params_[param_name] = param_value;
}

void Request::extractPathParam(const std::string &prefix, const std::string &param_name)
{
    if (basic_.path.size() > prefix.size() && basic_.path.substr(0, prefix.size()) == prefix)
    {
        std::string param_value = basic_.path.substr(prefix.size());
        setParam(param_name, param_value);
    }
    else
    {
        setParam(param_name, "");
    }
}

std::string Request::getPathParam(const std::string &param_name) const
{
    auto it = state_.params_.find(param_name);
    return (it != state_.params_.end() ? it->second : "");
}

void Request::parseQueryParams()
{
    basic_.query_params.clear(); // 清空现有参数
    if (basic_.query_string.empty())
    {
        return; // 无查询参数，直接返回
    }

    size_t start = 0;
    const size_t len = basic_.query_string.size();

    // 遍历整个query_string_，按"&"分割参数
    while (start < len)
    {
        // 找到当前参数的结束位置（下一个"&"或字符串末尾）
        size_t end = basic_.query_string.find("&", start);
        if (end == std::string::npos)
        {
            end = len; // 最后一个参数，结束位置为字符串长度
        }

        // 提取单个参数（如"a=1"）
        const std::string param = basic_.query_string.substr(start, end - start);
        if (!param.empty())
        { // 跳过空参数（如"a=1&&b=2"中的空串）
            // 解析键值对（分割"="）
            size_t eq_pos = param.find("=");
            if (eq_pos == std::string::npos)
            {
                // 无值参数（如"debug"）
                basic_.query_params[param] = "";
            }
            else
            {
                std::string key = param.substr(0, eq_pos);
                std::string value = param.substr(eq_pos + 1);
                basic_.query_params[key] = value;
            }
        }

        // 移动到下一个参数的起始位置
        start = end + 1;
    }
}

std::string Request::getQueryParam(const std::string &key) const
{
    auto it = basic_.query_params.find(key);
    if (it != basic_.query_params.end())
    {
        std::string query = it->second;
        return query;
    }
    return "";
}

// 获取JSON中的字符串参数（带默认值）
std::string Request::getJsonString(const std::string &key, const std::string &defaultVal) const
{
    if (content_.json_body.contains(key) && content_.json_body[key].is_string())
    {
        return content_.json_body[key].get<std::string>();
    }
    return defaultVal;
}

// 获取JSON中的整数参数（带默认值）
int Request::getJsonInt(const std::string &key, int defaultVal) const
{
    if (content_.json_body.contains(key) && content_.json_body[key].is_number_integer())
    {
        return content_.json_body[key].get<int>();
    }
    return defaultVal;
}

// 获取JSON中的嵌套对象（比如 parameters）
nlohmann::json Request::getJsonObject(const std::string &key) const
{
    if (content_.json_body.contains(key) && content_.json_body[key].is_object())
    {
        return content_.json_body[key];
    }
    return nlohmann::json::object(); // 返回空对象
}

// 判断请求体是否为JSON格式且已解析
bool Request::isJsonParsed() const
{
    return !content_.json_body.is_null();
}

bool Request::isRequestLineParsed() const
{
    std::lock_guard<std::mutex> lock(buffer_mutex); // 若需要线程安全
    return state_.request_line_parsed;
}

std::string Request::getUploadedFilePath() const
{
    std::lock_guard<std::mutex> lock(buffer_mutex);
    return multipart_.upload_file_path; // 注意：是multipart_结构体中的成员
}

std::string Request::getUploadedFilename() const
{
    std::lock_guard<std::mutex> lock(buffer_mutex);
    return multipart_.uploaded_filename; // 同上
}

std::string Request::toLowerCopy(const std::string &s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c)
                   { return std::tolower(c); });
    return r;
}