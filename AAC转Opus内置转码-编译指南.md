# ZLMediaKit AAC→Opus 内置转码功能

## ✅ 已完成的集成

所有代码已集成完毕，无需手动修改任何文件！

### 新增文件
```
src/Codec/OpusEncoder.h         ← Opus 编码器头文件
src/Codec/OpusEncoder.cpp       ← Opus 编码器实现
```

### 已修改文件
```
src/Common/config.h             ← 添加了转码配置声明
src/Common/config.cpp           ← 添加了转码配置定义和默认值
src/Common/MultiMediaSourceMuxer.h   ← 添加了转码器成员变量
src/Common/MultiMediaSourceMuxer.cpp ← 集成了转码逻辑
conf/config.ini                 ← 添加了转码配置项
```

---

## 🔧 编译步骤

### 1. 清理旧的构建
```bash
rm -rf build
mkdir build
cd build
```

### 2. 配置 CMake
```bash
cmake .. -DENABLE_FFMPEG=ON -DENABLE_WEBRTC=ON
```

### 3. 编译
```bash
make -j$(nproc)
```

---

## ⚙️ 使用方法

### 1. 修改配置文件 `conf/config.ini`

找到 `[protocol]` 部分，启用音频转码：

```ini
[protocol]
# ... 其他配置 ...

# 音频转码配置（用于 WebRTC 播放 AAC 流）
enable_audio_transcode=1        # ← 改为 1 启用转码
audio_transcode_bitrate=64000   # 64kbps（可选：32000-128000）
audio_transcode_sample_rate=48000  # 48kHz
audio_transcode_channels=2      # 立体声（可选：1=单声道）
```

### 2. 启动 MediaServer

```bash
cd release/linux/Release
./MediaServer -c ../../../conf/config.ini
```

### 3. 推流测试

```bash
# 推流（AAC 音频）
ffmpeg -re -i test.mp4 -c:v copy -c:a aac \
    -f rtsp rtsp://127.0.0.1:554/live/test
```

### 4. 播放

- **RTSP/RTMP/HLS**（原始 AAC）：
  ```
  rtsp://127.0.0.1:554/live/test
  rtmp://127.0.0.1:1935/live/test
  ```

- **WebRTC**（自动转码 Opus）：
  ```
  访问 http://127.0.0.1/ 在线播放器
  ```

---

## 📊 功能说明

### 转码流程

```
AAC 推流 → MultiMediaSourceMuxer
              ├─→ AAC 原始流 → RTSP/RTMP/HLS/FMP4/TS
              └─→ AudioTranscoder (AAC→PCM→Opus) → RTSP (WebRTC)
```

### 特性

✅ **按需转码**：只在启用 `enable_audio_transcode=1` 时才转码  
✅ **多协议兼容**：RTSP/RTMP 保持原始 AAC，WebRTC 使用转码后的 Opus  
✅ **异步处理**：解码和编码都是异步的，不阻塞主线程  
✅ **自动重采样**：自动转换采样率和声道数到目标格式  
✅ **资源池优化**：使用对象池减少内存分配开销  

---

## 🔍 日志验证

启动后查看日志，应该看到：

```
[INFO] Audio transcoding enabled for: live/test
[INFO] Creating AudioTranscoder for: live/test, AAC → Opus, 48000Hz, 2ch, 64000bps
[INFO] Creating AudioTranscoder: AAC → Opus, 48000Hz, 2 channels, 64000 bps
[INFO] Opus encoder initialized successfully, sample_rate=48000, channels=2, bitrate=64000, frame_size=960
[INFO] AudioTranscoder created successfully
[INFO] AudioTranscoder created successfully for: live/test
```

---

## 🐛 常见问题

### Q: 编译报错 "Opus encoder not found"
**A:** FFmpeg 未启用 libopus 支持，重新编译 FFmpeg：
```bash
# Ubuntu/Debian
sudo apt install libopus-dev
./configure --enable-libopus ...

# 或使用系统 FFmpeg
sudo apt install ffmpeg libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
```

### Q: WebRTC 播放无声音
**A:** 检查以下几点：
1. ✅ `enable_audio_transcode=1` 已设置
2. ✅ 日志中有 "AudioTranscoder created" 信息
3. ✅ WebRTC SDP 中包含 opus 编码器
4. ✅ 浏览器控制台无报错

### Q: CPU 占用过高
**A:** 降低转码参数：
```ini
audio_transcode_bitrate=32000      # 降低码率
audio_transcode_channels=1         # 改为单声道
audio_transcode_sample_rate=24000  # 降低采样率（不推荐）
```

### Q: 如何只对 WebRTC 转码，RTSP 保持 AAC？
**A:** 已经是这样实现的！当前版本：
- RTSP/RTMP/HLS：播放原始 AAC
- WebRTC（通过 RTSP 协议）：自动使用 Opus

---

## 📝 核心代码位置

- **Opus 编码器**: `src/Codec/OpusEncoder.{h,cpp}`
- **转码器集成**: `src/Common/MultiMediaSourceMuxer.cpp:871-905`
- **配置项**: `src/Common/config.{h,cpp}`
- **配置文件**: `conf/config.ini:98-106`

---

## 🎉 完成！

直接编译运行即可，所有代码已集成！

如果遇到问题，请检查：
1. FFmpeg 是否支持 libopus（`ffmpeg -codecs | grep opus`）
2. 编译时是否启用 `-DENABLE_FFMPEG=ON`
3. 配置文件中 `enable_audio_transcode=1` 是否生效
4. 日志中是否有错误信息

