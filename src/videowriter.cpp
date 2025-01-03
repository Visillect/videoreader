#include <videoreader/videowriter.h>
#ifdef VIDEOWRITER_WITH_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#include "ffmpeg_common.hpp"
#include <charconv>  // std::from_chars
#include <condition_variable>
#include <deque>
#include <optional>  // std::optional
#include <thread>
#endif
#include <stdexcept>  // std::runtime_error

#ifdef VIDEOWRITER_WITH_FFMPEG

static std::string format_error(int const errnum, char const* const message) {
  return std::string(message) + " (" + get_av_error(errnum) + ")";
}

static void log_packet(const AVFormatContext* fmt_ctx, const AVPacket* pkt) {
  AVRational* time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
  printf(
      "pts:%ld pts_time:%.6g dts:%ld dts_time:%.6g duration:%ld "
      "duration_time:%.6g stream_index:%d\n",
      pkt->pts,
      av_q2d(*time_base) * pkt->pts,
      pkt->dts,
      av_q2d(*time_base) * pkt->dts,
      pkt->duration,
      av_q2d(*time_base) * pkt->duration,
      pkt->stream_index);
}

struct VideoWriter::Impl {

  AVPacketUP pkt;
  AVCodecContextUP enc;
  AVStream* st;  // freed by context

  int64_t next_pts = 0;

  AVFrameUP frame;

  SwsContextUP sws_ctx;
  AVFormatContextUP oc;
  VideoReader::VRImage m_frameTemplate;

  const bool realtime;
  std::thread write_thread;
  std::deque<AVFrameUP> write_queue;
  std::condition_variable cv;
  std::mutex m;
  std::exception_ptr exception;

  Impl(bool realtime) : pkt(av_packet_alloc()), realtime{realtime} {
  }

  void send_frame(AVFrame* frame) {
    if (const int ret = avcodec_send_frame(this->enc.get(), frame); ret < 0) {
      throw std::runtime_error(
          format_error(ret, "avcodec_send_frame() failed"));
    }
    for (;;) {
      const int receive_packet_ret =
          avcodec_receive_packet(this->enc.get(), this->pkt.get());
      if (receive_packet_ret == AVERROR(EAGAIN))  // No more packets for now.
        break;
      if (receive_packet_ret == AVERROR_EOF)  // No more packets, ever.
        break;
      if (receive_packet_ret < 0) {
        throw std::runtime_error(format_error(
            receive_packet_ret, "avcodec_receive_packet() failed"));
      }

      av_packet_rescale_ts(
          this->pkt.get(), this->enc->time_base, this->st->time_base);
      this->pkt->stream_index = this->st->index;
      // log_packet(this->oc.get(), this->pkt.get());
      // if (int const ret = av_write_frame(this->oc.get(), this->pkt.get()); ret < 0) {
      //   throw std::runtime_error(format_error(ret, "av_write_frame() failed"));
      // }
      if (int const ret =
              av_interleaved_write_frame(this->oc.get(), this->pkt.get());
          ret < 0) {
        throw std::runtime_error(
            format_error(ret, "av_interleaved_write_frame() failed"));
      }
    }
    if (!frame) {  // close
      if (this->oc) {
        if (int const ret = av_write_trailer(this->oc.get()); ret != 0) {
          throw std::runtime_error(
              format_error(ret, "av_write_trailer() failed"));
        }
      }
      if (this->oc && !(this->oc->oformat->flags & AVFMT_NOFILE)) {
        if (int const ret = avio_closep(&this->oc->pb); ret != 0) {
          throw std::runtime_error(format_error(ret, "avio_closep() failed"));
        }
      }
    }
  }

  void write() {
    AVFrameUP popped_frame{};
    try {
      for (;;) {
        {
          std::unique_lock lk(m);
          cv.wait(lk, [&] {
            return !this->write_queue.empty();
          });
          popped_frame = std::move(this->write_queue.front());
          this->write_queue.pop_front();
        }
        this->send_frame(popped_frame.get());
        if (!popped_frame) {
          break;
        }
      }
    } catch (...) {
      this->exception = std::current_exception();
    }
  }

