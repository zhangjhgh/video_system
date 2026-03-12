#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <filesystem>
#include <algorithm>
#include "../include/tasks/TaskManager.hpp"        // 你的TaskManager头文件路径
#include "../include/video/VideoProcessor.hpp"     // 你的VideoProcessor头文件路径
#include "../include/video/VideoFormatUnifier.hpp" // 你的VideoFormatUnifier头文件路径

// --------------------------
// 全局测试配置（必须替换为你的实际路径）
// --------------------------
// 测试用输入视频路径（确保该文件存在）
const std::string TEST_INPUT_VIDEO = "/home/zhangsan/桌面/test.mp4";
// 测试结果输出目录（不存在会自动创建）
const std::string TEST_OUTPUT_DIR = "/home/zhangsan/~projects/video_system/server/results/";
// 临时文件目录（与VideoProcessor中generateTempMp4Path保持一致）
const std::string TEMP_FILE_DIR = "/tmp/video_system_temp/";

// --------------------------
// 工具函数：打印任务状态字符串（方便调试）
// --------------------------
std::string getTaskStatusStr(TaskStatus status)
{
    switch (status)
    {
    case TaskStatus::PENDING:
        return "PENDING";
    case TaskStatus::PROCESSONG:
        return "PROCESSONG";
    case TaskStatus::COMPLETED:
        return "COMPLETED";
    case TaskStatus::FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

// --------------------------
// 核心测试逻辑：模拟真实视频处理任务
// --------------------------
void simulateRealVideoTask(TaskManager &taskMgr, const std::string &taskId)
{
    // 1. 启动任务（状态从PENDING转为PROCESSONG）
    if (!taskMgr.startTask(taskId))
    {
        std::cerr << "[任务" << taskId << "] 启动失败：任务不存在或已启动" << std::endl;
        taskMgr.markFailed(taskId, "任务启动失败：不存在或已启动");
        return;
    }

    // 2. 获取任务基本信息（从TaskManager中读取输入路径和参数）
    TaskInfo task = taskMgr.getTaskInfo(taskId);
    if (task.taskId.empty())
    {
        std::cerr << "[任务" << taskId << "] 获取信息失败：任务不存在" << std::endl;
        taskMgr.markFailed(taskId, "获取任务信息失败：任务不存在");
        return;
    }
    std::cout << "[任务" << taskId << "] 开始处理：输入文件=" << task.inputFile
              << "，操作类型=" << task.operation
              << "，参数=" << task.parameters << std::endl;

    // 3. 解析任务参数（以“加水印”为例：参数格式为“水印文本|位置”，如“TestWatermark|0”）
    std::string watermarkText = "DefaultWatermark"; // 默认水印文本
    int watermarkPos = 0;                           // 默认位置：右下（0）
    size_t paramSplit = task.parameters.find('|');
    if (paramSplit != std::string::npos)
    {
        watermarkText = task.parameters.substr(0, paramSplit);
        try
        {
            watermarkPos = std::stoi(task.parameters.substr(paramSplit + 1));
            // 确保位置在合法范围（0-2）
            watermarkPos = std::clamp(watermarkPos, 0, 2);
        }
        catch (...)
        {
            std::cerr << "[任务" << taskId << "] 参数解析失败，使用默认位置（0）" << std::endl;
            watermarkPos = 0;
        }
    }

    // 4. 初始化处理组件
    VideoProcessor videoProc;
    VideoFormatUnifier formatUnifier;
    std::string tempMp4Path = videoProc.generateTempMp4Path();                 // 生成临时文件路径
    std::string outputVideoPath = TEST_OUTPUT_DIR + taskId + "_watermark.mp4"; // 最终输出路径

    // 5. 确保输出目录存在
    std::filesystem::create_directories(TEST_OUTPUT_DIR);
    std::filesystem::create_directories(TEMP_FILE_DIR);

    try
    {
        // 步骤1：格式转换（将输入视频转为标准MP4，存入临时文件）
        std::cout << "[任务" << taskId << "] 开始格式转换：" << task.inputFile << "→" << tempMp4Path << std::endl;
        if (!formatUnifier.unifyToMp4(task.inputFile, tempMp4Path, true))
        {
            throw std::runtime_error("格式转换失败：" + formatUnifier.getErrorMsg());
        }

        // 步骤2：获取视频信息（用于计算处理进度）
        VideoProcessor::VideoInfo videoInfo = videoProc.getVideoInfo(tempMp4Path);
        if (videoInfo.totalFrames <= 0)
        {
            throw std::runtime_error("获取视频信息失败：总帧数为0");
        }
        std::cout << "[任务" << taskId << "] 视频信息：分辨率=" << videoInfo.width << "x" << videoInfo.height
                  << "，帧率=" << videoInfo.fps << "，总帧数=" << videoInfo.totalFrames << std::endl;

        // 步骤3：添加水印（实时更新进度到TaskManager）
        std::cout << "[任务" << taskId << "] 开始添加水印：文本=" << watermarkText << "，位置=" << watermarkPos << std::endl;
        bool watermarkSuccess = videoProc.addWatermark(tempMp4Path, outputVideoPath, watermarkText, watermarkPos);
        if (!watermarkSuccess)
        {
            throw std::runtime_error("添加水印处理失败");
        }

        // 步骤4：处理完成，更新任务状态
        taskMgr.markCompleted(taskId, outputVideoPath);
        std::cout << "[任务" << taskId << "] 处理成功！输出文件：" << outputVideoPath << std::endl;
    }
    catch (const std::exception &e)
    {
        // 处理失败，记录错误信息
        std::cerr << "[任务" << taskId << "] 处理失败：" << e.what() << std::endl;
        taskMgr.markFailed(taskId, e.what());
    }

    // 清理临时文件（即使处理失败也删除）
    if (std::filesystem::exists(tempMp4Path))
    {
        std::filesystem::remove(tempMp4Path);
        // std::cout << "[任务" << taskId << "] 临时文件已清理：" << tempMp4Path << std::endl;
    }
}

// --------------------------
// 测试用例1：单任务完整生命周期（创建→处理→完成）
// --------------------------
void testSingleTaskLifecycle(TaskManager &taskMgr)
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试用例1：单任务完整生命周期" << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. 创建“加水印”任务（参数格式：水印文本|位置）
    std::string taskParam = "SingleTaskWatermark|1"; // 水印文本：SingleTaskWatermark，位置：左上（1）
    std::string taskId = taskMgr.createTask(TEST_INPUT_VIDEO, "WATERMARK", taskParam);
    std::cout << "1. 创建任务成功，任务ID：" << taskId << std::endl;

    // 2. 启动任务处理线程
    std::thread taskThread(simulateRealVideoTask, std::ref(taskMgr), taskId);
    taskThread.join(); // 等待任务完成

    // 3. 查询任务最终结果
    TaskInfo finalTask = taskMgr.getTaskInfo(taskId);
    std::cout << "\n3. 任务最终状态：" << std::endl;
    std::cout << "   - 任务ID：" << finalTask.taskId << std::endl;
    std::cout << "   - 状态：" << getTaskStatusStr(finalTask.status) << std::endl;
    std::cout << "   - 进度：" << finalTask.progress << "%" << std::endl;
    std::cout << "   - 输出文件：" << finalTask.outputFile << std::endl;
    std::cout << "   - 错误信息：" << finalTask.errorMassage << std::endl;

    // 验证用例是否通过
    bool isPass = (finalTask.status == TaskStatus::COMPLETED) && (finalTask.progress == 100.0) && (!finalTask.outputFile.empty()) && (finalTask.errorMassage.empty());
    std::cout << "\n测试用例1 " << (isPass ? "通过 ✅" : "失败 ❌") << std::endl;
}

// --------------------------
// 测试用例2：多任务并发处理（验证线程安全）
// --------------------------
void testMultiTaskConcurrency(TaskManager &taskMgr)
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试用例2：多任务并发处理（3个任务）" << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. 创建3个不同参数的“加水印”任务
    std::vector<std::string> taskIds;
    std::vector<std::string> taskParams = {
        "MultiTaskWatermark_1|0", // 任务1：右下位置
        "MultiTaskWatermark_2|1", // 任务2：左上位置
        "MultiTaskWatermark_3|2"  // 任务3：中间位置
    };

    for (int i = 0; i < 3; i++)
    {
        std::string taskId = taskMgr.createTask(TEST_INPUT_VIDEO, "WATERMARK", taskParams[i]);
        taskIds.push_back(taskId);
        std::cout << "1. 创建并发任务" << (i + 1) << "，ID：" << taskId << "，参数：" << taskParams[i] << std::endl;
    }

    // 2. 启动3个并发线程处理任务
    std::vector<std::thread> taskThreads;
    for (const std::string &taskId : taskIds)
    {
        taskThreads.emplace_back(simulateRealVideoTask, std::ref(taskMgr), taskId);
    }

    // 等待所有任务完成
    for (auto &thread : taskThreads)
    {
        thread.join();
    }

    // 3. 查询所有任务结果，验证并发安全性
    std::vector<TaskInfo> allTasks = taskMgr.getAllTasks();
    int completedCount = 0;
    bool hasConflict = false;

    std::cout << "\n3. 并发任务最终结果：" << std::endl;
    for (const TaskInfo &task : allTasks)
    {
        // 只关注本次创建的3个任务
        if (std::find(taskIds.begin(), taskIds.end(), task.taskId) == taskIds.end())
        {
            continue;
        }

        std::cout << "   - 任务" << task.taskId << "：状态=" << getTaskStatusStr(task.status)
                  << "，进度=" << task.progress << "%，输出=" << (task.outputFile.empty() ? "无" : task.outputFile.substr(0, 50) + "...") << std::endl;

        // 统计完成数，检查是否有状态冲突（如进度0%却显示COMPLETED）
        if (task.status == TaskStatus::COMPLETED)
        {
            completedCount++;
            if (task.progress != 100.0 || task.outputFile.empty())
            {
                hasConflict = true;
            }
        }
        else if (task.status == TaskStatus::FAILED)
        {
            if (task.errorMassage.empty())
            {
                hasConflict = true;
            }
        }
    }

    // 验证用例是否通过（无冲突且至少2个任务完成视为通过）
    bool isPass = !hasConflict && (completedCount >= 2);
    std::cout << "\n测试用例2 " << (isPass ? "通过 ✅" : "失败 ❌") << std::endl;
    std::cout << "   - 完成任务数：" << completedCount << "/3" << std::endl;
    std::cout << "   - 状态冲突：" << (hasConflict ? "有 ❌" : "无 ✅") << std::endl;
}

