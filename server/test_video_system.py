#!/usr/bin/env python3
"""
视频处理系统完整测试脚本（适配GET/POST仅支持场景）
功能：上传→处理→查询进度→下载全流程测试，支持单例和并发测试
"""

import requests
import time
import os
import argparse
import uuid
from typing import Dict, Optional, Any, List
from concurrent.futures import ThreadPoolExecutor, as_completed

# 服务器配置（根据实际情况修改）
DEFAULT_SERVER_URL = "http://127.0.0.1:8080"
UPLOAD_ENDPOINT = "/api/upload"         # POST
PROCESS_ENDPOINT = "/api/process"       # POST
QUERY_ENDPOINT_TEMPLATE = "/api/task/{}"  # GET
DOWNLOAD_ENDPOINT_TEMPLATE = "/api/download/{}"  # GET

# 支持的视频格式
ALLOWED_EXTENSIONS = {"mp4", "avi", "mov", "flv", "mkv"}

class VideoSystemTester:
    def __init__(self, server_url: str = DEFAULT_SERVER_URL):
        self.server_url = server_url.rstrip('/')
        self.session = requests.Session()
        # 增加连接超时配置，避免长期阻塞
        self.session.timeout = 10
        self.session.headers.update({"User-Agent": "VideoSystemTester/1.0"})

    def _retry_request(self, func, max_retries: int = 3, delay: float = 2) -> Any:
        """带重试的请求封装（处理临时网络错误）"""
        for i in range(max_retries):
            try:
                return func()
            except (requests.exceptions.ConnectionError, 
                    requests.exceptions.Timeout,
                    requests.exceptions.ChunkedEncodingError) as e:
                if i == max_retries - 1:
                    raise  # 最后一次重试失败，抛出异常
                time.sleep(delay)
                print(f"⚠️ 请求重试 ({i+1}/{max_retries})：{str(e)}")
        raise Exception("超过最大重试次数")

    def validate_file(self, file_path: str) -> bool:
        """验证测试文件的合法性"""
        if not os.path.exists(file_path):
            print(f"❌ 文件不存在: {file_path}")
            return False
        
        file_ext = file_path.split('.')[-1].lower()
        if file_ext not in ALLOWED_EXTENSIONS:
            print(f"❌ 不支持的文件格式: .{file_ext}，支持格式: {ALLOWED_EXTENSIONS}")
            return False
        
        file_size = os.path.getsize(file_path)
        if file_size == 0:
            print(f"❌ 文件为空: {file_path}")
            return False
        
        print(f"✓ 文件验证通过: {os.path.basename(file_path)} ({file_size} 字节)")
        return True

    def upload_file(self, file_path: str, test_id: str = "") -> Optional[str]:
        """上传文件到服务器，返回服务器文件路径"""
        print(f"{test_id}[1/4] 上传文件: {os.path.basename(file_path)}")
        
        try:
            with open(file_path, "rb") as f:
                files = {
                    "file": (
                        os.path.basename(file_path),
                        f,
                        f"video/{file_path.split('.')[-1].lower()}"
                    )
                }
                
                # 带重试的上传请求
                response = self._retry_request(
                    lambda: self.session.post(
                        f"{self.server_url}{UPLOAD_ENDPOINT}",
                        files=files,
                        timeout=60  # 上传超时设长一些
                    )
                )
            
            if response.status_code == 200:
                try:
                    result = response.json()
                except ValueError:
                    print(f"{test_id}❌ 上传响应格式错误: {response.text}")
                    return None
                
                if result.get("code") == 200:
                    server_file_path = result["data"].get("filePath")
                    if not server_file_path:
                        print(f"{test_id}❌ 服务器未返回文件路径")
                        return None
                    print(f"{test_id}✓ 上传成功 → 服务器路径: {server_file_path}")
                    return server_file_path
                else:
                    print(f"{test_id}❌ 上传失败: {result.get('msg', '未知错误')}")
            else:
                print(f"{test_id}❌ 上传HTTP错误: {response.status_code}，响应: {response.text[:100]}")
                
        except Exception as e:
            print(f"{test_id}❌ 上传异常: {str(e)}")
                
        return None

    def process_video(self, server_file_path: str, test_id: str = "") -> Optional[str]:
        """请求视频处理（加水印），返回任务ID"""
        print(f"{test_id}[2/4] 提交处理请求...")
        
        try:
            # 生成唯一水印文本，避免并发任务冲突
            unique_id = uuid.uuid4().hex[:8]
            request_body = {
                "filePath": server_file_path,
                "operation": "watermark",
                "parameters": {
                    "watermarkText": f"TEST_{unique_id}",  # 唯一水印
                    "position": 0,          # 水印位置（0-3：左上/右上/左下/右下）
                    "fontSize": 24,
                    "fontColor": "#FFFFFF",
                    "opacity": 0.8
                }
            }
            
            # 带重试的处理请求
            response = self._retry_request(
                lambda: self.session.post(
                    f"{self.server_url}{PROCESS_ENDPOINT}",
                    json=request_body,
                    timeout=30
                )
            )
            
            if response.status_code == 200:
                try:
                    result = response.json()
                except ValueError:
                    print(f"{test_id}❌ 处理响应格式错误: {response.text}")
                    return None
                
                if result.get("code") == 200:
                    task_id = result["data"].get("taskId")
                    if not task_id:
                        print(f"{test_id}❌ 服务器未返回任务ID")
                        return None
                    print(f"{test_id}✓ 处理请求成功 → 任务ID: {task_id}")
                    return task_id
                else:
                    print(f"{test_id}❌ 处理请求失败: {result.get('msg', '未知错误')}")
            else:
                print(f"{test_id}❌ 处理请求HTTP错误: {response.status_code}，响应: {response.text[:100]}")
                
        except Exception as e:
            print(f"{test_id}❌ 处理请求异常: {str(e)}")
            
        return None

    def wait_for_task_completion(self, task_id: str, timeout: int = 300, test_id: str = "") -> Optional[Dict]:
        """轮询任务进度，等待完成（超时时间默认5分钟）"""
        print(f"{test_id}[3/4] 等待任务完成（超时{timeout}秒）...")
        
        start_time = time.time()
        last_progress = -1  # 记录上次进度，避免重复打印
        
        while time.time() - start_time < timeout:
            try:
                # 带重试的查询请求
                response = self._retry_request(
                    lambda: self.session.get(
                        f"{self.server_url}{QUERY_ENDPOINT_TEMPLATE.format(task_id)}",
                        timeout=10
                    )
                )
                
                if response.status_code == 200:
                    try:
                        result = response.json()
                    except ValueError:
                        print(f"{test_id}❌ 进度响应格式错误: {response.text}")
                        time.sleep(2)
                        continue
                    
                    task_data = result.get("data", {})
                    status = task_data.get("status", "unknown")
                    progress = task_data.get("progress", 0.0)
                    
                    # 进度变化时才打印（减少日志冗余）
                    if progress != last_progress:
                        print(f"{test_id}  进度: {progress:.1f}% - 状态: {status}")
                        last_progress = progress
                    
                    # 任务完成状态判断（成功/失败）
                    if status in ["success", "completed"]:
                        print(f"{test_id}✓ 任务处理完成!")
                        return task_data
                    elif status == "failed":
                        error_msg = task_data.get("errorMsg", "未知错误")
                        print(f"{test_id}❌ 任务处理失败: {error_msg}")
                        return None
                
                else:
                    print(f"{test_id}❌ 查询进度HTTP错误: {response.status_code}")
                
            except Exception as e:
                print(f"{test_id}❌ 查询进度异常: {str(e)}")
            
            time.sleep(2)  # 每2秒查询一次
        
        print(f"{test_id}❌ 任务超时未完成（{timeout}秒）")
        return None

    def download_file(self, output_file_path: str, local_filename: str, test_id: str = "") -> bool:
        """下载处理后的文件（仅使用GET，适配服务器不支持HEAD的情况）"""
        print(f"{test_id}[4/4] 下载结果文件...")

        try:
            # 修正1：移除路径中的outputs/前缀（关键！）
            if output_file_path.startswith(("outputs/", "outputs\\")):
                output_file_path = output_file_path[len("outputs/"):]
                print(f"{test_id}  修正路径: {output_file_path}")

            # 构建下载URL
            download_url = f"{self.server_url}{DOWNLOAD_ENDPOINT_TEMPLATE.format(output_file_path)}"
            print(f"{test_id}  下载URL: {download_url}")

            # 修正2：不使用HEAD请求，直接GET下载（适配服务器）
            print(f"{test_id}  开始下载...")
            response = self._retry_request(
                lambda: self.session.get(
                    download_url,
                    stream=True,
                    timeout=60  # 下载超时设长一些
                )
            )

            # 处理HTTP错误状态
            if response.status_code == 404:
                print(f"{test_id}❌ 服务器文件不存在（404）")
                return False
            elif response.status_code != 200:
                print(f"{test_id}❌ 下载失败: HTTP {response.status_code}，响应: {response.text[:100]}")
                return False

            # 写入本地文件
            total_size = 0
            with open(local_filename, 'wb') as f:
                for chunk in response.iter_content(chunk_size=8192):
                    if chunk:  # 过滤空块
                        f.write(chunk)
                        total_size += len(chunk)

            # 验证下载文件完整性
            if not os.path.exists(local_filename):
                print(f"{test_id}❌ 下载文件未生成")
                return False
            
            local_size = os.path.getsize(local_filename)
            if local_size != total_size:
                print(f"{test_id}❌ 下载文件不完整（写入{local_size}字节，实际接收{total_size}字节）")
                return False
            
            print(f"{test_id}✓ 下载成功 → {local_filename}（{local_size}字节）")
            return True

        except Exception as e:
            print(f"{test_id}❌ 下载异常: {str(e)}")
            # 清理不完整文件
            if os.path.exists(local_filename):
                os.remove(local_filename)
            return False

    def single_test(self, video_file: str, test_name: str = "单例测试") -> Dict[str, Any]:
        """执行单流程测试（上传→处理→下载）"""
        print(f"\n{'='*60}")
        print(f"🚀 开始 {test_name}")
        print(f"{'='*60}")
        
        start_time = time.time()
        test_id = ""  # 单例测试无前缀
        
        # 1. 验证文件
        if not self.validate_file(video_file):
            return {"success": False, "error": "文件验证失败"}
        
        # 2. 上传文件
        server_file_path = self.upload_file(video_file, test_id)
        if not server_file_path:
            return {"success": False, "error": "文件上传失败"}
        
        # 3. 提交处理任务
        task_id = self.process_video(server_file_path, test_id)
        if not task_id:
            return {"success": False, "error": "处理请求失败"}
        
        # 4. 等待任务完成
        task_result = self.wait_for_task_completion(task_id, test_id=test_id)
        if not task_result:
            return {"success": False, "error": "任务处理失败或超时"}
        
        # 5. 下载结果文件
        output_file = task_result.get("outputFile", "")
        if not output_file:
            return {"success": False, "error": "任务结果无输出文件路径"}
        
        # 生成本地保存路径（基于任务ID，避免重名）
        local_output_dir = os.path.join("test_results", task_id)
        os.makedirs(local_output_dir, exist_ok=True)
        local_filename = os.path.join(local_output_dir, os.path.basename(output_file))
        
        download_success = self.download_file(output_file, local_filename, test_id)
        
        # 统计总耗时
        total_time = time.time() - start_time
        
        if download_success:
            print(f"\n🎉 {test_name} 成功完成!")
            print(f"⏱️  总耗时: {total_time:.1f}秒")
            print(f"📁 本地文件: {local_filename}")
            return {
                "success": True,
                "time": total_time,
                "task_id": task_id,
                "local_file": local_filename
            }
        else:
            return {"success": False, "error": "文件下载失败", "task_id": task_id}

    def concurrent_test(self, video_file: str, num_concurrent: int = 5) -> List[Dict]:
        """执行并发测试（多线程同时运行多个单流程）"""
        print(f"\n{'='*60}")
        print(f"🔥 开始并发测试 - {num_concurrent}个任务同时执行")
        print(f"{'='*60}")
        
        start_time = time.time()
        results: List[Dict] = []
        os.makedirs("test_results", exist_ok=True)
        
        def run_single_task(task_index: int) -> Dict[str, Any]:
            """单个并发任务的执行函数"""
            task_id_prefix = f"[任务{task_index+1}] "
            return self.single_test(
                video_file,
                test_name=f"并发任务{task_index+1}/{num_concurrent}"
            )
        
        # 使用线程池执行并发任务
        with ThreadPoolExecutor(max_workers=num_concurrent) as executor:
            # 提交所有任务
            future_to_task = {
                executor.submit(run_single_task, i): i 
                for i in range(num_concurrent)
            }
            
            # 收集结果
            for future in as_completed(future_to_task):
                task_index = future_to_task[future]
                try:
                    result = future.result()
                    results.append({
                        "task_index": task_index,
                        **result
                    })
                except Exception as e:
                    print(f"[任务{task_index+1}] ❌ 任务崩溃: {str(e)}")
                    results.append({
                        "task_index": task_index,
                        "success": False,
                        "error": f"任务崩溃: {str(e)}"
                    })
        
        # 统计并发测试结果
        total_time = time.time() - start_time
        success_count = sum(1 for r in results if r.get("success"))
        failed_count = len(results) - success_count
        
        print(f"\n{'='*60}")
        print("📊 并发测试结果汇总")
        print(f"{'='*60}")
        print(f"总任务数: {num_concurrent}")
        print(f"✅ 成功: {success_count} 个")
        print(f"❌ 失败: {failed_count} 个")
        print(f"⏱️  总耗时: {total_time:.1f}秒")
        print(f"📈 平均任务耗时: {total_time/num_concurrent:.1f}秒")
        
        # 打印失败详情
        if failed_count > 0:
            print("\n❌ 失败任务详情:")
            for r in results:
                if not r.get("success"):
                    print(f"  任务{r['task_index']+1}: {r.get('error', '未知错误')}")
        
        return results

def main():
    # 解析命令行参数
    parser = argparse.ArgumentParser(description="视频处理系统全流程测试工具")
    parser.add_argument("--server", default=DEFAULT_SERVER_URL, 
                      help=f"服务器地址 (默认: {DEFAULT_SERVER_URL})")
    parser.add_argument("--file", required=True, 
                      help="测试视频文件路径（本地）")
    parser.add_argument("--concurrent", type=int, default=0, 
                      help="并发任务数量（0=仅单例测试，默认0）")
    parser.add_argument("--timeout", type=int, default=300, 
                      help="任务超时时间（秒，默认300）")
    args = parser.parse_args()
    
    # 初始化测试器
    tester = VideoSystemTester(args.server)
    
    try:
        if args.concurrent > 0:
            # 执行并发测试
            tester.concurrent_test(args.file, args.concurrent)
        else:
            # 执行单例测试
            tester.single_test(args.file)
            
    except KeyboardInterrupt:
        print("\n⚠️ 测试被用户中断")
    except Exception as e:
        print(f"\n❌ 测试框架异常: {str(e)}")

if __name__ == "__main__":
    main()