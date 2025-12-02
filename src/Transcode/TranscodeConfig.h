/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_TRANSCODECONFIG_H
#define ZLMEDIAKIT_TRANSCODECONFIG_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "Util/mini.h"

namespace mediakit {

enum class HWAccelType {
    NONE = 0,
    NVIDIA_NVENC,
    INTEL_QSV,
    AMD_VCE,
    VAAPI
};

struct TranscodeTemplate {
    std::string name;
    std::string video_codec;
    std::string audio_codec;
    std::string video_params;
    std::string audio_params;
    std::string filter_params;
    int video_bitrate = 0;
    int audio_bitrate = 0;
    int width = 0;
    int height = 0;
    int fps = 0;
    
    std::string getFFmpegParams() const;
    bool isValid() const;
};

struct TranscodeRule {
    std::string app_pattern;
    std::string stream_pattern;
    std::vector<std::string> templates;
    bool auto_start = true;
    int priority = 0;
    
    bool matchStream(const std::string &app, const std::string &stream) const;
};

class TranscodeConfig {
public:
    static TranscodeConfig &Instance();
    
    bool loadConfig();
    void reloadConfig();
    
    // 基础配置
    bool isEnabled() const { return _enable; }
    int maxConcurrent() const { return _max_concurrent; }
    HWAccelType hwAccelType() const { return _hw_accel; }
    std::string tempDir() const { return _temp_dir; }
    int timeoutSec() const { return _timeout_sec; }
    std::string ffmpegBin() const { return _ffmpeg_bin; }
    
    // 按需转码配置
    bool onDemandEnabled() const { return _on_demand_enabled; }
    int stopDelaySeconds() const { return _stop_delay_seconds; }
    int checkIntervalSeconds() const { return _check_interval_seconds; }
    bool startOnlyWithPlayer() const { return _start_only_with_player; }
    
    // 模板管理
    std::shared_ptr<TranscodeTemplate> getTemplate(const std::string &name) const;
    std::vector<std::string> getAllTemplateNames() const;
    bool addTemplate(const TranscodeTemplate &tmpl);
    bool removeTemplate(const std::string &name);
    
    // 规则管理
    std::vector<std::string> getMatchedTemplates(const std::string &app, const std::string &stream) const;
    std::vector<TranscodeRule> getAllRules() const;
    bool addRule(const TranscodeRule &rule);
    bool removeRule(const std::string &app_pattern, const std::string &stream_pattern);
    
    // 配置验证
    bool validateConfig() const;
    std::string getConfigError() const;

private:
    TranscodeConfig() = default;
    ~TranscodeConfig() = default;
    
    void parseBasicConfig();
    void parseTemplates();
    void parseRules();
    bool parseTemplate(const std::string &name, const std::string &params, TranscodeTemplate &tmpl);
    
    std::string getHWAccelParams() const;
    
private:
    // 基础配置
    bool _enable = false;
    int _max_concurrent = 4;
    HWAccelType _hw_accel = HWAccelType::NONE;
    std::string _temp_dir = "./temp/transcode";
    int _timeout_sec = 300;
    std::string _ffmpeg_bin = "ffmpeg";
    
    // 按需转码配置
    bool _on_demand_enabled = false;
    int _stop_delay_seconds = 5;
    int _check_interval_seconds = 10;
    bool _start_only_with_player = false;
    
    // 模板配置
    std::map<std::string, std::shared_ptr<TranscodeTemplate>> _templates;
    
    // 规则配置
    std::vector<TranscodeRule> _rules;
    
    // 错误信息
    mutable std::string _last_error;
};

} // namespace mediakit

#endif // ZLMEDIAKIT_TRANSCODECONFIG_H