// --------------------------
// 测试用例3：异常场景验证（边界条件）
// --------------------------
void testExceptionScenarios(TaskManager &taskMgr)
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试用例3：异常场景验证" << std::endl;
    std::cout << "========================================" << std::endl;

    // 测试场景1：启动不存在的任务
    std::string nonExistentTaskId = "task_999999_999";
    bool startNonExistent = taskMgr.startTask(nonExistentTaskId);
    std::cout << "1. 启动不存在的任务（ID：" << nonExistentTaskId << "）："
              << (startNonExistent ? "成功 ❌（错误）" : "失败 ✅（正确）") << std::endl;

    // 测试场景2：重复启动已启动的任务
    std::string repeatTaskId = taskMgr.createTask(TEST_INPUT_VIDEO, "WATERMARK", "RepeatTask|0");
    taskMgr.startTask(repeatTaskId);                   // 第一次启动（成功）
    bool startAgain = taskMgr.startTask(repeatTaskId); // 第二次启动（应失败）
    std::cout << "2. 重复启动已启动的任务（ID：" << repeatTaskId << "）："
              << (startAgain ? "成功 ❌（错误）" : "失败 ✅（正确）") << std::endl;

    // 测试场景3：更新不存在任务的进度
    taskMgr.updateProgress(nonExistentTaskId, 50.0); // 无异常抛出即正确
    std::cout << "3. 更新不存在任务的进度：无异常抛出 ✅（正确）" << std::endl;

    // 测试场景4：处理不存在的输入文件
    std::string invalidInputTaskId = taskMgr.createTask("/invalid/path/nonexistent.mp4", "WATERMARK", "InvalidInput|0");
    std::thread invalidTaskThread(simulateRealVideoTask, std::ref(taskMgr), invalidInputTaskId);
    invalidTaskThread.join();
    TaskInfo invalidTask = taskMgr.getTaskInfo(invalidInputTaskId);
    std::cout << "4. 处理不存在的输入文件（ID：" << invalidInputTaskId << "）："
              << "状态=" << getTaskStatusStr(invalidTask.status)
              << (invalidTask.status == TaskStatus::FAILED ? " ✅（正确）" : " ❌（错误）") << std::endl;

    std::cout << "\n测试用例3 完成（无程序崩溃即视为通过 ✅）" << std::endl;
}

// --------------------------
// 主函数：执行所有测试用例
// --------------------------
int main(int argc, char *argv[])
{
    // 初始化TaskManager
    TaskManager taskMgr;
    std::cout << "========================================" << std::endl;
    std::cout << "TaskManager 测试程序启动" << std::endl;
    std::cout << "========================================" << std::endl;

    // 执行所有测试用例
    testSingleTaskLifecycle(taskMgr);
    testMultiTaskConcurrency(taskMgr);
    testExceptionScenarios(taskMgr);

    std::cout << "\n========================================" << std::endl;
    std::cout << "TaskManager 测试程序结束" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}