  bool push(VideoReader::Frame const& frame) {
    VideoReader::VRImage const& img = frame.image;
    if (this->frame->width != img.width || this->frame->height != img.height) {
      throw std::runtime_error("can't change video frame size");
    }
    if (const int ret = sws_scale(
            this->sws_ctx.get(),
            &img.data,
            &img.stride,
            0,
            img.height,
            this->frame->data,
            this->frame->linesize);
        ret < 0) {
      throw std::runtime_error(format_error(ret, "sws_scale() failed"));
    }
    if (this->exception) {
      std::rethrow_exception(this->exception);
    }
    this->frame->pts = std::llround(frame.timestamp_s * 65535.0);
    if (this->realtime) {
      AVFrameUP dynframe{av_frame_alloc()};
      // `av_frame_ref` is so bad. We make a copy. It could be 300% better.
      if (const int ret = av_frame_ref(dynframe.get(), this->frame.get());
          ret < 0) {
        throw std::runtime_error(format_error(ret, "av_frame_ref() failed"));
      }
      std::unique_lock lk(this->m);
      if (this->write_queue.size() > 9) {
        return false;  //
      }
      this->write_queue.push_back(std::move(dynframe));
      this->cv.notify_one();
    } else {
      this->send_frame(this->frame.get());
    }
    return true;
  }

  void close() {
    if (this->realtime) {
      {
        std::unique_lock lk(this->m);
        this->write_queue.push_back(nullptr);
        this->cv.notify_one();
      }
      if (this->write_thread.joinable()) {
        this->write_thread.join();
      }
      if (this->exception) {
        std::rethrow_exception(this->exception);
      }
    } else {
      this->send_frame(nullptr);
    }
  }
};

static int64_t pop_value_int64(
    AVDictionary* dict, const char* key, int64_t const default_value) {
  AVDictionaryEntry const* entry = av_dict_get(dict, key, NULL, 0);
  if (entry) {
    av_dict_set(&dict, key, NULL, 0);  // remove item
    std::string const str{entry->value};
    int64_t result{};
    auto [ptr, ec] =
        std::from_chars(str.data(), str.data() + str.size(), result);
    if (ec == std::errc()) {
      return result;
    }
    throw std::runtime_error("`" + str + "` is not a valid int64");
  }
  return default_value;
}

