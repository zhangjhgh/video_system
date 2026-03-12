#include "../../include/video/VideoProcessor.hpp"

VideoProcessor::VideoProcessor()
{
    // 初始化字体路径（固定值，后续不修改）
    fontPath = "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"; // 示例中文字体路径
    // 验证字体文件是否存在
    if (!std::filesystem::exists(fontPath))
    {
        std::cerr << "[Warning] 字体文件不存在：" << fontPath << std::endl;
        fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"; //  fallback 字体
    }
    std::cout << "VideoProcessor Initialized" << std::endl;
}

VideoProcessor::~VideoProcessor()
{
    std::cout << "VideoProcessor destroyed" << std::endl;
}

// 生成随机临时文件名（避免多线程/多次调用冲突）
std::string generateTempMp4Path()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    return "./temp_video_" + std::to_string(dis(gen)) + ".mp4";
}

bool VideoProcessor::addWatermark(const std::string &inputPath,
                                  const std::string &outputPath,
                                  const std::string &watermarkText,
                                  int position,
                                  int fontSize,
                                  const std::string color,
                                  float opacity,
                                  ProgressCallback progressCallback)
{
    try
    {
        opacity = std::clamp(opacity, 0.0f, 1.0f);
    }
    catch (...)
    {
        std::cerr << "透明度解析失败，使用默认值0.7f" << std::endl;
        opacity = 0.7f;
    }
    cv::Ptr<cv::freetype::FreeType2> ft2 = cv::freetype::createFreeType2();
    if (ft2.empty() || !isFontAvail(fontPath, *ft2))
    { // 加载字体
        std::cerr << "[Error] 加载字体失败：" << fontPath << std::endl;
        return false;
    }

    // 将输入视频转为标准化MP4（临时文件）
    VideoFormatUnifier unifier;
    std::string tempMp4Path = generateTempMp4Path();
    bool convertOk = unifier.unifyToMp4(inputPath, tempMp4Path, true);
    if (!convertOk) // 允许覆盖临时文件)
    {
        std::cerr << "格式转换失败：" << unifier.getErrorMsg() << std::endl;
        return false;
    }

    cv::VideoCapture cap(tempMp4Path);
    if (!cap.isOpened())
    {
        std::cerr << "Error Opening Video File: " << inputPath << std::endl;
        return false;
    }
    // 获取视频基本信息
    int frameWidth = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int frameHeight = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);
    int totalFrame = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    int lastProcess = -1.0;
    const double PROGRESS_THRESHOLD = 1.0;
    const std::chrono::seconds TIME_THRESHOLD = std::chrono::seconds(1);

    // 记录上次回调时间
    auto lastCallbackTime = std::chrono::steady_clock::now();

    // 创建视频写入器
    cv::VideoWriter writer;
    int codeC = cv::VideoWriter::fourcc('a', 'v', 'c', '1'); // H.264编码
    writer.open(outputPath, codeC, fps, cv::Size(frameWidth, frameHeight));

    // 关键：添加编码器容错（若H.264不可用，自动降级为MP4V）
    if (!writer.isOpened())
    {
        std::cerr << "H.264编码器不可用，尝试MP4V编码器..." << std::endl;
        codeC = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
        writer.open(outputPath, codeC, fps, cv::Size(frameWidth, frameHeight));
        if (!writer.isOpened())
        {
            std::cerr << "所有编码器均失败，无法保存视频！" << std::endl;
            // 记得删除临时文件，避免残留
            std::remove(tempMp4Path.c_str());
            return false;
        }
    }

    // 帧处理
    cv::Mat frame; // 用于存储视频的每一帧图像（OpenCV中用Mat表示图像矩阵）
    int processedFrames = 0;
    int baseline = 0;
    cv::Size textSize = ft2->getTextSize(
        watermarkText,
        fontSize,
        std::max(1, fontSize / 10),
        &baseline);
    textSize.height += baseline;
    cv::Scalar textColor = parseColor(color);

    while (cap.read(frame))
    {
        if (frame.empty())
            break;

        // 计算文本尺寸（用局部 ft2，无共享）
        cv::Point watermarkPos = calWatermarkPosition(position, frame, textSize);
        drawWatermark(*ft2, frame, watermarkText, watermarkPos, fontSize, textColor, opacity);
        writer.write(frame);
        processedFrames++;
        auto now = std::chrono::steady_clock::now();
        // 计算当前进度
        double progress = totalFrame > 0 ? (processedFrames * 100.0) / totalFrame : 0.0;
        progress = std::min(progress, 100.0);

        // 判断是否触发回调
        if (std::abs(progress - lastProcess) >= PROGRESS_THRESHOLD || (now - lastCallbackTime) >= TIME_THRESHOLD)
        {
            progressCallback(progress);
            lastProcess = progress;
            lastCallbackTime = now;
        }
        // 进度输出(10%、20%...)
        if (totalFrame > 0 && processedFrames % (totalFrame / 10) == 0)
        {
            std::cout << "Progress: " << progress << "%" << std::endl;
        }
    }
    if (lastProcess < 100.0)
        progressCallback(100.0);
    cap.release();
    writer.release();
    std::remove(tempMp4Path.c_str());

    // 检查是否处理完成
    if (processedFrames < totalFrame)
    {
        std::cerr << "警告：仅处理了" << processedFrames << "/" << totalFrame << std::endl;
        return false;
    }
    return true;
}

