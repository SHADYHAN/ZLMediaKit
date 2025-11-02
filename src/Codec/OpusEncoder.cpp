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

///////////////////////////////// OpusEncoder /////////////////////////////////

OpusEncoder::OpusEncoder(int sample_rate, int channels, int bitrate)
    : _sample_rate(sample_rate), _channels(channels), _bitrate(bitrate) {
    _frame_pool.setSize(32);
    if (!init()) {
        throw std::runtime_error("Failed to initialize Opus encoder");
    }
}

OpusEncoder::~OpusEncoder() {
    stopThread(true);
    if (_fifo) {
        av_audio_fifo_free(_fifo);
        _fifo = nullptr;
    }
}

bool OpusEncoder::init() {
    // 查找 Opus 编码器
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
    if (!codec) {
        ErrorL << "Opus encoder not found, please compile FFmpeg with --enable-libopus";
        return false;
    }
    
    // 创建编码器上下文
    _context.reset(avcodec_alloc_context3(codec), [](AVCodecContext *ctx) {
        avcodec_free_context(&ctx);
    });
    
    if (!_context) {
        ErrorL << "Failed to allocate Opus encoder context";
        return false;
    }
    
    // 设置编码参数
    _context->sample_rate = _sample_rate;
    _context->bit_rate = _bitrate;
    _context->sample_fmt = AV_SAMPLE_FMT_FLT;  // Opus 要求浮点格式
    
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 0, 0)
    av_channel_layout_default(&_context->ch_layout, _channels);
#else
    _context->channels = _channels;
    _context->channel_layout = av_get_default_channel_layout(_channels);
#endif
    
    // 设置帧大小（Opus 支持 2.5, 5, 10, 20, 40, 60 ms）
    // 这里使用 20ms（最常用）
    _context->frame_size = _sample_rate * 20 / 1000;  // 20ms
    
    // 打开编码器
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "application", "audio", 0);  // audio, voip, lowdelay
    av_dict_set(&opts, "packet_loss", "1", 0);       // 支持丢包
    
    int ret = avcodec_open2(_context.get(), codec, &opts);
    av_dict_free(&opts);
    
    if (ret < 0) {
        ErrorL << "Failed to open Opus encoder: " << ffmpeg_err(ret);
        return false;
    }
    
    // 创建音频 FIFO 缓冲区（缓冲 4 个帧的大小）
    _fifo = av_audio_fifo_alloc(_context->sample_fmt, _channels, _context->frame_size * 4);
    if (!_fifo) {
        ErrorL << "Failed to allocate audio FIFO";
        return false;
    }
    
    // 预分配复用的编码帧
    _encode_frame = std::shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *frame) {
        av_frame_free(&frame);
    });
    if (!_encode_frame) {
        ErrorL << "Failed to allocate reusable encode frame";
        return false;
    }
    
    _encode_frame->nb_samples = _context->frame_size;
    _encode_frame->format = _context->sample_fmt;
    _encode_frame->sample_rate = _context->sample_rate;
    
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 0, 0)
    // Initialize before copy (av_frame_alloc should zero it, but be explicit for safety)
    memset(&_encode_frame->ch_layout, 0, sizeof(_encode_frame->ch_layout));
    int ret_ch = av_channel_layout_copy(&_encode_frame->ch_layout, &_context->ch_layout);
    if (ret_ch < 0) {
        ErrorL << "Failed to copy channel layout: " << ffmpeg_err(ret_ch);
        return false;
    }
#else
    _encode_frame->channel_layout = _context->channel_layout;
    _encode_frame->channels = _context->channels;
#endif
    
    // 预分配缓冲区
    int ret_buf = av_frame_get_buffer(_encode_frame.get(), 0);
    if (ret_buf < 0) {
        ErrorL << "Failed to allocate encode frame buffer: " << ffmpeg_err(ret_buf);
        return false;
    }
    
    InfoL << "Opus encoder initialized successfully"
          << ", sample_rate=" << _sample_rate
          << ", channels=" << _channels
          << ", bitrate=" << _bitrate
          << ", frame_size=" << _context->frame_size;
    
    return true;
}

bool OpusEncoder::inputFrame(const FFmpegFrame::Ptr &pcm_frame, bool async) {
    if (!_context || !pcm_frame) {
        return false;
    }
    
    if (async && !TaskManager::isEnabled()) {
        startThread("opus encoder");
    }
    
    if (!async || !TaskManager::isEnabled()) {
        return encodeFrame(pcm_frame);
    }
    
    // 异步编码
    auto frame_cache = pcm_frame;
    return addEncodeTask([this, frame_cache]() {
        encodeFrame(frame_cache);
    });
}

