#include "server/Server.hpp"
#include "server/VideoServerAdapter.hpp"
#include <csignal>

std::mutex log_mutex;
Server *Server::instance_ = nullptr; // 初始化静态指针
Server::Server(int port, std::weak_ptr<VideoServerAdapter> adapter, int thread_num) : port_(port), server_socket(), epoll_(), thread_pool_(thread_num), conn_manager_(), is_runing_(false), adapter_(adapter)
{
    // 先创建 epoll 实例
    if (!epoll_.create())
    {
        std::cerr << "Server init failed: epoll create failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    // 初始化 server socket
    if (!server_socket.create())
    {
        perror("socket create failed");
        exit(EXIT_FAILURE);
    }

    if (!server_socket.bind(port_))
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (!server_socket.listen())
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    // 把监听 socket 设置为非阻塞
    if (!server_socket.setNonBlocking())
    {
        perror("setNonBlocking listen socket failed");
        exit(EXIT_FAILURE);
    }

    // 3. 给 server socket 加 epoll 事件（ET 模式）
    if (!epoll_.addFd(server_socket.getFd(), EPOLLIN | EPOLLET))
    {
        perror("epoll add server socket failed");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server constructed, listening on port " << port_ << std::endl;
}

Server::~Server()
{
    stop();
}

void Server::start()
{
    initSignalHandler();
    is_runing_ = true;
    int normal_timeout_sec = 30;
    int processing_timeout_sec = 300;
    std::cout << "epfd = " << epoll_.getFd() << std::endl;
    std::cout << "Server is running on port " << port_ << std::endl;

    const int MAX_EVENTS = 4096; // 单次最多处理4096个事件
    // 主循环
    while (is_runing_)
    {
        std::cout << "[EventLoop] Waiting for events... epfd=" << epoll_.getFd() << std::endl;
        // 等待事件，3000ms 超时以便周期性做维护（如超时清理）
        std::vector<epoll_event> events = epoll_.wait(MAX_EVENTS, -1);

        if (events.empty())
        {
            std::cout << "[EventLoop] epoll.wait returned empty (errno=" << errno << ")" << std::endl;
            if (errno == EINTR && !is_runing_)
                break;
            continue;
        }
        std::cout << "[EventLoop] Received " << events.size() << " events" << std::endl;
        for (const auto &ev : events)
        {
            int fd = ev.data.fd;
            uint32_t event_type = ev.events;
            // 新增：打印触发事件的fd和事件类型
            std::cout << "[EventLoop] Event triggered: fd=" << fd
                      << ", events=0x" << std::hex << event_type << std::dec << std::endl;
            if (fd == server_socket.getFd())
            {
                std::cout << "[EventLoop] Listen socket (fd=" << fd << ") has new connection request" << std::endl;
                // 监听 socket 有新连接（ET 模式下需要循环 accept）
                handleNewConnection();
                continue;
            }
            else
            {
                auto conn = conn_manager_.get(ev.data.fd);
                if (!conn || !conn->isValid())
                {
                    // 连接不存在（可能刚被移除），确保 epoll 清理
                    epoll_.delFd(ev.data.fd);
                    continue;
                }

                // 优先处理读事件
                if (event_type & (EPOLLIN | EPOLLPRI | EPOLLHUP | EPOLLERR))
                {
                    handleReadEvent(conn);
                }
                else if (event_type & EPOLLOUT)
                {
                    handleWriteEvent(conn);
                }
            }
        }
        std::vector<std::shared_ptr<Connection>> close_conns = conn_manager_.clear_timeout_connections(normal_timeout_sec, processing_timeout_sec);
        for (auto &conn : close_conns)
        {
            if (conn->getState() != ConnectionState::PROCESSING)
            {
                int fd = conn->getFd();
                epoll_.delFd(fd);
                conn->close();
                // 注意：conn_manager_.remove(fd) 已经在 clear_timeout_connections 中调用
                std::cout << "[FD=" << fd << "] connection timeout, removed" << std::endl;
            }
            else
            {
                std::cout << "[FD=" << conn->getFd() << "] skip timeout cleanup (processing)" << std::endl;
            }
        }
    }
    // 2. 循环退出后，清理资源（关键：避免资源泄漏）
    std::cout << "Cleaning up server resources..." << std::endl;
    close(server_socket.getFd()); // 关闭监听Socket
    epoll_.close();               // 关闭epoll实例（需实现Epoll::close()）
    std::cout << "Server shut down successfully" << std::endl;
}

// 信号处理函数（静态）：收到SIGINT时设置is_runing_为false
void Server::handleSignal(int sig)
{
    if (sig == SIGINT && instance_ != nullptr)
    {
        std::cout << "\nReceived signal: " << sig << ", shutting down server..." << std::endl;
        instance_->is_runing_ = false; // 关键：设置退出标志
    }
}

// 初始化信号处理：注册SIGINT的处理函数
void Server::initSignalHandler()
{
    instance_ = this; // 将当前Server实例赋值给静态指针
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = Server::handleSignal; // 绑定信号处理函数
    // 屏蔽其他信号，避免处理SIGINT时被其他信号中断
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    // 注册SIGINT信号的处理
    if (sigaction(SIGINT, &sa, nullptr) == -1)
    {
        throw std::runtime_error("Failed to register signal handler: " + std::string(strerror(errno)));
    }
}

void Server::stop()
{
    if (!is_runing_)
        return;
    is_runing_ = false;

    // 删除监听 fd
    epoll_.delFd(server_socket.getFd());
    server_socket.close();
    std::cout << "Server stopped" << std::endl;
}

void Server::handleNewConnection()
{
    std::cout << "[handleNewConnection] Start accepting new connections..." << std::endl;
    for (;;)
    {
        struct sockaddr_in client_addr;
        int client_fd = server_socket.accept(&client_addr);
        // 新增：打印accept返回值
        std::cout << "[handleNewConnection] accept returned fd=" << client_fd
                  << ", errno=" << errno << std::endl;
        if (client_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                std::cout << "[handleNewConnection] No more new connections" << std::endl;
                // 没有更多连接
                break;
            }
            else
            {
                perror("accept");
                break;
            }
        }

        // 打印客户端信息
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        std::cout << "New connection from " << ip_str << ":" << ntohs(client_addr.sin_port)
                  << ", fd=" << client_fd << std::endl;

        // 设置 client_fd 非阻塞（非常重要）
        if (!server_socket.setNonBlocking(client_fd))
        {
            std::cerr << "Warning: setNonBlocking(client_fd) failed for fd=" << client_fd << std::endl;
            ::close(client_fd);
            continue;
        }

        // 将新 fd 加入 epoll（ET + ONESHOT）
        if (!epoll_.addFd(client_fd, EPOLLIN | EPOLLET | EPOLLONESHOT))
        {
            std::cerr << "epoll add client fd failed: " << client_fd << std::endl;
            ::close(client_fd);
            continue;
        }

        // 把连接加入管理器
        conn_manager_.add(client_fd, client_addr);
    }
}

void Server::handleReadEvent(std::shared_ptr<Connection> conn)
{
    int fd = conn->getFd();

    // ✅ 双重检查连接有效性
    auto current_conn = conn_manager_.get(fd);
    if (!current_conn || current_conn != conn || !conn->isValid())
    {
        std::cout << "[FD=" << fd << "] Connection invalid in handleReadEvent" << std::endl;
        return;
    }

    // ✅ 严格的状态验证
    if (conn->getState() != ConnectionState::READING)
    {
        std::cout << "[FD=" << fd << "] Invalid state: " << static_cast<int>(conn->getState())
                  << ", expected READING(1)" << std::endl;
        // 尝试恢复状态
        if (conn->getState() == ConnectionState::NEW)
        {
            conn->setState(ConnectionState::READING);
            std::cout << "[FD=" << fd << "] Recovered state from NEW to READING" << std::endl;
        }
        else
        {
            return;
        }
    }

    char buf[65536];

    Request *req_ptr = conn->request();
    if (!req_ptr)
    {
        std::cout << "[FD=" << fd << "] Request 对象为空，关闭连接" << std::endl;
        epoll_.delFd(fd);
        conn->close();
        conn_manager_.remove(fd);
        return;
    }
    Request &req = *req_ptr;
    // 添加读取超时保护
    auto read_start = std::chrono::steady_clock::now();
    bool read_again = true;

    while (read_again)
    {
        read_again = false;
        size_t max_read = sizeof(buf); // 默认64KB
        // 防止无限循环
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - read_start);
        if (duration.count() > 5000)
        { // 5秒超时
            std::cout << "[FD=" << fd << "] Read operation timeout" << std::endl;
            break;
        }

        if (!req.isRequestLineParsed())
        {
            // 未解析请求行：只读取8192字节（足够解析请求行，避免接收大量文件）
            max_read = 8192;
            // 额外保护：已接收超过16KB仍未解析，直接判定为非法请求
            if (req.getTotalReceived() >= 16384)
            {
                std::cout << "[FD=" << fd << "] 非法请求：请求行超过16KB，关闭连接" << std::endl;
                epoll_.delFd(fd);
                conn->close();
                conn_manager_.remove(fd);
                return;
            }
        }
        // 读取数据（使用限制后的 read_size）
        ssize_t n = ::read(fd, buf, max_read);
        if (n > 0)
        {
            conn->updateActiveTime();
            // 强制校验请求行解析状态和 path 有效性
            bool need_parse = false;
            req.appendToBuffer(buf, static_cast<size_t>(n));
            req.addReceivedSize(static_cast<size_t>(n));

            // 请求行未解析 → 需要解析
            if (!req.isRequestLineParsed())
            {
                need_parse = true;
            }
            // 情况2：请求行已解析，但 path 为空 → 强制重新解析
            else if (req.getPath().empty())
            {
                std::cout << "[FD=" << fd << "] 检测到空 path，强制重新解析请求行" << std::endl;
                req.resetRequestLineParsed();
                need_parse = true;
            }
            // 情况3：请求行已解析且 path 非空 → 无需解析
            else
            {
                need_parse = false; // 正常情况不重复解析
            }

            if (need_parse)
            {
                bool parsed = req.parseRequestLine();
                if (!parsed)
                {
                    // 数据不足，重新注册读事件等待更多数据
                    epoll_.modFd(fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
                    return; // 用 return 而非 break，避免继续执行后续逻辑
                }
                else
                {
                    std::cout << "[FD=" << fd << "] 请求行解析成功，二次调用 appendToBuffer 处理请求头" << std::endl;
                    req.appendToBuffer("", 0); // 关键：传空数据，仅触发后续处理逻辑
                }
            }
            // 路由判断（第一次仅判断路由是否有效）
            if (req.isRequestLineParsed() && !req.isRouteChecked())
            {
                Response check_resp;
                auto adapter = adapter_.lock();
                if (!adapter->dispatchRequest(req, check_resp, true))
                {
                    conn->safeSend(check_resp.toString().data(), check_resp.toString().size());
                    conn->setState(ConnectionState::WRITING);
                    conn->markForCloseAfterWrite();
                    epoll_.modFd(fd, EPOLLOUT | EPOLLET | EPOLLONESHOT);
                    req.setRouteChecked(true);
                    return;
                }
                else
                {
                    // 适配器为空（异常情况），返回500
                    check_resp.setStatusCode(500);
                    check_resp.setBody("Server adapter not available");
                }
                req.setRouteChecked(true);
                std::cout << "[Server] 路由有效，继续接收请求体" << std::endl;
            }
            // 添加 100 Continue 响应
            if (!conn->is100ContinueSent())
            {
                std::string expect_header = req.getHeader("expect");
                std::cout << "[FD=" << fd << "] Expect header: '" << expect_header << "'" << std::endl;

                if (!expect_header.empty())
                {
                    // 转为纯小写，避免大小写匹配问题
                    std::string lower_expect = expect_header;
                    std::transform(lower_expect.begin(), lower_expect.end(), lower_expect.begin(), ::tolower);

                    // 判断是否有 body 待上传
                    std::string content_length = req.getHeader("content-length");
                    std::string content_type = req.getHeader("content-type");
                    bool has_body = false;

                    // 处理 Content-Length（带容错）
                    if (!content_length.empty())
                    {
                        try
                        {
                            has_body = (std::stoul(content_length) > 0);
                        }
                        catch (...)
                        {
                            has_body = true; // 解析失败时默认视为有 body
                            std::cout << "[FD=" << fd << "] 警告：Content-Length 格式无效" << std::endl;
                        }
                    }

                    // 补充 multipart 类型判断（即使 Content-Length 为 0 也可能有 body）
                    if (content_type.find("multipart/form-data") != std::string::npos)
                    {
                        has_body = true;
                    }

                    // 满足条件则发送 100 Continue
                    if (lower_expect.find("100-continue") != std::string::npos && has_body)
                    {
                        std::string response = "HTTP/1.1 100 Continue\r\n\r\n";
                        ssize_t sent = ::send(fd, response.c_str(), response.length(), MSG_NOSIGNAL);
                        conn->set100ContinueSent(true);

                        if (sent > 0)
                        {
                            std::cout << "[FD=" << fd << "] Sent 100 Continue" << std::endl;
                        }
                        else
                        {
                            std::cout << "[FD=" << fd << "] 发送 100 Continue 失败：" << strerror(errno) << std::endl;
                        }
                    }
                }
            }
            std::cout << "[FD=" << fd << "] Read " << n << " bytes, total=" << req.getTotalReceived()
                      << ", buffer_has_data=" << !req.getRecvBuffer().empty()
                      << ", is_complete=" << (req.isRequestComplete() ? "true" : "false") << std::endl;
            // 若请求已完整，直接退出循环；否则允许再次读取（避免遗漏数据）
            if (req.isRequestComplete())
            {
                std::cout << "[FD=" << fd << "] Request complete, breaking read loop" << std::endl;
                break;
            }
            else
            {
                // 检查是否还有数据可读（避免无限循环）
                auto now = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - read_start);
                if (duration.count() < 5000)
                { // 5秒内允许再次读取
                    read_again = true;
                }
            }
        }
        else if (n == 0)
        {
            std::cout << "[FD=" << fd << "] Client closed connection" << std::endl;
            epoll_.delFd(fd);
            conn->close();
            conn_manager_.remove(fd);
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                std::cout << "[FD=" << fd << "] No more data available" << std::endl;
                break;
            }
            else
            {
                std::cout << "[FD=" << fd << "] Read error: " << strerror(errno) << std::endl;
                epoll_.delFd(fd);
                conn->close();
                conn_manager_.remove(fd);
                return;
            }
        }
    }
    if (req.isRequestComplete() && req.getTotalReceived() > 0)
    {
        // ✅ 再次验证连接状态
        current_conn = conn_manager_.get(fd);
        if (!current_conn || current_conn != conn)
        {
            std::cout << "[FD=" << fd << "] Connection removed during read" << std::endl;
            return;
        }

        // ✅ 设置处理状态
        conn->setState(ConnectionState::PROCESSING);
        std::cout << "[FD=" << fd << "][handleReadEvent] Setting state to PROCESSING" << std::endl;

        // ✅ 禁用 epoll 事件
        if (!epoll_.modFd(fd, 0))
        {
            std::cout << "[FD=" << fd << "][handleReadEvent] Failed to disable epoll events, closing connection" << std::endl;
            epoll_.delFd(fd);
            conn->close();
            conn_manager_.remove(fd);
            return;
        }
        std::cout << "[FD=" << fd << "] handleReadEvent: 请求已完整，提交到线程池" << std::endl;
        // ✅ 提交到线程池
        thread_pool_.enqueue([this, conn]()
                             { this->processRequest(conn); });

        std::cout << "[FD=" << fd << "][handleReadEvent] Successfully submitted to thread pool" << std::endl;
    }
    else
    {
        // 请求未完整：重新注册读事件，等待后续数据
        std::cout << "[FD=" << fd << "][handleReadEvent] Request not complete, total_read=" << req.getTotalReceived()
                  << ", will re-register read event" << std::endl;

        // ✅ 重新注册读事件
        if (!epoll_.modFd(fd, EPOLLIN | EPOLLET | EPOLLONESHOT))
        {
            std::cout << "[FD=" << fd << "][handleReadEvent] Failed to re-register read events" << std::endl;
            epoll_.delFd(fd);
            conn->close();
            conn_manager_.remove(fd);
        }
    }
}