// 关键帧提取
std::vector<cv::Mat> VideoProcessor::extractKeyFrames(const std::string &videoPath, int maxFrames)
{
    cv::Mat frame;
    int frameCount = 0;
    std::vector<cv::Mat> keyFrames;
    // 步骤1：格式转换 → 临时MP4
    VideoFormatUnifier unifier;
    std::string tempMp4Path = generateTempMp4Path();
    if (!unifier.unifyToMp4(videoPath, tempMp4Path, true))
    {
        std::cerr << "格式转换失败：" << unifier.getErrorMsg() << std::endl;
        return keyFrames; // 返回空向量
    }

    cv::VideoCapture cap(tempMp4Path);
    // 均匀采样
    int totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    int step = std::max(1, totalFrames / maxFrames);

    while (cap.read(frame))
    {
        if (frame.empty())
            break;
        if (frameCount % step == 0)
        {
            keyFrames.push_back(frame.clone());
            std::cout << "Extracted frame: " << frameCount << std::endl;
        }
        frameCount++;
    }
    return keyFrames;
}

// 视频信息获取
VideoProcessor::VideoInfo VideoProcessor::getVideoInfo(const std::string &videoPath)
{
    VideoInfo info = {
        0.0, // duration（秒）
        0,   // width
        0,   // height
        0.0, // fps
        0    // totalFrames
    };
    ;
    // 格式转换 → 临时MP4
    VideoFormatUnifier unifier;
    std::string tempMp4Path = generateTempMp4Path();
    if (!unifier.unifyToMp4(videoPath, tempMp4Path, true))
    {
        std::cerr << "格式转换失败：" << unifier.getErrorMsg() << std::endl;
        return info; // 返回默认空信息
    }

    cv::VideoCapture cap(tempMp4Path);
    if (cap.isOpened())
    {
        info.duration = cap.get(cv::CAP_PROP_FRAME_COUNT) / cap.get(cv::CAP_PROP_FPS);
        info.width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
        info.height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
        info.fps = cap.get(cv::CAP_PROP_FPS);
        info.totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
        cap.release();
    }
    return info;
}

