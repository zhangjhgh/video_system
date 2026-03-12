#include <iostream>
#include "../include/video/VideoProcessor.hpp"

int main()
{
    // 初始化
    VideoProcessor processor;

    std::string input = "/home/zhangsan/桌面/test.mp4";
    std::string output = "/home/zhangsan/~projects/video_system/server/outputs";
    std::string watermark = "MyVideoServer";
    int position = 0;

    bool success = processor.addWatermark(input, output, watermark, position);

    if (success)
    {
        std::cout << "加水印成功" << std::endl;
    }
    else
    {
        std::cout << "加水印失败" << std::endl;
    }

    return 0;
}