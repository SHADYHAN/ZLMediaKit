/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "TranscodeSession.h"
#include "Util/logger.h"
#include "Util/util.h"
#include "Util/CMD.h"
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <regex>
#include <sstream>

using namespace std;
using namespace toolkit;

namespace mediakit {

TranscodeSession::TranscodeSession(const string &input_url, 
                                   const string &output_url,
                                   const string &template_name,
                                   const string &app,
                                   const string &stream)
    : _input_url(input_url)
    , _output_url(output_url)
    , _template_name(template_name)
    , _app(app)
    , _stream(stream) {
    
    _session_id = makeRandStr(16);
    _template = TranscodeConfig::Instance().getTemplate(template_name);
    
    // 初始化转码信息
    _info.input_url = input_url;
    _info.output_url = output_url;
    _info.template_name = template_name;
    _info.app = app;
    _info.stream = stream;
    _info.state = TranscodeState::Idle;
}

TranscodeSession::~TranscodeSession() {
    stop();
}

bool TranscodeSession::start(const onTranscodeResult &callback) {
    if (_state != TranscodeState::Idle) {
        if (callback) {
            callback(_info, false, "Transcode session already started or not idle");
        }
        return false;
    }
    
    if (!_template) {
        string error = "Template not found: " + _template_name;
        if (callback) {
            callback(_info, false, error);
        }
        return false;
    }
    
    _result_callback = callback;
    _state = TranscodeState::Starting;
    _exit_flag = false;
    
    InfoL << "Starting transcode session: " << _session_id 
          << ", input: " << _input_url 
          << ", output: " << _output_url
          << ", template: " << _template_name;
    
    if (!startFFmpegProcess()) {
        _state = TranscodeState::Error;
        if (_result_callback) {
            _result_callback(_info, false, _info.error_msg);
        }
        return false;
    }
    
    onTranscodeStart();
    return true;
}

void TranscodeSession::stop() {
    if (_state == TranscodeState::Idle || _state == TranscodeState::Stopped) {
        return;
    }
    
    InfoL << "Stopping transcode session: " << _session_id;
    
    _state = TranscodeState::Stopping;
    _exit_flag = true;
    
    stopFFmpegProcess();
    
    if (_watch_thread.joinable()) {
        _watch_thread.join();
    }
    
    onTranscodeStop();
}

TranscodeInfo TranscodeSession::getInfo() const {
    lock_guard<mutex> lock(_info_mutex);
    return _info;
}

void TranscodeSession::setProgressCallback(const function<void(const TranscodeInfo &)> &callback) {
    _progress_callback = callback;
}

void TranscodeSession::onTranscodeStart() {
    {
        lock_guard<mutex> lock(_info_mutex);
        _info.state = TranscodeState::Running;
        _info.start_time = getCurrentMillisecond();
    }
    
    _state = TranscodeState::Running;
    _start_ticker.resetTime();
    
    // 启动监控线程
    _watch_thread = thread([this]() {
        watchFFmpegProcess();
    });
    
    InfoL << "Transcode session started: " << _session_id;
}

void TranscodeSession::onTranscodeStop() {
    {
        lock_guard<mutex> lock(_info_mutex);
        _info.state = TranscodeState::Stopped;
    }
    
    _state = TranscodeState::Stopped;
    InfoL << "Transcode session stopped: " << _session_id;
}

void TranscodeSession::onTranscodeError(const string &error) {
    {
        lock_guard<mutex> lock(_info_mutex);
        _info.state = TranscodeState::Error;
        _info.error_msg = error;
    }
    
    _state = TranscodeState::Error;
    ErrorL << "Transcode session error: " << _session_id << ", error: " << error;
    
    if (_result_callback) {
        _result_callback(_info, false, error);
    }
}

bool TranscodeSession::startFFmpegProcess() {
    string cmd = buildFFmpegCommand();
    InfoL << "FFmpeg command: " << cmd;
    
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        onTranscodeError("Failed to create pipes");
        return false;
    }
    
    _ffmpeg_pid = fork();
    if (_ffmpeg_pid == -1) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        onTranscodeError("Failed to fork FFmpeg process");
        return false;
    }
    
    if (_ffmpeg_pid == 0) {
        // 子进程
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // 执行FFmpeg命令
        vector<string> args;
        stringstream ss(cmd);
        string arg;
        while (ss >> arg) {
            args.push_back(arg);
        }
        
        vector<char*> argv;
        for (auto &arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        execvp(argv[0], argv.data());
        exit(1);
    } else {
        // 父进程
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        _stdout_fd = stdout_pipe[0];
        _stderr_fd = stderr_pipe[0];
        
        // 设置非阻塞
        fcntl(_stdout_fd, F_SETFL, O_NONBLOCK);
        fcntl(_stderr_fd, F_SETFL, O_NONBLOCK);
        
        return true;
    }
}