// 水印位置计算
cv::Point VideoProcessor::calWatermarkPosition(int position, const cv::Mat &frame, const cv::Size &textSize)
{
    int margin = 20;
    // 文本总高度 = 文字高度（textSize.height） + 基线下方延伸（通常用 textSize.height 的 1/4 估算，或通过 getTextSize 的 baseline 参数获取）
    // 这里简化处理，直接用 textSize.height 作为完整高度（实际需结合 baseline，见下方说明）
    int totalTextHeight = textSize.height;

    switch (position)
    {
    case 0: // 右下
        // 修正 y 坐标：减去文本总高度和边距，确保整个文字在帧内
        return cv::Point(
            frame.cols - textSize.width - margin, // x：右边缘 - 文本宽 - 边距
            frame.rows - totalTextHeight - margin // y：下边缘 - 文本高 - 边距（关键修正）
        );

    case 1: // 左上
        // 保持不变：x 为边距，y 为 文本高 + 边距（确保顶部不截断）
        return cv::Point(margin, totalTextHeight + margin);

    case 2: // 居中
        // 垂直居中需考虑文本高度，修正为：(帧高 - 文本高)/2 + 文本高（基于基线对齐）
        return cv::Point(
            (frame.cols - textSize.width) / 2,                   // x 居中
            (frame.rows - totalTextHeight) / 2 + totalTextHeight // y 居中（基线对齐）
        );

    default: // 右下
        return cv::Point(frame.cols - textSize.width - margin, frame.rows - totalTextHeight - margin);
    }
}

// 文字渲染
void VideoProcessor::drawWatermark(
    cv::freetype::FreeType2 &ft2,
    cv::Mat &frame,
    const std::string &text,
    cv::Point position,
    int fontSize,         // 字体大小
    cv::Scalar textColor, // 文字颜色
    float opacity         // 透明度
)
{
    int thickness = std::max(1, fontSize / 10); // 线宽随字体大小变化

    // 绘制阴影（略暗，全不透明）
    ft2.putText(
        frame, text, position + cv::Point(2, 2),
        fontSize, cv::Scalar(0, 0, 0), thickness, cv::LINE_AA, false);

    // 绘制主文字（带透明度）
    cv::Mat overlay = frame.clone();
    ft2.putText(overlay, text, position,
                fontSize, textColor, thickness, cv::LINE_AA, false);
    cv::addWeighted(overlay, opacity, frame, 1 - opacity, 0, frame);
}

// 辅助函数：将"#RRGGBB"转换为cv::Scalar(B,G,R)
cv::Scalar VideoProcessor::parseColor(const std::string &colorStr)
{
    if (colorStr.empty() || colorStr[0] != '#')
    {
        return cv::Scalar(255, 255, 255); // 默认白色
    }
    // 提取RGB分量（注意OpenCV是BGR顺序）
    int r = std::stoi(colorStr.substr(1, 2), nullptr, 16);
    int g = std::stoi(colorStr.substr(3, 2), nullptr, 16);
    int b = std::stoi(colorStr.substr(5, 2), nullptr, 16);
    return cv::Scalar(b, g, r); // BGR顺序
}

bool VideoProcessor::isFontAvail(const std::string &fontPath, cv::freetype::FreeType2 &ft2)
{
    bool loadSuccess = false;
    const std::string testText = "测试";
    const int testFontHeight = 30;
    const int testThickness = 1;

    // 加载字体（void返回值，通过后续getTextSize验证）
    ft2.loadFontData(fontPath, 0);

    int baseline = 0;
    cv::Size textSize = ft2.getTextSize(
        testText,
        testFontHeight,
        testThickness,
        &baseline);

    // 尺寸有效则表示加载成功
    if (textSize.width > 0 && textSize.height > 0)
    {
        loadSuccess = true;
        ft2.setSplitNumber(10);
        std::cout << "[Success] 字体加载成功：" << fontPath << std::endl;
        std::cout << "[Debug] 测试文本尺寸：宽=" << textSize.width << "，高=" << textSize.height << std::endl;
    }
    else
    {
        std::cerr << "[Error] 字体加载失败：" << fontPath << std::endl;
        std::cerr << "  请检查字体路径是否正确，或字体是否支持中文。" << std::endl;
    }

    return loadSuccess;
}