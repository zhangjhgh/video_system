#ifndef VIDEOPRROCESSOR_HPP
#define VIDEOPRROCESSOR_HPP

#include <opencv2/opencv.hpp>
#include <opencv2/freetype.hpp>
#include <string>
#include <memory>
#include <vector>
#include <iostream>
#include <filesystem>
#include "VideoFormatUnifier.hpp"

// 视频处理
class VideoProcessor
{
public:
    VideoProcessor();
    ~VideoProcessor();

    using ProgressCallback = std::function<void(double)>;
    // 添加水印
    bool addWatermark(const std::string &inputPath, const std::string &outputPath, const std::string &watermarkText, int position, int fontSize, const std::string color, float opacity, ProgressCallback progressCallback = [](double) {}); // 0：右下 1：左上 2：中间

    // 关键帧提取
    std::vector<cv::Mat> extractKeyFrames(const std::string &videoPath, int maxFrames = 10);

    // 视频信息获取
    struct VideoInfo
    {
        double duration; // 时长（秒）
        int width;       // 宽度
        int height;      // 高度
        double fps;      // 帧率
        int totalFrames; // 总帧数
    };

    VideoInfo getVideoInfo(const std::string &videoPath);
    std::string generateTempMp4Path()
    {
        // 实现逻辑（确保生成绝对路径且唯一）
        std::string tempDir = "/tmp/video_system_temp/";
        std::filesystem::create_directories(tempDir);
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        std::random_device rd;
        return tempDir + "temp_" + std::to_string(timestamp) + "_" + std::to_string(rd() % 10000) + ".mp4";
    }

private:
    std::string fontPath;                                                        // 中文字体路径
    bool isFontAvail(const std::string &fontPath, cv::freetype::FreeType2 &ft2); // 判断字体是否可用
    // 水印位置计算
    cv::Point calWatermarkPosition(int position, const cv::Mat &frame, const cv::Size &textSize);

    // 绘制水印文本
    void drawWatermark(
        cv::freetype::FreeType2 &ft2,
        cv::Mat &frame,
        const std::string &text,
        cv::Point position,
        int fontSize,
        cv::Scalar textColor,
        float opacity);
    cv::Scalar parseColor(const std::string &colorStr);
};
#endif // VIDEOPRROCESSOR_HPP