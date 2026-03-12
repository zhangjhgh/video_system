#!/usr/bin/env python3
"""
系统状态检查脚本 - 修复版本
"""

import requests
import sys

def check_system_health(server_url: str):
    """检查系统健康状态"""
    print("🔍 检查系统状态...")
    
    endpoints = [
        ("服务可用性", ""),  # 根路径
        ("上传接口", "/api/upload"),
        ("处理接口", "/api/process"),
    ]
    
    all_healthy = True
    
    for name, endpoint in endpoints:
        try:
            response = requests.get(f"{server_url}{endpoint}", timeout=5)
            status = "✅ 正常" if response.status_code in [200, 404, 405] else "❌ 异常"
            print(f"  {name}: {status} (HTTP {response.status_code})")
            
            if response.status_code >= 500:
                all_healthy = False
                
        except requests.exceptions.RequestException as e:
            print(f"  {name}: ❌ 连接失败 ({str(e)})")
            all_healthy = False
    
    return all_healthy

def check_disk_usage(server_url: str):
    """检查磁盘使用情况（如果服务器提供此接口）"""
    print("\n💾 检查存储状态...")
    
    # 尝试通过下载接口检查存储可用性
    test_files = [
        "outputs/test_exist.mp4",  # 假设不存在的文件
    ]
    
    for test_file in test_files:
        try:
            response = requests.get(
                f"{server_url}/api/download/{test_file}",
                timeout=5
            )
            
            if response.status_code == 404:
                print("  ✅ 下载接口响应正常（返回预期404）")
                break
            elif response.status_code == 200:
                print("  ⚠️  测试文件意外存在")
            else:
                print(f"  ⚠️  下载接口状态: HTTP {response.status_code}")
                
        except Exception as e:
            print(f"  ❌ 下载接口检查失败: {str(e)}")

if __name__ == "__main__":
    server_url = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8080"
    
    print(f"📊 系统状态检查 - {server_url}")
    print("=" * 50)
    
    healthy = check_system_health(server_url)
    check_disk_usage(server_url)
    
    print("\n" + "=" * 50)
    if healthy:
        print("🎉 系统状态: ✅ 健康")
    else:
        print("⚠️  系统状态: ❌ 存在问题，建议检查服务器日志")