VideoWriter::VideoWriter(
    std::string const& uri,
    VideoReader::VRImage const& format,
    std::vector<std::string> const& parameter_pairs,  // size % 2 == 0
    bool realtime,
    VideoReader::LogCallback log_callback,
    void* userdata) :
    impl{new Impl(realtime)} {
  this->impl->sws_ctx.reset(sws_getContext(
      format.width,
      format.height,
      AV_PIX_FMT_RGB24 /* from */,
      format.width,
      format.height,
      AV_PIX_FMT_YUV420P /* to */,
      SWS_BICUBIC,
      NULL,
      NULL,
      NULL));
  if (!this->impl->sws_ctx) {
    throw std::runtime_error("sws_getContext() failed");
  }

  char format_name[] = "matroska";
  char encoder_name[] = "libx264";  // libxvid
  // find codec
  AVFormatContext* oc_ = nullptr;
  {
    if (const int ret = avformat_alloc_output_context2(
            &oc_, NULL, format_name, uri.c_str());
        ret < 0) {
      throw std::runtime_error(
          format_error(ret, "avformat_alloc_output_context2 error"));
    }
    this->impl->oc.reset(oc_);
  }

  const AVCodec* codec = avcodec_find_encoder_by_name(encoder_name);
  // oc_->oformat->video_codec = codec;
  // const AVCodec *codec = avcodec_find_encoder(oc_->oformat->video_codec);
  if (!codec) {
    throw std::runtime_error("avcodec_find_encoder_by_name() failed");
  }
  if (log_callback != nullptr) {
    char const* profile_name{};
    if (codec->profiles) {
      profile_name = codec->profiles->name;
    } else {
      profile_name = "has no profiles";
    }
    std::string const message =
        "using profile `" + std::string(profile_name) + "`";
    log_callback(message.c_str(), VideoReader::LogLevel::INFO, userdata);
    log_callback(codec->long_name, VideoReader::LogLevel::INFO, userdata);
  }

  this->impl->st = avformat_new_stream(oc_, codec);
  if (!this->impl->st) {
    throw std::runtime_error("avformat_new_stream() failed");
  }
  this->impl->st->id = 0;

  AVCodecContext* c = avcodec_alloc_context3(codec);
  if (!c) {
    throw std::runtime_error("avcodec_alloc_context3() failed");
  }
  this->impl->enc.reset(c);

  auto options = _create_dict_from_params_vec(parameter_pairs);

  // c->codec_id = oc_->oformat->video_codec;
  c->codec_id = codec->id;
  c->bit_rate =
      pop_value_int64(options.get(), "br", 4000000);  // bits per second
  c->width = format.width;
  c->height = format.height;
  this->impl->st->time_base =
      AVRational{1, 65535}; /* 65535 - is MPEG 4 limit */
  c->time_base = this->impl->st->time_base;
  c->framerate =
      AVRational{0, 1};  // AVRational{c->time_base.den, c->time_base.num};
  //c->rc_buffer_size = 8339456; // 1 MiB
  c->gop_size = 12;  // emit one intra frame every twelve frames at most
  c->pix_fmt = AV_PIX_FMT_YUV420P;
  if (oc_->oformat->flags & AVFMT_GLOBALHEADER) {
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  av_opt_set(c->priv_data, "quality", "7", 0);
  av_opt_set(c->priv_data, "qp", "18", 0);

  AVDictionary* options_ptr = options.release();

  if (const int ret = avcodec_open2(c, codec, &options_ptr); ret < 0) {
    throw std::runtime_error(format_error(ret, "avcodec_open2() failed"));
  }
  options.reset(options_ptr);
  int const count = av_dict_count(options_ptr);
  if (count) {
    const AVDictionaryEntry* e = NULL;
    while ((e = av_dict_get(options_ptr, "", e, AV_DICT_IGNORE_SUFFIX))) {
      log_callback(
          (std::string("invalid key `") + e->key + "`").c_str(),
          VideoReader::LogLevel::ERROR,
          userdata);
    }
    throw std::runtime_error("invalid arguments. see logs for mare info.");
  }

  /* Allocate frame */
  {
    this->impl->frame.reset(av_frame_alloc());
    if (!this->impl->frame) {
      throw std::runtime_error("av_frame_alloc() failed");
    }

    this->impl->frame->format = c->pix_fmt;
    this->impl->frame->width = c->width;
    this->impl->frame->height = c->height;

    if (const int ret = av_frame_get_buffer(this->impl->frame.get(), 32);
        ret < 0) {
      throw std::runtime_error(
          format_error(ret, "av_frame_get_buffer() failed"));
    }
  }
  if (!this->impl->st->codecpar) {
    throw std::runtime_error("codecpar is empty");
  }
  if (avcodec_parameters_from_context(this->impl->st->codecpar, c) < 0) {
    throw std::runtime_error("avcodec_parameters_from_context() failed");
  }

  /* open the output file, if needed */
  if (!(oc_->oformat->flags & AVFMT_NOFILE)) {
    if (int const ret = avio_open(&oc_->pb, uri.c_str(), AVIO_FLAG_WRITE);
        ret < 0) {
      throw std::runtime_error(format_error(ret, "avio_open() failed"));
    }
  }
  /* Write the stream header, if any. */
  if (int const ret = avformat_write_header(oc_, NULL); ret < 0) {
    throw std::runtime_error(
        format_error(ret, "avformat_write_header() failed"));
  }
  if (realtime) {
    this->impl->write_thread =
        std::thread(&VideoWriter::Impl::write, this->impl.get());
  }
}

bool VideoWriter::push(VideoReader::Frame const& frame) {
  if (!this->impl) {
    throw std::runtime_error("video was closed");
  }
  return this->impl->push(frame);
}

void VideoWriter::close() {
  if (!this->impl) {
    throw std::runtime_error("already closed");
  }
  this->impl->close();
  this->impl.reset(nullptr);
}

VideoWriter::~VideoWriter() {
  if (this->impl) {
    this->impl->close();
    this->impl.reset(nullptr);
  }
};
#else

struct VideoWriter::Impl {};

VideoWriter::VideoWriter(
    std::string const& uri,
    VideoReader::VRImage const& format,
    std::vector<std::string> const& parameter_pairs,  // size % 2 == 0
    bool realtime,
    VideoReader::LogCallback log_callback,
    void* userdata) {
  throw std::runtime_error("no backend compiled for videowriter");
}
bool VideoWriter::push(VideoReader::Frame const&) {
  return false;
}
void VideoWriter::close() {
}

VideoWriter::~VideoWriter() = default;

#endif
