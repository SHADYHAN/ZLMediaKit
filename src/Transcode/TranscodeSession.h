/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_TRANSCODESESSION_H
#define ZLMEDIAKIT_TRANSCODESESSION_H

#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include "Util/TimeTicker.h"
#include "Network/Session.h"
#include "Common/MediaSource.h"
#include "TranscodeConfig.h"

namespace mediakit {

enum class TranscodeState {
    Idle,
    Starting,
    Running,
    Stopping,
    Stopped,
    Error
};

struct TranscodeInfo {
    std::string input_url;
    std::string output_url;
    std::string template_name;
    std::string app;
    std::string stream;
    TranscodeState state = TranscodeState::Idle;
    std::string error_msg;
    uint64_t start_time = 0;
    uint64_t bytes_in = 0;
    uint64_t bytes_out = 0;
    int frames_in = 0;
    int frames_out = 0;
    float fps = 0.0f;
    float bitrate = 0.0f;
};

class TranscodeSession : public std::enable_shared_from_this<TranscodeSession> {
public:
    using Ptr = std::shared_ptr<TranscodeSession>;
    using onTranscodeResult = std::function<void(const TranscodeInfo &info, bool success, const std::string &error)>;
    
    TranscodeSession(const std::string &input_url, 
                     const std::string &output_url,
                     const std::string &template_name,
                     const std::string &app,
                     const std::string &stream);
    ~TranscodeSession();
    
    // 启动转码
    bool start(const onTranscodeResult &callback = nullptr);
    
    // 停止转码
    void stop();
    
    // 获取转码信息
    TranscodeInfo getInfo() const;
    
    // 是否正在运行
    bool isRunning() const { return _state == TranscodeState::Running; }
    
    // 获取会话ID
    std::string getSessionId() const { return _session_id; }
    
    // 设置进度回调
    void setProgressCallback(const std::function<void(const TranscodeInfo &)> &callback);

private:
    void onTranscodeStart();
    void onTranscodeStop();
    void onTranscodeError(const std::string &error);
    void updateProgress();
    
    bool startFFmpegProcess();
    void stopFFmpegProcess();
    void watchFFmpegProcess();
    void parseFFmpegOutput(const std::string &line);
    
    std::string buildFFmpegCommand() const;
    std::string getHWAccelParams() const;
    std::string getOutputParams() const;
    
private:
    std::string _session_id;
    std::string _input_url;
    std::string _output_url;
    std::string _template_name;
    std::string _app;
    std::string _stream;
    
    std::shared_ptr<TranscodeTemplate> _template;
    std::atomic<TranscodeState> _state{TranscodeState::Idle};
    
    onTranscodeResult _result_callback;
    std::function<void(const TranscodeInfo &)> _progress_callback;
    
    // FFmpeg进程相关
    int _ffmpeg_pid = -1;
    int _stdout_fd = -1;
    int _stderr_fd = -1;
    
    // 统计信息
    mutable std::mutex _info_mutex;
    TranscodeInfo _info;
    toolkit::Ticker _start_ticker;
    
    // 异步线程
    std::thread _watch_thread;
    std::atomic<bool> _exit_flag{false};
};

} // namespace mediakit

#endif // ZLMEDIAKIT_TRANSCODESESSION_H