#ifndef VIDEOFORMATUNIFIER_HPP
#define VIDEOFORMATUNIFIER_HPP
#include <opencv2/opencv.hpp>
#include <string>
#include <memory>
#include <vector>
#include <iostream>
#include <filesystem>
#include <random>

class VideoFormatUnifier
{
public:
    // 转换视频格式为mp4
    bool unifyToMp4(const std::string &inputPath,
                    std::string &outputPath,
                    bool overwriter = false);

    // 转换进度
    double getProgress() const { return m_progress; }

    // 错误信息
    std::string getErrorMsg() const { return m_errorMsg; }

private:
    double m_progress = 0.0; // 转换进度
    std::string m_errorMsg;  // 错误信息

    // 检查输入文件是否存在
    bool checkInputValid(const std::string &inputPath);

    // 生成输出路径（默认原路径，加"_unified.mp4"）
    std::string generateOutputPath(const std::string &inputPath);

    // 验证MP4编码是否可用
    bool checkMp4EncoderSupport();
};

#endif