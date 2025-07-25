#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libswscale/swscale.h>
}
#include <memory>
#include <string>
#include <vector>
#include <videoreader/videoreader.hpp>

struct AVDictionaryDeleter {
  void operator()(AVDictionary* av_dictionary) const noexcept {
    av_dict_free(&av_dictionary);
  }
};
using AVDictionaryUP = std::unique_ptr<AVDictionary, AVDictionaryDeleter>;

AVDictionaryUP
_create_dict_from_params_vec(std::vector<std::string> const& parameter_pairs);

std::string get_av_error(int const errnum);

struct AVFrameDeleter {
  void operator()(AVFrame* f) const noexcept {
    av_frame_free(&f);
  }
};
using AVFrameUP = std::unique_ptr<AVFrame, AVFrameDeleter>;

struct AVPacketDeleter {
  void operator()(AVPacket* p) const noexcept {
    av_packet_free(&p);
  }
};
using AVPacketUP = std::unique_ptr<AVPacket, AVPacketDeleter>;

struct AVCodecContextDeleter {
  void operator()(AVCodecContext* ctx) const noexcept {
#if LIBAVCODEC_VERSION_INT <= AV_VERSION_INT(58, 134, 100)  // ubuntu-22.04
    avcodec_close(ctx);
#endif
    avcodec_free_context(&ctx);
  }
};
using AVCodecContextUP = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;

struct SwsContextDeleter {
  void operator()(SwsContext* sws_context) const noexcept {
    sws_freeContext(sws_context);
  }
};
using SwsContextUP = std::unique_ptr<SwsContext, SwsContextDeleter>;

struct AVFormatContextDeleter {
  void operator()(AVFormatContext* c) const noexcept {
    avformat_close_input(&c);
    avformat_free_context(c);
  }
};
using AVFormatContextUP =
    std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;

void videoreader_ffmpeg_callback(
    void* avcl, int level, const char* fmt, va_list vl);

struct FFmpegLogInfo {
  VideoReader::LogCallback log_callback;
  void* userdata;
  int print_prefix;

  FFmpegLogInfo(
      VideoReader::LogCallback log_callback, void* userdata, int print_prefix) :
      log_callback{log_callback},
      userdata{userdata},
      print_prefix{print_prefix} {
  }
};