bool OpusEncoder::encodeFrame(const FFmpegFrame::Ptr &pcm_frame) {
    TimeTicker2(50, TraceL);  // 提高阈值，减少日志
    
    auto frame = pcm_frame->get();
    if (!frame || !_fifo || !_encode_frame) {
        return false;
    }
    
    // FIFO 溢出保护：如果积压过多，丢弃一些旧样本
    int fifo_size = av_audio_fifo_size(_fifo);
    if (fifo_size > _context->frame_size * 10) {
        int drain_samples = _context->frame_size * 2;
        WarnL << "FIFO overflow detected (" << fifo_size << " samples), dropping " 
              << drain_samples << " old samples";
        av_audio_fifo_drain(_fifo, drain_samples);
    }
    
    // 将输入帧写入 FIFO
    int ret = av_audio_fifo_write(_fifo, (void **)frame->data, frame->nb_samples);
    if (ret < frame->nb_samples) {
        WarnL << "Failed to write samples to FIFO: " << ret << "/" << frame->nb_samples;
        return false;
    }
    
    // 当 FIFO 中有足够的样本时，进行编码
    while (av_audio_fifo_size(_fifo) >= _context->frame_size) {
        // 确保帧可写入（复用帧时需要）
        av_frame_make_writable(_encode_frame.get());
        
        // 从 FIFO 读取样本到复用的编码帧
        ret = av_audio_fifo_read(_fifo, (void **)_encode_frame->data, _context->frame_size);
        if (ret < _context->frame_size) {
            WarnL << "Failed to read samples from FIFO: " << ret << "/" << _context->frame_size;
            return false;
        }
        
        // 设置 PTS：使用累计样本数（FFmpeg 内部使用样本数）
        _encode_frame->pts = _total_samples;
        
        // ⚠️ 关键：先更新计数，再发送到编码器
        _total_samples += _context->frame_size;
        
        // 发送帧到编码器
        ret = avcodec_send_frame(_context.get(), _encode_frame.get());
        if (ret < 0) {
            if (ret != AVERROR_INVALIDDATA) {
                WarnL << "avcodec_send_frame failed: " << ffmpeg_err(ret);
            }
            continue;
        }
        
        // 接收编码后的数据包
        while (true) {
            auto pkt = alloc_av_packet();
            ret = avcodec_receive_packet(_context.get(), pkt.get());
            
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            
            if (ret < 0) {
                WarnL << "avcodec_receive_packet failed: " << ffmpeg_err(ret);
                break;
            }
            
            // 创建输出帧
            auto opus_frame = _frame_pool.obtain();
            opus_frame->_codec_id = CodecOpus;
            opus_frame->_buffer.assign((char *)pkt->data, pkt->size);
            
            // ⚠️ 关键：将 FFmpeg 的 PTS（样本数）转换为毫秒
            // PTS(ms) = 样本数 * 1000 / 采样率
            // 例如：960 * 1000 / 48000 = 20ms
            if (pkt->pts != AV_NOPTS_VALUE) {
                opus_frame->_dts = pkt->pts * 1000 / _sample_rate;
            } else {
                // 如果编码器没有生成 PTS，手动计算
                opus_frame->_dts = (_total_samples - _context->frame_size) * 1000 / _sample_rate;
            }
            opus_frame->_pts = opus_frame->_dts;
            
            onEncoded(opus_frame);
        }
    }
    
    return true;
}

void OpusEncoder::onEncoded(const Frame::Ptr &opus_frame) {
    if (_on_output) {
        try {
            _on_output(opus_frame);
        } catch (std::exception &ex) {
            WarnL << "Exception in OpusEncoder callback: " << ex.what();
        } catch (...) {
            WarnL << "Unknown exception in OpusEncoder callback";
        }
    }
}

void OpusEncoder::setOnOutput(onOutput cb) {
    _on_output = std::move(cb);
}

const AVCodecContext *OpusEncoder::getContext() const {
    return _context.get();
}

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
    try {
        _encoder = std::make_shared<OpusEncoder>(target_sample_rate, target_channels, target_bitrate);
    } catch (std::exception &ex) {
        throw std::runtime_error(string("Failed to create Opus encoder: ") + ex.what());
    }
    
    // 创建输出 Track
    _output_track = Factory::getTrackByCodecId(CodecOpus, target_sample_rate, target_channels, 0);
    if (!_output_track) {
        throw std::runtime_error("Failed to create Opus track");
    }
    
    // 设置解码回调（直接捕获 this，生命周期由外部保证）
    _decoder->setOnDecode([this](const FFmpegFrame::Ptr &frame) {
        onDecoded(frame);
    });
    
    // 设置编码回调
    _encoder->setOnOutput([this](const Frame::Ptr &frame) {
        onEncoded(frame);
    });
    
    InfoL << "AudioTranscoder created successfully";
}

AudioTranscoder::~AudioTranscoder() {
    // Clear callbacks first to prevent any further calls during destruction
    _on_output = nullptr;
    
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
    
    // 输入帧到解码器（异步解码）
    return _decoder->inputFrame(frame, true, true);
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
        _encoder->inputFrame(resampled, true);
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

