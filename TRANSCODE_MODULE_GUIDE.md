# ZLMediaKit 转码模块使用指南

## 📋 目录
- [概述](#概述)
- [功能特性](#功能特性)
- [配置说明](#配置说明)
- [API接口](#api接口)
- [使用示例](#使用示例)
- [性能调优](#性能调优)
- [故障排除](#故障排除)

## 概述

ZLMediaKit转码模块是一个高性能的实时流媒体转码解决方案，支持多码率、多格式的并发转码。基于FFmpeg，提供硬件加速支持，适用于直播、点播、监控等多种场景。

### 核心特性
- 🚀 **高性能**：支持NVIDIA/Intel/AMD硬件加速
- 🔄 **多码率**：同时生成多个分辨率/比特率版本
- 📡 **实时转码**：低延迟流媒体转码
- 🎯 **智能调度**：自动资源管理和任务调度
- 🔧 **灵活配置**：模板化配置系统
- 📊 **监控统计**：详细的转码状态和统计信息

## 功能特性

### 支持的编码格式
- **视频编码**：H.264, H.265/HEVC
- **音频编码**：AAC, MP3, OPUS
- **硬件加速**：NVENC, Intel QSV, AMD VCE, VAAPI

### 转码能力
- **分辨率转换**：自动缩放到目标分辨率
- **码率控制**：CBR/VBR码率模式
- **帧率调整**：支持帧率转换
- **音频处理**：音频编码参数调整

### 智能功能
- **按需转码**：检测到播放器时自动启动
- **规则引擎**：基于流名称自动应用转码策略
- **资源管理**：智能并发控制和负载均衡
- **故障恢复**：自动重试和异常处理

## 配置说明

### 基础配置 `[transcode]`

```ini
[transcode]
# 是否启用转码功能
enable=1

# 最大并发转码任务数（根据硬件性能调整）
# CPU转码: 建议2-4
# NVIDIA RTX 3060: 建议4-6
# NVIDIA RTX 4080: 建议8-12
maxConcurrent=8

# 硬件加速类型
# none: 纯CPU转码（不添加任何 -hwaccel 参数）
# nvidia: NVIDIA NVENC (推荐RTX系列显卡)
# intel: Intel Quick Sync Video
# amd: AMD VCE
# vaapi: Linux VAAPI (Intel集显)
hwAccel=none

# 转码临时文件目录
tempDir=./temp/transcode

# 转码超时时间(秒)
# 建议根据内容长度调整：直播流建议120-300秒
timeoutSec=120

# FFmpeg二进制文件路径 (可选)
ffmpegBin=ffmpeg

# 是否自动启动转码（检测到媒体源时）
autoStart=1

# 异步解码队列最大长度 (3~1000)，对应底层 TaskManager::setMaxTaskSize
maxAsyncFrameSize=30
```

当 `hwAccel=none` 时，生成的 FFmpeg 命令不会添加任何 `-hwaccel` 相关参数，解码和编码都在 CPU 上完成，便于排查卡顿/花屏问题。

### 转码模板配置 `[transcode_templates]`

转码模板定义了不同清晰度的编码参数，本分支中默认提供了针对 H.264 NVENC 的低延迟 CBR 模板，并给出了等价风格的 CPU 备用模板：

```ini
[transcode_templates]
# 推荐：模板名直接等于转码后流的 app 名（例如 480/720），方便按 app 自动匹配

# NVIDIA NVENC 优化模板（当前默认使用）
480=-vcodec h264_nvenc -preset p2 -rc cbr -profile:v baseline -level 3.1 -b:v 800k  -maxrate 800k  -bufsize 1600k -g 25 -bf 0 -forced-idr 1 -vf scale=854:480,fps=25,setpts=N/TB/25  -vsync cfr -acodec aac -b:a 96k
720=-vcodec h264_nvenc -preset p2 -rc cbr -profile:v baseline -level 3.1 -b:v 2000k -maxrate 2000k -bufsize 2000k -g 25 -bf 0 -forced-idr 1 -vf scale=1280:720,fps=25,setpts=N/TB/25 -vsync cfr -acodec aac -b:a 128k

# CPU 软件编码模板（备用方案，与 NVENC 模板保持相同 GOP/码率/滤镜，便于切换对比）
480_cpu=-vcodec libx264 -preset veryfast -tune zerolatency -profile:v baseline -level 3.1 -b:v 800k  -maxrate 800k  -bufsize 1600k -g 25 -bf 0 -forced-idr 1 -vf scale=854:480,fps=25,setpts=N/TB/25  -vsync cfr -acodec aac -b:a 96k
720_cpu=-vcodec libx264 -preset veryfast -tune zerolatency -profile:v baseline -level 3.1 -b:v 2000k -maxrate 2000k -bufsize 2000k -g 25 -bf 0 -forced-idr 1 -vf scale=1280:720,fps=25,setpts=N/TB/25 -vsync cfr -acodec aac -b:a 128k
1080_cpu=-vcodec libx264 -preset veryfast -tune zerolatency -profile:v baseline -level 4.0 -b:v 4000k -maxrate 6000k -bufsize 8000k -g 25 -bf 0 -forced-idr 1 -vf scale=1920:1080,fps=25,setpts=N/TB/25 -vsync cfr -acodec aac -b:a 192k
```

#### 模板参数说明

**NVENC参数**：
- `-preset p1-p7`: 编码预设，p1最快p7最慢但质量最好
- `-rc vbr/cbr`: 码率控制模式，VBR可变码率，CBR恒定码率
- `-cq 18-30`: 质量参数，越小质量越好
- `-vf scale` / `-vf scale_cuda`: 默认使用 CPU 缩放；在显卡资源充足的场景下，可以改用 GPU 缩放以减轻 CPU 压力

**通用参数**：
- `-b:v`: 视频码率
- `-maxrate/-bufsize`: VBR模式的最大码率和缓冲区大小
- `-vf scale`: 视频缩放滤镜
- `-acodec/-b:a`: 音频编码器和码率

### 转码规则配置 `[transcode_rules]`

规则引擎支持通配符匹配，自动为符合条件的流应用转码：

```ini
[transcode_rules]
# 直播流多码率转码
live/*=480p,720p,1080p

# 录制回放只转720p
record/*=720p

# 监控流低码率转码
monitor/*=480p

# 特定流的高质量转码
live/vip_stream=720p_60fps,1080p

# 移动端优化流
mobile/*=480p,720p

# 会议流转码
meeting/*=720p_lowlatency
```

**规则匹配语法**：
- `*`: 匹配任意字符
- `live/*`: 匹配live应用下所有流
- `*/mobile`: 匹配所有应用下名为mobile的流
- `exact_name`: 精确匹配流名称

## API接口

转码模块提供6个RESTful API接口，支持完整的转码任务管理：

### 1. 启动转码任务

**接口**：`POST /index/api/startTranscode`

**参数**：
- `secret`: API密钥（必需）
- `app`: 应用名（必需）
- `stream`: 流名称（必需）  
- `templates`: 转码模板列表，逗号分隔（可选）
- `input_url`: 自定义输入URL（可选）
- `vhost`: 虚拟主机（可选，默认__defaultVhost__）

**响应**：
```json
{
  "code": 0,
  "msg": "success",
  "data": {
    "task_id": "live_test_20240131_123456",
    "templates": ["480p", "720p", "1080p"]
  }
}
```

**示例**：
```bash
curl -X POST "http://127.0.0.1/index/api/startTranscode?secret=YOUR_SECRET&app=live&stream=test&templates=720p,480p"
```

### 2. 停止转码任务

**接口**：`POST /index/api/stopTranscode`

**参数**：
- `secret`: API密钥（必需）
- `app`: 应用名（与stream同时使用）
- `stream`: 流名称（与app同时使用）
- `task_id`: 任务ID（可替代app+stream）

**响应**：
```json
{
  "code": 0,
  "msg": "success"
}
```

**示例**：
```bash
# 按应用和流名停止
curl -X POST "http://127.0.0.1/index/api/stopTranscode?secret=YOUR_SECRET&app=live&stream=test"

# 按任务ID停止
curl -X POST "http://127.0.0.1/index/api/stopTranscode?secret=YOUR_SECRET&task_id=live_test_20240131_123456"
```

### 3. 获取转码任务列表

**接口**：`GET /index/api/getTranscodeList`

**参数**：
- `secret`: API密钥（必需）

**响应**：
```json
{
  "code": 0,
  "msg": "success", 
  "data": [
    {
      "task_id": "live_test_20240131_123456",
      "app": "live",
      "stream": "test",
      "input_url": "rtmp://127.0.0.1/live/test",
      "create_time": 1706686956,
      "auto_started": false,
      "total_sessions": 3,
      "running_sessions": 3,
      "error_sessions": 0,
      "templates": ["480p", "720p", "1080p"]
    }
  ]
}
```

### 4. 获取转码任务详情

**接口**：`GET /index/api/getTranscodeInfo`

**参数**：
- `secret`: API密钥（必需）
- `app`: 应用名（与stream同时使用）
- `stream`: 流名称（与app同时使用）
- `task_id`: 任务ID（可替代app+stream）

**响应**：
```json
{
  "code": 0,
  "msg": "success",
  "data": {
    "task_id": "live_test_20240131_123456",
    "app": "live", 
    "stream": "test",
    "input_url": "rtmp://127.0.0.1/live/test",
    "create_time": 1706686956,
    "sessions": [
      {
        "template_name": "720p",
        "state": "Running",
        "output_url": "rtmp://127.0.0.1/live/test_720p",
        "start_time": 1706686960,
        "fps": 25.0,
        "bitrate": 2048.5,
        "frames_in": 1500,
        "frames_out": 1498
      }
    ]
  }
}
```

### 5. 获取转码统计信息

**接口**：`GET /index/api/getTranscodeStats`

**参数**：
- `secret`: API密钥（必需）

**响应**：
```json
{
  "code": 0,
  "msg": "success",
  "data": {
    "total_tasks": 5,
    "running_tasks": 3,
    "total_sessions": 12,
    "running_sessions": 9,
    "max_concurrent": 8,
    "hw_accel": "nvidia",
    "uptime": 86400,
    "sessions": [
      {
        "app": "live",
        "stream": "test", 
        "template_name": "720p",
        "state": "Running",
        "fps": 25.0,
        "bitrate": 2048.5
      }
    ]
  }
}
```

### 6. 获取转码模板列表

**接口**：`GET /index/api/getTranscodeTemplates`

**参数**：
- `secret`: API密钥（必需）

**响应**：
```json
{
  "code": 0,
  "msg": "success",
  "data": {
    "templates": [
      {
        "name": "480p",
        "video_codec": "h264_nvenc",
        "audio_codec": "aac", 
        "video_bitrate": 800,
        "audio_bitrate": 96,
        "width": 854,
        "height": 480,
        "params": "-vcodec h264_nvenc -preset p2 -rc cbr -profile:v baseline -level 3.1 -b:v 800k -maxrate 800k -bufsize 1600k -g 25 -bf 0 -forced-idr 1 -vf scale=854:480,fps=25,setpts=N/TB/25 -vsync cfr -acodec aac -b:a 96k"
      }
    ]
  }
}
```

## 使用示例

### Watcher 相关 HTTP 接口

本分支从主仓合并了 watcher 统计相关接口，可用于排查转码流的真实观众和来源 IP：

#### 1. 获取 watcher 列表 - `/index/api/getWatchers`

**接口**：`GET /index/api/getWatchers`

**参数**：
- `secret`: API密钥（必需）
- `schema`: 协议类型过滤（可选，如 `rtsp`/`rtmp`/`rtc` 等）
- `vhost`: 虚拟主机过滤（可选）
- `app`: 应用名过滤（可选）
- `stream`: 流名过滤（可选）
- `ip`: 观众IP过滤（可选）

**说明**：
- 内部维护一个最近 watcher 环形队列（上限约300条），记录最近一段时间内的观众。
- 对于非 `rtc` 协议，会自动过滤掉已经离线的会话以及已不存在的流。
- 对于 WebRTC，服务器会在 `/index/api/webrtc` 中解析 `X-Real-IP`/`X-Forwarded-For`，将真实客户端 IP 注入 watcher 记录。

**响应示例**（简化）：

```json
{
  "code": 0,
  "msg": "success",
  "data": [
    {
      "schema": "rtc",
      "vhost": "__defaultVhost__",
      "app": "live",
      "stream": "test",
      "ip": "203.0.113.10",
      "port": 52345,
      "id": "session_xxx",
      "protocol": "rtc",
      "params": "session=abcd1234&type=play",
      "start_stamp": 1700000000
    }
  ]
}
```

#### 2. 获取带 watcher 信息的媒体列表 - `/index/api/getMediaListWithWatchers`

**接口**：`GET /index/api/getMediaListWithWatchers`

**参数**：
- `secret`: API密钥（必需）
- `schema`/`vhost`/`app`/`stream`: 与 `/index/api/getMediaList` 一致的过滤条件（可选）

**说明**：
- 在原有 `/index/api/getMediaList` 返回的每一路流对象上，额外增加 `watchers` 字段。
- `watchers` 是一个精简数组，只包含部分代表性观众：
  - 最多返回前4个（最早） + 最新1个；
  - 每个元素仅包含 `ip` 和 `port` 两个字段，便于快速排查来源分布。

**响应示例**（片段）：

```json
{
  "code": 0,
  "msg": "success",
  "data": [
    {
      "schema": "rtsp",
      "vhost": "__defaultVhost__",
      "app": "live",
      "stream": "test",
      "watchers": [
        {"ip": "203.0.113.10", "port": 52345},
        {"ip": "198.51.100.20", "port": 52346}
      ]
    }
  ]
}
```

这些接口可与转码模块的统计接口配合使用，例如：先通过 `/index/api/getTranscodeList` 找到活跃的转码任务，再使用 `/index/api/getWatchers` / `/index/api/getMediaListWithWatchers` 分析真实观众分布和来源 IP。

### 基础使用流程

1. **配置转码参数**
```ini
# 编辑 conf/config.ini
[transcode]
enable=1
maxConcurrent=4
hwAccel=none

[transcode_templates]
720=-vcodec h264_nvenc -preset p2 -rc cbr -profile:v baseline -level 3.1 -b:v 2000k -maxrate 2000k -bufsize 2000k -g 25 -bf 0 -forced-idr 1 -vf scale=1280:720,fps=25,setpts=N/TB/25 -vsync cfr -acodec aac -b:a 128k

[transcode_rules]
live/*=720
```

2. **启动MediaServer**
```bash
./MediaServer
```

3. **推送原始流**
```bash
ffmpeg -re -i input.mp4 -c copy -f rtmp rtmp://127.0.0.1/live/test
```

4. **启动转码**
```bash
curl -X POST "http://127.0.0.1/index/api/startTranscode?secret=YOUR_SECRET&app=live&stream=test&templates=720p"
```

5. **播放转码流**
```bash
ffplay rtmp://127.0.0.1/live/test_720p
```

### 高级使用场景

#### 场景1：多码率直播

**配置**：
```ini
[transcode_templates]
480=-vcodec h264_nvenc -preset p2 -rc cbr -profile:v baseline -level 3.1 -b:v 800k  -maxrate 800k  -bufsize 1600k -g 25 -bf 0 -forced-idr 1 -vf scale=854:480,fps=25,setpts=N/TB/25  -vsync cfr -acodec aac -b:a 96k
720=-vcodec h264_nvenc -preset p2 -rc cbr -profile:v baseline -level 3.1 -b:v 2000k -maxrate 2000k -bufsize 2000k -g 25 -bf 0 -forced-idr 1 -vf scale=1280:720,fps=25,setpts=N/TB/25 -vsync cfr -acodec aac -b:a 128k
1080=-vcodec h264_nvenc -preset p2 -rc cbr -profile:v baseline -level 4.0 -b:v 4000k -maxrate 6000k -bufsize 8000k -g 25 -bf 0 -forced-idr 1 -vf scale=1920:1080,fps=25,setpts=N/TB/25 -vsync cfr -acodec aac -b:a 192k

[transcode_rules]
live/*=480,720,1080
```

**自动转码**：当推送到`live/`应用的流会自动生成3个码率版本。

#### 场景2：按需转码

**配置**：
```ini
[transcode]
autoStart=1

[transcode_rules]
vod/*=720p
```

**行为**：当有播放器请求`vod/movie_720p`时，自动启动`vod/movie`的720p转码。

#### 场景3：CPU+GPU混合转码

**配置**：
```ini
[transcode_templates]
# GPU转码高清版本
1080_gpu=-vcodec h264_nvenc -preset p2 -rc cbr -profile:v baseline -level 4.0 -b:v 4000k -maxrate 6000k -bufsize 8000k -g 25 -bf 0 -forced-idr 1 -vf scale=1920:1080,fps=25,setpts=N/TB/25 -vsync cfr -acodec aac -b:a 192k

# CPU转码低清版本
480_cpu=-vcodec libx264 -preset veryfast -tune zerolatency -profile:v baseline -level 3.1 -b:v 800k  -maxrate 800k  -bufsize 1600k -g 25 -bf 0 -forced-idr 1 -vf scale=854:480,fps=25,setpts=N/TB/25  -vsync cfr -acodec aac -b:a 96k

[transcode_rules]
live/hd_*=1080_gpu,480_cpu
```

### 监控和管理

#### 实时监控转码状态
```bash
# 获取所有转码任务
curl "http://127.0.0.1/index/api/getTranscodeList?secret=YOUR_SECRET" | jq

# 获取统计信息
curl "http://127.0.0.1/index/api/getTranscodeStats?secret=YOUR_SECRET" | jq

# 获取特定任务详情
curl "http://127.0.0.1/index/api/getTranscodeInfo?secret=YOUR_SECRET&app=live&stream=test" | jq
```

#### 批量管理脚本
```bash
#!/bin/bash
# 批量启动转码

streams=("stream1" "stream2" "stream3")
templates="480p,720p,1080p"

for stream in "${streams[@]}"; do
    curl -X POST "http://127.0.0.1/index/api/startTranscode?secret=YOUR_SECRET&app=live&stream=$stream&templates=$templates"
    echo "Started transcode for $stream"
done
```

## 性能调优

### 硬件优化

#### NVIDIA GPU优化
```ini
[transcode]
# RTX 3060: 4-6个并发
# RTX 3080: 6-8个并发
# RTX 4080: 8-12个并发
maxConcurrent=8
hwAccel=nvidia

[transcode_templates]
# 使用低延迟 CBR 模板，便于与 CPU 模板对比
720=-vcodec h264_nvenc -preset p2 -rc cbr -profile:v baseline -level 3.1 -b:v 2000k -maxrate 2000k -bufsize 2000k -g 25 -bf 0 -forced-idr 1 -vf scale=1280:720,fps=25,setpts=N/TB/25 -vsync cfr -acodec aac -b:a 128k
```

#### CPU优化
```ini
[transcode_templates]
# CPU转码使用 faster/veryfast 预设，配合与 NVENC 相同的 GOP/码率
720_cpu=-vcodec libx264 -preset veryfast -tune zerolatency -profile:v baseline -level 3.1 -b:v 2000k -maxrate 2000k -bufsize 2000k -g 25 -bf 0 -forced-idr 1 -vf scale=1280:720,fps=25,setpts=N/TB/25 -vsync cfr -acodec aac -b:a 128k
```

### 内存和存储优化

```ini
[transcode]
# 使用SSD存储临时文件
tempDir=/ssd/temp/transcode

# 减少超时时间避免占用资源
timeoutSec=120
```

### 网络优化

```ini
[transcode_templates]
# 使用CBR模式保证网络稳定性
720p_stable=-vcodec h264_nvenc -preset p4 -rc cbr -b:v 2000k -vf scale_cuda=1280:720 -acodec aac -b:a 128k
```

## 故障排除

### 常见问题

#### 1. 转码启动失败

**症状**：API返回失败，日志显示启动错误

**排查步骤**：
```bash
# 检查FFmpeg是否安装
ffmpeg -version

# 检查GPU驱动（NVIDIA）
nvidia-smi

# 检查配置文件
curl "http://127.0.0.1/index/api/getTranscodeTemplates?secret=YOUR_SECRET"

# 查看详细错误
curl "http://127.0.0.1/index/api/getTranscodeInfo?secret=YOUR_SECRET&app=live&stream=test"
```

**解决方案**：
- 确保FFmpeg支持所需编码器
- 检查硬件加速驱动
- 验证模板参数语法

#### 2. 转码性能差

**症状**：转码延迟高，CPU/GPU占用率低

**优化方案**：
```ini
# 增加并发数
maxConcurrent=8

# 使用更快预设
720p=-vcodec h264_nvenc -preset p1 -rc cbr -b:v 2000k -vf scale_cuda=1280:720
```

#### 3. 转码卡顿或中断

**症状**：转码过程中断，输出流不稳定

**解决步骤**：
1. 增加超时时间
2. 检查网络连接
3. 使用更稳定的编码参数
4. 监控系统资源使用

### 日志分析

MediaServer日志会记录转码相关信息：

```
[2024-01-31 12:34:56.789] [INFO] Transcode task started: live/test -> 720p
[2024-01-31 12:34:57.123] [WARN] FFmpeg process slow start: 334ms  
[2024-01-31 12:34:58.456] [ERROR] Transcode failed: live/test_720p, error: Encoder initialization failed
```

### 性能监控

#### 系统资源监控
```bash
# GPU使用率（NVIDIA）
nvidia-smi -l 1

# CPU和内存使用率
htop

# 磁盘IO
iotop
```

#### 转码监控
```bash
# 实时监控转码状态
watch -n 1 'curl -s "http://127.0.0.1/index/api/getTranscodeStats?secret=YOUR_SECRET" | jq ".data.running_sessions"'
```

## 总结

ZLMediaKit转码模块提供了企业级的流媒体转码解决方案，具备以下优势：

- ✅ **高性能**：硬件加速支持，单机可支持数十路并发转码
- ✅ **易配置**：模板化配置，规则引擎自动化管理  
- ✅ **高可靠**：完善的错误处理和恢复机制
- ✅ **可监控**：详细的统计信息和状态监控
- ✅ **API完整**：RESTful API支持完整的生命周期管理

通过合理配置和调优，可以满足从小规模直播到大型视频平台的各种转码需求。

---

**版本信息**：ZLMediaKit v20240131  
**更新时间**：2024年1月31日  
**作者**：ZLMediaKit开发团队