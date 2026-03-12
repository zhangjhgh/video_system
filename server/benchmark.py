#!/usr/bin/env python3
"""
性能基准测试脚本 - 修复版本
"""

import requests
import time
import statistics
from concurrent.futures import ThreadPoolExecutor

def benchmark_upload(server_url: str, file_path: str, num_requests: int = 10):
    """上传性能测试"""
    print(f"📤 上传性能测试 ({num_requests} 次请求)...")
    
    times = []
    successes = 0
    
    for i in range(num_requests):
        try:
            start_time = time.time()
            
            with open(file_path, "rb") as f:
                files = {"file": (os.path.basename(file_path), f, "video/mp4")}
                response = requests.post(
                    f"{server_url}/api/upload",
                    files=files,
                    timeout=30
                )
            
            elapsed = time.time() - start_time
            times.append(elapsed)
            
            if response.status_code == 200:
                successes += 1
                print(f"  请求 {i+1}: ✅ {elapsed:.2f}秒")
            else:
                print(f"  请求 {i+1}: ❌ HTTP {response.status_code}")
                
        except Exception as e:
            print(f"  请求 {i+1}: ❌ 异常 {str(e)}")
    
    if times:
        print(f"\n📊 上传性能统计:")
        print(f"  成功率: {successes}/{num_requests} ({successes/num_requests*100:.1f}%)")
        print(f"  平均时间: {statistics.mean(times):.2f}秒")
        print(f"  最短时间: {min(times):.2f}秒")
        print(f"  最长时间: {max(times):.2f}秒")
        if len(times) > 1:
            print(f"  标准差: {statistics.stdev(times):.2f}秒")
        else:
            print(f"  标准差: 0.00秒")

def benchmark_download(server_url: str, download_path: str, num_requests: int = 10):
    """下载性能测试"""
    print(f"\n📥 下载性能测试 ({num_requests} 次请求)...")
    
    times = []
    successes = 0
    
    for i in range(num_requests):
        try:
            start_time = time.time()
            
            response = requests.get(
                f"{server_url}/api/download/{download_path}",
                stream=True,
                timeout=30
            )
            
            # 读取少量数据来测试响应时间
            if response.status_code == 200:
                content = response.content[:1024]  # 只读取1KB
                elapsed = time.time() - start_time
                times.append(elapsed)
                successes += 1
                print(f"  请求 {i+1}: ✅ {elapsed:.2f}秒")
            else:
                print(f"  请求 {i+1}: ❌ HTTP {response.status_code}")
                
        except Exception as e:
            print(f"  请求 {i+1}: ❌ 异常 {str(e)}")
    
    if times:
        print(f"\n📊 下载性能统计:")
        print(f"  成功率: {successes}/{num_requests} ({successes/num_requests*100:.1f}%)")
        print(f"  平均时间: {statistics.mean(times):.2f}秒")
        print(f"  最短时间: {min(times):.2f}秒")
        print(f"  最长时间: {max(times):.2f}秒")
        if len(times) > 1:
            print(f"  标准差: {statistics.stdev(times):.2f}秒")
        else:
            print(f"  标准差: 0.00秒")

if __name__ == "__main__":
    import sys
    import os
    
    server_url = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8080"
    test_file = sys.argv[2] if len(sys.argv) > 2 else "./test_video.mp4"
    
    print(f"🚀 性能基准测试 - {server_url}")
    print("=" * 50)
    
    # 上传性能测试
    if os.path.exists(test_file):
        benchmark_upload(server_url, test_file)
    else:
        print(f"❌ 测试文件不存在: {test_file}")
    
    print("\n🎯 提示: 下载性能测试需要先运行完整流程测试获得可下载文件路径")