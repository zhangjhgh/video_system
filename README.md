# video_system

一个基于 C++ 实现的简单视频处理/管理系统项目，主要用于学习和实践视频数据处理、系统模块设计以及后端工程开发。

该项目实现了视频数据的基本处理流程，并通过模块化设计组织代码结构，便于扩展和维护。

---

## 项目简介

video_system 是一个用于学习视频系统开发的实践项目，主要实现视频数据处理流程的基础功能，并通过合理的代码结构组织不同模块。

项目重点在于：

- 视频处理流程的实现
- 系统模块化设计
- 后端工程开发实践

---

## 项目特点

- 使用 **C++ 实现核心逻辑**
- 模块化设计，代码结构清晰
- 支持基本的视频数据处理流程
- 适合作为 **视频系统开发学习项目**

---

## 项目结构

```
video_system
├── src/            # 核心源码
├── include/        # 头文件
├── data/           # 测试数据
├── build/          # 编译生成目录
├── CMakeLists.txt  # CMake 构建文件
└── README.md       # 项目说明
```

---

## 运行环境

操作系统：

- Linux（推荐 Ubuntu / CentOS）

编译环境：

- g++ 或 clang（支持 C++11 及以上）
- CMake ≥ 3.10

---

## 编译项目

```bash
git clone https://github.com/zhangjhgh/video_system.git
cd video_system

mkdir build
cd build

cmake ..
make
```

## 运行程序

编译完成后执行：

```bash
./video_system
```


## 学习内容

通过本项目可以学习到：

- C++ 项目结构设计
- 视频系统基础流程
- Linux 环境下项目构建
- CMake 构建系统使用