void TranscodeSession::stopFFmpegProcess() {
    if (_ffmpeg_pid > 0) {
        InfoL << "Stopping FFmpeg process PID: " << _ffmpeg_pid;
        
        // 先发送SIGTERM信号
        if (kill(_ffmpeg_pid, SIGTERM) != 0) {
            WarnL << "Failed to send SIGTERM to FFmpeg process: " << strerror(errno);
        }
        
        // 等待进程结束：这里采用较短的总等待时间，避免在程序退出(Ctrl+C)时长时间阻塞
        int status;
        int wait_count = 0;
        const int max_wait_count = 5; // 最多等待约1秒
        
        while (wait_count < max_wait_count) {
            int ret = waitpid(_ffmpeg_pid, &status, WNOHANG);
            if (ret == _ffmpeg_pid) {
                // 进程已经结束
                InfoL << "FFmpeg process terminated gracefully";
                break;
            } else if (ret == -1) {
                // 出错或进程不存在
                WarnL << "waitpid failed: " << strerror(errno);
                break;
            }
            
            // 进程还在运行，继续等待
            usleep(200000); // 等待200毫秒
            wait_count++;
        }
        
        // 如果还没结束，强制杀死
        if (wait_count >= max_wait_count) {
            WarnL << "FFmpeg process didn't respond to SIGTERM, sending SIGKILL";
            if (kill(_ffmpeg_pid, SIGKILL) != 0) {
                WarnL << "Failed to send SIGKILL to FFmpeg process: " << strerror(errno);
            }
            
            // 最后等待进程彻底结束
            int final_wait_count = 0;
            const int max_final_wait = 5; // 最多再等约1秒
            while (final_wait_count < max_final_wait) {
                int ret = waitpid(_ffmpeg_pid, &status, WNOHANG);
                if (ret == _ffmpeg_pid || ret == -1) {
                    break;
                }
                usleep(200000); // 等待200毫秒
                final_wait_count++;
            }
        }
        
        _ffmpeg_pid = -1;
        InfoL << "FFmpeg process cleanup completed";
    }
    
    if (_stdout_fd >= 0) {
        close(_stdout_fd);
        _stdout_fd = -1;
    }
    
    if (_stderr_fd >= 0) {
        close(_stderr_fd);
        _stderr_fd = -1;
    }
}

void TranscodeSession::watchFFmpegProcess() {
    char buffer[4096];
    string output_buffer;
    
    while (!_exit_flag && _ffmpeg_pid > 0) {
        // 检查进程是否还在运行
        int status;
        int ret = waitpid(_ffmpeg_pid, &status, WNOHANG);
        if (ret > 0) {
            // 进程已结束
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                InfoL << "FFmpeg process exited normally: " << _session_id;
                if (_result_callback) {
                    _result_callback(_info, true, "");
                }
            } else {
                string error = "FFmpeg process exited with error, status: " + to_string(status);
                onTranscodeError(error);
            }
            break;
        }
        
        // 读取stderr输出(FFmpeg的日志输出到stderr)
        ssize_t bytes_read = read(_stderr_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            output_buffer += buffer;
            
            // 按行解析输出
            size_t pos = 0;
            while ((pos = output_buffer.find('\n')) != string::npos) {
                string line = output_buffer.substr(0, pos);
                output_buffer.erase(0, pos + 1);
                parseFFmpegOutput(line);
            }
        }
        
        usleep(100000); // 100ms
    }
}

void TranscodeSession::parseFFmpegOutput(const string &line) {
    // 解析FFmpeg输出，提取进度信息，并输出原始日志方便排查问题
    DebugL << "FFmpeg[" << _session_id << "]: " << line;
    // 示例输出: frame=  123 fps= 25 q=28.0 size=    1024kB time=00:00:05.12 bitrate=1638.4kbits/s speed=1.02x
    
    static regex frame_regex(R"(frame=\s*(\d+))");
    static regex fps_regex(R"(fps=\s*([\d\.]+))");
    static regex bitrate_regex(R"(bitrate=\s*([\d\.]+)kbits/s)");
    static regex size_regex(R"(size=\s*(\d+)kB)");
    
    smatch match;
    bool updated = false;
    
    {
        lock_guard<mutex> lock(_info_mutex);
        
        if (regex_search(line, match, frame_regex)) {
            _info.frames_out = stoi(match[1]);
            updated = true;
        }
        
        if (regex_search(line, match, fps_regex)) {
            _info.fps = stof(match[1]);
            updated = true;
        }
        
        if (regex_search(line, match, bitrate_regex)) {
            _info.bitrate = stof(match[1]);
            updated = true;
        }
        
        if (regex_search(line, match, size_regex)) {
            _info.bytes_out = stoll(match[1]) * 1024;
            updated = true;
        }
    }
    
    if (updated && _progress_callback) {
        _progress_callback(_info);
    }
}

string TranscodeSession::buildFFmpegCommand() const {
    stringstream cmd;
    
    cmd << TranscodeConfig::Instance().ffmpegBin();
    
    // 硬件加速参数
    string hw_accel = getHWAccelParams();
    if (!hw_accel.empty()) {
        cmd << " " << hw_accel;
    }
    
    // 输入参数
    cmd << " -i " << _input_url;
    
    // 模板参数
    if (_template) {
        cmd << _template->getFFmpegParams();
    }
    
    // 输出参数
    cmd << " " << getOutputParams();
    
    // 输出地址
    cmd << " " << _output_url;
    
    // 覆盖输出文件
    cmd << " -y";
    
    return cmd.str();
}

string TranscodeSession::getHWAccelParams() const {
    auto hw_accel = TranscodeConfig::Instance().hwAccelType();
    switch (hw_accel) {
    case HWAccelType::NVIDIA_NVENC:
        return "-hwaccel cuda -hwaccel_output_format cuda";
    case HWAccelType::INTEL_QSV:
        return "-hwaccel qsv -hwaccel_output_format qsv";
    case HWAccelType::AMD_VCE:
        return "-hwaccel d3d11va -hwaccel_output_format d3d11";
    case HWAccelType::VAAPI:
        return "-hwaccel vaapi -hwaccel_output_format vaapi -vaapi_device /dev/dri/renderD128";
    default:
        return "";
    }
}

string TranscodeSession::getOutputParams() const {
    // 默认输出参数
    return "-f flv";
}

} // namespace mediakit