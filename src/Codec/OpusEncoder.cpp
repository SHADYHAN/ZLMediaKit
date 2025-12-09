/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#if defined(ENABLE_FFMPEG)

#include "OpusEncoder.h"
#include "Extension/Factory.h"
#include "Util/logger.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

///////////////////////////////// AudioTranscoder /////////////////////////////////

AudioTranscoder::AudioTranscoder(const Track::Ptr &track,
                                int target_sample_rate,
                                int target_channels,
                                int target_bitrate)
    : _target_sample_rate(target_sample_rate)
    , _target_channels(target_channels)
    , _target_bitrate(target_bitrate) {
    
    auto codec_id = track->getCodecId();
    InfoL << "Creating AudioTranscoder: " 
          << getCodecName(codec_id) << " → Opus"
          << ", " << target_sample_rate << "Hz"
          << ", " << target_channels << "ch"
          << ", " << target_bitrate << "bps";
    
    // 创建解码器
    try {
        _decoder = std::make_shared<FFmpegDecoder>(track);
    } catch (std::exception &ex) {
        throw std::runtime_error(string("Failed to create decoder: ") + ex.what());
    }
    
    // 创建重采样器（转换为目标采样率和格式）
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 0, 0)
    // Initialize AVChannelLayout properly for FFmpeg 8+ compatibility
    AVChannelLayout ch_layout = {};  // Zero-initialize first
    if (target_channels == 1) {
        ch_layout = AV_CHANNEL_LAYOUT_MONO;
    } else {
        ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    }
    _resampler = std::make_shared<FFmpegSwr>(AV_SAMPLE_FMT_FLT, &ch_layout, target_sample_rate);
#else
    int channel_layout = target_channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    _resampler = std::make_shared<FFmpegSwr>(AV_SAMPLE_FMT_FLT, target_channels, channel_layout, target_sample_rate);
#endif
    
    // 创建 Opus 编码器
    _output_track = Factory::getTrackByCodecId(CodecOpus, target_sample_rate, target_channels, 0);
    if (!_output_track) {
        throw std::runtime_error("Failed to create Opus track");
    }
    _output_track->setBitRate(_target_bitrate);

    // 创建 FFmpeg-based Opus 编码器
    try {
        _encoder = std::make_shared<FFmpegEncoder>(_output_track);
    } catch (std::exception &ex) {
        throw std::runtime_error(string("Failed to create Opus encoder: ") + ex.what());
    }
    
    // 设置解码回调
    // 注意：这里使用裸 this 指针，生命周期由析构函数中的 stopThread 保证
    // 析构时会先停止线程，再析构成员，确保回调不会访问已销毁的对象
    _decoder->setOnDecode([this](const FFmpegFrame::Ptr &frame) {
        onDecoded(frame);
    });
    
    // 设置编码回调
    _encoder->setOnEncode([this](const Frame::Ptr &frame) {
        onEncoded(frame);
    });
    
    InfoL << "AudioTranscoder created successfully";
}

AudioTranscoder::~AudioTranscoder() {
    // Clear callbacks first to prevent any further calls during destruction
    _on_output = nullptr;
    if (_encoder) {
        _encoder->setOnEncode(nullptr);
    }
    if (_decoder) {
        _decoder->setOnDecode(nullptr);
    }
    
    // Stop encoder thread before decoder to prevent callback issues
    if (_encoder) {
        _encoder->stopThread(true);
    }
    
    // Stop decoder thread
    if (_decoder) {
        _decoder->stopThread(true);
    }
    
    if (_input_frame_count > 0) {
        InfoL << "AudioTranscoder destroyed"
              << ", in=" << _input_frame_count
              << ", out=" << _output_frame_count;
    }
}

bool AudioTranscoder::inputFrame(const Frame::Ptr &frame) {
    if (!_decoder || !frame) {
        return false;
    }
    
    _input_frame_count++;
    
    // 输入帧到解码器（同步解码，避免在音频路径上额外排队累积时延）
    // old-transcode 中 RtcMediaSourceMuxer 使用 FFmpegDecoder::inputFrame(frame, true, false)
    // 这里也采用 async=false 的方式，使音频转码行为更加一致
    return _decoder->inputFrame(frame, true, false);
}

void AudioTranscoder::onDecoded(const FFmpegFrame::Ptr &pcm_frame) {
    if (!pcm_frame || !_resampler) {
        return;
    }
    
    // 重采样到目标格式
    auto resampled = _resampler->inputFrame(pcm_frame);
    if (!resampled) {
        return;
    }
    
    // 编码为 Opus（异步编码）
    if (_encoder) {
        // old-transcode 在 WebRTC 音频转码路径中使用同步编码（async=false），
        // 这里同样关闭音频编码侧的异步队列，减少 FIFO 深度和大幅时间戳漂移的可能。
        _encoder->inputFrame(resampled, false);
    }
}

void AudioTranscoder::onEncoded(const Frame::Ptr &opus_frame) {
    if (!opus_frame) {
        return;
    }
    
    _output_frame_count++;
    
    try {
        // 更新输出 Track
        if (_output_track) {
            _output_track->inputFrame(opus_frame);
        }
        
        // 回调给上层
        if (_on_output) {
            _on_output(opus_frame);
        }
    } catch (std::exception &ex) {
        WarnL << "Exception in AudioTranscoder::onEncoded: " << ex.what();
    } catch (...) {
        WarnL << "Unknown exception in AudioTranscoder::onEncoded";
    }
}

void AudioTranscoder::setOnOutput(onOutput cb) {
    _on_output = std::move(cb);
}

Track::Ptr AudioTranscoder::getOutputTrack() const {
    return _output_track;
}

void AudioTranscoder::flush() {
    if (_decoder) {
        _decoder->flush();
    }
}

} // namespace mediakit

#endif // ENABLE_FFMPEG

