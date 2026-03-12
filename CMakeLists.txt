cmake_minimum_required(VERSION 3.16)
project(VideoProcessingSystem VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置构建类型，默认为Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "源目录: ${CMAKE_SOURCE_DIR}")
message(STATUS "构建目录: ${CMAKE_BINARY_DIR}")

# 查找 OpenCV
message(STATUS "查找 OpenCV...")
find_package(OpenCV REQUIRED)
if(OpenCV_FOUND)
    message(STATUS "✅ OpenCV 找到: ${OpenCV_VERSION}")
    message(STATUS "OpenCV 包含路径: ${OpenCV_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "❌ OpenCV 未找到")
endif()

# 查找 Qt6
message(STATUS "查找 Qt6...")
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
if(Qt6_FOUND)
    message(STATUS "✅ Qt6 找到: ${Qt6_VERSION}")
    
    # 只为 Qt 目标启用自动 MOC
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
else()
    message(FATAL_ERROR "❌ Qt6 未找到")
endif()

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/shared
    ${OpenCV_INCLUDE_DIRS}
)

# 测试1: 仅 OpenCV 的简单测试 - 禁用 AUTOMOC
add_executable(simple_test tests/simple_test.cpp)
# 为 simple_test 显式禁用 AUTOMOC
set_target_properties(simple_test PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(simple_test ${OpenCV_LIBS})
message(STATUS "添加 simple_test 目标 (禁用 AUTOMOC)")

# 测试2: OpenCV + Qt 集成测试 - 使用 AUTOMOC
add_executable(test_opencv_qt tests/test_opencv_qt.cpp)
target_link_libraries(test_opencv_qt
    Qt6::Core
    Qt6::Widgets
    ${OpenCV_LIBS}
)
message(STATUS "添加 test_opencv_qt 目标")

# 设置输出目录
set_target_properties(simple_test test_opencv_qt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "=== CMake 配置完成 ===")
message(STATUS "构建目标:")
message(STATUS "  - simple_test: 仅 OpenCV 测试")
message(STATUS "  - test_opencv_qt: OpenCV + Qt 集成测试")