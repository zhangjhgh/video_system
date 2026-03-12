#include "../../include/video/VideoFormatUnifier.hpp"

bool VideoFormatUnifier::unifyToMp4(const std::string &inputPath,
                                    std::string &outputPath,
                                    bool overwrite)
{
    // 检查输入合法性
    if (!checkInputValid(inputPath))
    {
        m_errorMsg = "输入文件不存在或无法访问";
        return false;
    }

    // 生成输出路径
    if (outputPath.empty())
    {
        outputPath = generateOutputPath(inputPath);
    }

    // 检查输出文件是否存在，是否需要覆盖
    if (!overwrite && std::filesystem::exists(outputPath))
    {
        m_errorMsg = "文件已存在，且未被允许覆盖";
        return false;
    }

    // 打开视频文件
    cv::VideoCapture cap(inputPath);
    if (!cap.isOpened())
    {
        m_errorMsg = "无法打开视频";
        return false;
    }

    int frameWidth = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int frameHeight = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);
    int totalFrame = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));

    if (frameWidth <= 0 || frameHeight <= 0 || fps <= 0)
    {
        m_errorMsg = "输入视频无效，可能已损坏";
        cap.release();
        return false;
    }

    // 尝试多种解码器
    std::vector<int> possibleFourCCs = {
        cv::VideoWriter::fourcc('a', 'v', 'c', '1'),
        cv::VideoWriter::fourcc('h', '2', '6', '4'),
        cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
        cv::VideoWriter::fourcc('x', 'w', '6', '4')};
    cv::VideoWriter writer;
    // 打开输出视频
    bool writerOpen = false;
    for (int fourcc : possibleFourCCs)
    {
        writerOpen = writer.open(outputPath, fourcc, fps, cv::Size(frameWidth, frameHeight));
        if (writerOpen)
            break;
    }

    // 如果H.264不可用，使用系统默认编码器
    if (!writerOpen)
        writerOpen = writer.open(outputPath, -1, fps, cv::Size(frameWidth, frameHeight), true);
    if (!writerOpen)
    {
        m_errorMsg = "无法创建输出MP4文件";
        cap.release();
        return false;
    }

    // 逐帧转换
    cv::Mat frame;
    int processed = 0;
    m_progress = 0.0;
    while (cap.read(frame))
    {
        if (frame.empty())
            break;
        writer.write(frame);
        processed++;
        // 更新进度
        if (totalFrame <= 0)
        {
            m_progress = std::min(100.0, static_cast<double>(processed) / 10);
        }
        else
        {
            m_progress = static_cast<double>(processed) / totalFrame * 100;
        }
        m_progress = std::min(100.0, m_progress);
    }

    cap.release();
    writer.release();

    // 验证输出文件是否可用
    cv::VideoCapture veifyCap(outputPath);
    if (!veifyCap.isOpened())
    {
        m_errorMsg = "转换成功，但无法打开（可能损坏）";
        std::remove(outputPath.c_str());
        return false;
    }

    cv::Mat veifyFrame;
    if (!veifyCap.read(veifyFrame) || veifyFrame.empty())
    {
        m_errorMsg = "转换成功，但输出文件有无效帧";
        veifyCap.release();
        std::remove(outputPath.c_str());
        return false;
    }
    veifyCap.release();
    // 检查是否转换完成
    if (processed < totalFrame - 1)
    {
        m_errorMsg = "转换未完成，已处理" + std::to_string(processed) + "帧";
        std::remove(outputPath.c_str());
        return false;
    }
    m_errorMsg = "";
    return true;
}

bool VideoFormatUnifier::checkInputValid(const std::string &inputPath)
{
    // 检查文件是否存在
    if (!std::filesystem::exists(inputPath))
    {
        m_errorMsg = "输入文件不存在: " + inputPath;
        return false;
    }

    // 检查是否为普通文件（不是目录）
    if (!std::filesystem::is_regular_file(inputPath))
    {
        m_errorMsg = "输入路径不是文件: " + inputPath;
        return false;
    }

    // 检查文件大小是否大于0（避免空文件）
    if (std::filesystem::file_size(inputPath) <= 0)
    {
        m_errorMsg = "输入文件为空: " + inputPath;
        return false;
    }

    // 所有检查通过
    return true;
}

std::string VideoFormatUnifier::generateOutputPath(const std::string &inputPath)
{
    std::filesystem::path inputFsPath(inputPath); // 用filesystem路径对象处理

    // 1. 获取输入文件的父目录（如输入是"a/b/c.mp4"，父目录是"a/b"）
    std::filesystem::path parentDir = inputFsPath.parent_path();

    // 2. 获取输入文件的"主名"（不含扩展名，如"c.mp4"的主名是"c"）
    std::string baseName = inputFsPath.stem().string();

    // 3. 生成新文件名：主名 + "_unified.mp4"
    std::string newFileName = baseName + "_unified.mp4";

    // 4. 拼接父目录和新文件名，得到完整输出路径
    std::filesystem::path outputFsPath = parentDir / newFileName;

    // 转换为字符串返回（处理相对路径/绝对路径的自动适配）
    return outputFsPath.string();
}

bool VideoFormatUnifier::checkMp4EncoderSupport()
{
    // 生成随机临时文件名（避免并发时文件冲突）
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    std::string tempFileName = "./temp_encoder_test_" + std::to_string(dis(gen)) + ".mp4";

    // 准备一个最小化的测试帧（10x10像素，空白图像）
    cv::Mat testFrame(10, 10, CV_8UC3, cv::Scalar(0, 0, 0)); // 10x10黑色帧

    // 尝试用H.264编码器创建VideoWriter
    cv::VideoWriter testWriter;
    int fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1'); // H.264编码
    bool isOpened = testWriter.open(
        tempFileName,
        fourcc,
        25.0,             // 测试用帧率（任意值）
        cv::Size(10, 10), // 与测试帧尺寸一致
        true              // 彩色帧
    );

    // 如果打开失败，直接返回false
    if (!isOpened)
    {
        // 清理临时文件（即使没打开成功，也可能创建了空文件）
        if (std::filesystem::exists(tempFileName))
        {
            std::filesystem::remove(tempFileName);
        }
        return false;
    }

    // 尝试写入一帧（验证编码功能）
    testWriter.write(testFrame);

    // 释放资源（触发数据写入磁盘）
    testWriter.release();

    // 检查临时文件是否生成且非空
    bool isValid = std::filesystem::exists(tempFileName) && std::filesystem::file_size(tempFileName) > 0;

    // 无论结果如何，删除临时文件（避免残留）
    if (std::filesystem::exists(tempFileName))
    {
        std::filesystem::remove(tempFileName);
    }

    return isValid;
}