// 在工作线程中处理请求（会准备好 send_buf）
void Server::processRequest(std::shared_ptr<Connection> conn)
{
    if (!conn || !conn->isValid())
        return;

    int fd = conn->getFd();
    std::cout << "[Worker " << std::this_thread::get_id()
              << "] processRequest FD=" << fd
              << ", state=" << static_cast<int>(conn->getState()) << std::endl;

    // 在处理前检查连接是否仍然在管理器中
    auto current_conn = conn_manager_.get(fd);
    if (!current_conn || current_conn != conn)
    {
        std::lock_guard<std::mutex> lg(log_mutex);
        std::cout << "[Worker " << std::this_thread::get_id()
                  << "] Connection removed before processing, FD=" << fd << std::endl;
        return;
    }
    const std::string msg = conn->recv_buf_copy();
    // 添加详细的请求分析
    std::cout << "[Worker " << std::this_thread::get_id()
              << "] Processing request, first 100 chars: "
              << msg.substr(0, std::min(msg.size(), 100ul)) << std::endl;

    bool is_http = false;
    auto &req = *conn->request();
    auto &resp = *conn->response();
    if (!req.getMethod().empty())
        is_http = true;
    {
        std::cout << "[Worker " << std::this_thread::get_id() << "] FD=" << conn->getFd()
                  << ", method=" << req.getMethod()
                  << ", path=" << req.getPath()
                  << ", version=" << req.getVersion()
                  << ", Connection header=" << req.getHeader("Connection")
                  << ", isKeepAlive=" << req.isKeepAlive() << std::endl;
    }

    if (is_http)
    {
        bool ok = req.isRequestComplete();
        if (!ok)
        {
            std::cout << "[Worker " << std::this_thread::get_id()
                      << "] Request parse failed" << std::endl;
            resp.setStatusCode(400);
            resp.setHeader("Content-Type", "text/plain");
            resp.setBody("Bad Request");
            conn->setKeepAlive(false);
        }
        else
        {
            // 简单路由
            std::string path = req.getPath();
            std::string method = req.getMethod();
            // 转换为shared_ptr
            auto adapter = adapter_.lock();
            if (!adapter)
            {
                resp.setStatusCode(500);
                resp.setHeader("Content-Type", "text/plain");
                resp.setBody("Server Adapter Not Available");
                conn->setKeepAlive(false);
            }
            else
            { // 调用 dispatchRequest，获取路由匹配结果
                // 显式调用，无论返回值如何，确保路由处理逻辑执行
                bool route_found = adapter->dispatchRequest(req, resp, false);
                if (route_found)
                {
                    std::cout << "[Worker " << std::this_thread::get_id() << "] route handle success" << std::endl;
                }
                else
                {
                    // 这里可以补充未匹配时的日志（可选，因为dispatchRequest已设置响应）
                    std::cout << "[Worker " << std::this_thread::get_id() << "] route not found (handled by dispatch)" << std::endl;
                }
            }
        }
        // 设置 Connection 头和 keep-alive 标志
        if (req.isKeepAlive())
        {
            conn->set100ContinueSent(false);
            resp.setHeader("Connection", "keep-alive");
            conn->setKeepAlive(true);
        }
        else
        {
            resp.setHeader("Connection", "close");
            conn->setKeepAlive(false);
        }
    }
    else
    {
        // 非 HTTP 简单 echo
        resp.setStatusCode(200);
        resp.setHeader("Content-Type", "text/plain");
        resp.setBody("Echo: " + msg);
        conn->setKeepAlive(false);
    }

    // 将响应写入 send_buf
    std::string resp_str = resp.toString();
    conn->append_send(resp_str.data(), resp_str.size());
    conn->setState(ConnectionState::WRITING);

    current_conn = conn_manager_.get(fd);
    if (!current_conn || current_conn != conn)
    {
        std::lock_guard<std::mutex> lg(log_mutex);
        std::cout << "[Worker " << std::this_thread::get_id()
                  << "] Connection removed during processing, FD=" << fd << std::endl;
        return;
    }

    // 注册写事件，必须包含 EPOLLONESHOT
    if (!epoll_.modFd(fd, EPOLLOUT | EPOLLET | EPOLLONESHOT))
    {
        std::lock_guard<std::mutex> lg(log_mutex);
        std::cout << "[Worker " << std::this_thread::get_id()
                  << "] modFd failed, removing FD=" << fd << std::endl;
        auto removed_conn = conn_manager_.remove(fd);
        if (removed_conn)
            removed_conn->close();
    }
    else
    {
        std::cout << "[Epoll] modFd success: fd=" << fd << ", events=EPOLLOUT|EPOLLONESHOT" << std::endl;
    }
    if (!conn->isValid())
    {
        std::cout << "[Worker] fd invalid before modFd" << std::endl;
        return;
    }
}

