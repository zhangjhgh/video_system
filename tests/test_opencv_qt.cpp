#include <iostream>
#include <opencv2/opencv.hpp>

int main()
{
    std::cout << "=== 简单环境测试 ===" << std::endl;
    std::cout << "测试 OpenCV 基础功能..." << std::endl;

    try
    {
        // 测试 OpenCV 版本
        std::cout << "OpenCV 版本: " << CV_VERSION << std::endl;

        // 创建一个测试图像
        cv::Mat test_image(300, 500, CV_8UC3, cv::Scalar(50, 100, 150));

        // 添加文字
        cv::putText(test_image, "OpenCV Test - SUCCESS",
                    cv::Point(30, 150),
                    cv::FONT_HERSHEY_SIMPLEX,
                    1.0,
                    cv::Scalar(255, 255, 255),
                    2);

        // 保存图像
        std::string filename = "simple_test_result.jpg";
        bool save_result = cv::imwrite(filename, test_image);

        if (save_result)
        {
            std::cout << "✅ 测试图像保存成功: " << filename << std::endl;
        }
        else
        {
            std::cout << "❌ 测试图像保存失败" << std::endl;
            return 1;
        }

        // 测试 C++17 特性
        std::cout << "测试 C++17 标准..." << std::endl;
        if constexpr (__cplusplus >= 201703L)
        {
            std::cout << "✅ C++17 支持正常 (C++版本: " << __cplusplus << ")" << std::endl;
        }
        else
        {
            std::cout << "❌ C++17 不支持" << std::endl;
            return 1;
        }

        std::cout << "=== 简单测试全部通过 ===" << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cout << "❌ 测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cout << "❌ 测试过程中发生未知异常" << std::endl;
        return 1;
    }
}