// 写事件处理：把 send_buf 尽量写完
void Server::handleWriteEvent(std::shared_ptr<Connection> conn)
{
    int fd = conn->getFd();
    int code = conn->response()->getStatusCode();
    // 检查连接有效性和状态
    if (!conn->canWrite())
    {
        std::cout << "[FD=" << fd << "] Invalid state for write, state="
                  << static_cast<int>(conn->getState()) << std::endl;
        return;
    }

    auto current_conn = conn_manager_.get(fd);
    if (!current_conn || current_conn != conn)
    {
        std::cout << "[FD=" << fd << "] Connection invalid in handleWriteEvent" << std::endl;
        return;
    }
    std::string send_data = conn->send_buf_copy(); // 复制一份，以便在写过程中修改 send_buf
    if (send_data.empty())
    {
        // 没有要发送的数据：回到读取状态
        conn->setState(ConnectionState::READING);
        epoll_.modFd(fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
        return;
    }

    ssize_t total_sent = 0;
    ssize_t remaining = static_cast<ssize_t>(send_data.size());
    while (remaining > 0)
    {
        ssize_t n = conn->safeSend(send_data.data() + total_sent, static_cast<size_t>(remaining));
        if (n > 0)
        {
            total_sent += n;
            remaining -= n;
            conn->updateActiveTime();
            conn->addTotalSent(n);
        }
        else if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 把已经发送的部分从 conn 的 send buffer 删除
                conn->erase_sent(static_cast<size_t>(total_sent));
                // 写缓冲区满，稍后继续写，重新注册 EPOLLOUT
                epoll_.modFd(fd, EPOLLOUT | EPOLLET | EPOLLONESHOT);
                return;
            }
            else
            {
                // 写入错误：移除连接
                perror("write");
                epoll_.delFd(fd);
                conn->close();
                conn_manager_.remove(fd);
                return;
            }
        }
        else
        {
            // write 返回 0：通常不常见，安全地重试或关闭
            epoll_.modFd(fd, EPOLLOUT | EPOLLET | EPOLLONESHOT);
            conn->erase_sent(static_cast<size_t>(total_sent));
            return;
        }
    }
    // 修改发送完成日志
    std::cout << "[FD=" << fd << "] 全部发送完成，响应总大小=" << send_data.size()
              << "字节，累计发送=" << total_sent
              << "字节（" << (total_sent == send_data.size() ? "匹配" : "不匹配") << "）" << std::endl;
    // 全部数据已写出：从 send buffer 中擦除已发送数据
    conn->erase_sent(static_cast<size_t>(total_sent));
    conn->resetTotalSent();

    if (conn->closeAfterWrite())
    {
        // 强制关闭，无视 keep-alive
        conn_manager_.remove(fd);
        epoll_.delFd(fd);
        std::cout << "[FD=" << fd << "]" << code << "响应发送完成，强制关闭连接" << std::endl;
        conn->close();
    }
    else if (conn->getKeepAlive())
    {
        // 重置连接以复用
        conn->resetForKeepAlive();
        epoll_.modFd(fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
        std::cout << "[FD=" << fd << "] Keep-alive connection reset for next request" << std::endl;
    }
    else
    {
        // 安全关闭
        auto removed_conn = conn_manager_.remove(fd);
        if (removed_conn)
        {
            epoll_.delFd(fd);
            removed_conn->close();
            std::cout << "[FD=" << fd << "] Connection closed (no keep-alive)" << std::endl;
        }
    }
}
