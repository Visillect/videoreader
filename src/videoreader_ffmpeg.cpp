#include "videoreader_ffmpeg.hpp"

//
// Resources:
//   * https://blogs.gentoo.org/lu_zero/2016/03/29/new-avcodec-api/
//
// Pitfalls (debugging requires a video for reproduction):
//   * Calls `avcodec_receive_frame` only once, unlike docs are suggesting
//   * Doesn't support dynamic resolution change

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avio.h>  // needed?
#include <libavutil/avutil.h>
}
#include "ffmpeg_common.hpp"
#include "spinlock.hpp"
#include "thismsgpack.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include <stdexcept>  // std::runtime_error
#include <thread>
#include <vector>

static AVFormatContextUP _get_format_context(
    std::string const& filename, AVDictionaryUP& options, void* opaque) {
  AVInputFormat const* input_format = nullptr;
  std::string path_to_use = filename;
  std::size_t const protocol_idx = filename.find("://");
  if (protocol_idx != std::string::npos) {
    std::string const protocol = filename.substr(0, protocol_idx);
    input_format = av_find_input_format(protocol.c_str());
    if (input_format != nullptr) {
      path_to_use = filename.substr(protocol_idx + 3);
    }
  }
  AVFormatContext* format_context = avformat_alloc_context();
  if (format_context == nullptr) {
    throw std::runtime_error("Failed to allocate AVFormatContext");
  }
  format_context->opaque = opaque;

  AVDictionary* opts = options.release();

  int const ret = avformat_open_input(
      &format_context,
      path_to_use.c_str(),
#ifdef FF_API_AVIOFORMAT  // silly cast for ffmpeg4
      (AVInputFormat*)
#endif
          input_format,
      &opts);

  options.reset(opts);
  if (ret < 0) {
    std::string error = get_av_error(ret);
    if (ret == AVERROR_PROTOCOL_NOT_FOUND) {
      const AVInputFormat* input_fmt = NULL;
      const AVOutputFormat* output_fmt = NULL;
      void* opaque = NULL;

      error += " (available:"
#ifdef VIDEOREADER_WITH_PYLON
               " pylon://"
#endif
#ifdef VIDEOREADER_WITH_GALAXY
               " galaxy://"
#endif
#ifdef VIDEOREADER_WITH_IDATUM
               " idatum://"
#endif
          ;
      while ((input_fmt = av_demuxer_iterate(&opaque))) {
        if (input_fmt->priv_class && input_fmt->priv_class->category ==
                                         AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT) {
          std::string const name = std::string(input_fmt->name);
          std::size_t const comma_idx = name.find(",");
          if (comma_idx == std::string::npos) {
            error += " " + name + "://";
          } else {
            error += " " + name.substr(0, comma_idx) + ":// " +
                     name.substr(comma_idx + 1) + "://";
          }
        }
      }
      error += ")";
    }
    throw std::runtime_error("Can't open `" + filename + "`, " + error);
  }
  return AVFormatContextUP(format_context);
}

/*
This is a copy from libavformat/internal.h. Helps with logging big time.
*/
struct DirtyHackFFStream {
// I read the ffmpeg sources - there's no better way.
// we just need to update the structure below as ffmpeg updates

// 7.0 - 61.1.100
// 6.0 - 60.3.100
// 5.0 - 59.16.100
// 4.0 - 58.12.100
#if LIBAVFORMAT_VERSION_INT <= AV_VERSION_INT(61, 0, 0)
  int reorder;
  struct AVBSFContext* bsfc;
  int bitstream_checked;
  AVCodecContext* avctx;
#else
  AVFormatContext* fmtctx;
  int reorder;
  struct AVBSFContext* bsfc;
  int bitstream_checked;
  struct AVCodecContext* avctx;
#endif
};

static AVStream* _get_video_stream(AVFormatContext* format_context) {
  for (unsigned stream_idx = 0; stream_idx < format_context->nb_streams;
       ++stream_idx) {
    AVStream* stream = format_context->streams[stream_idx];
#ifdef FF_API_AVIOFORMAT
    reinterpret_cast<DirtyHackFFStream*>(stream->internal)
#else
    reinterpret_cast<DirtyHackFFStream*>(stream + 1)
#endif
        ->avctx->opaque = format_context->opaque;
  }
  if (avformat_find_stream_info(format_context, NULL) != 0) {
    throw std::runtime_error("avformat_find_stream_info failed");
  }
  for (unsigned stream_idx = 0; stream_idx < format_context->nb_streams;
       ++stream_idx) {
    AVStream* av_stream = format_context->streams[stream_idx];
    if (av_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      return av_stream;
    }
  }
  throw std::runtime_error("video steam not found");
}

static AVCodecContextUP _get_codec_context(
    AVCodecParameters const* av_codecpar,
    AVDictionaryUP& options,
    void* opaque) {
  AVCodec const* av_codec = avcodec_find_decoder(av_codecpar->codec_id);
  if (!av_codec) {
    throw std::runtime_error("Unsupported codec");
  }
  auto codec_context = AVCodecContextUP(avcodec_alloc_context3(av_codec));
  codec_context->opaque = opaque;

  if (avcodec_parameters_to_context(codec_context.get(), av_codecpar) != 0) {
    throw std::runtime_error("avcodec_parameters_to_context failed");
  }

  AVDictionary* opts = options.release();
  if (avcodec_open2(codec_context.get(), av_codec, &opts) != 0) {
    throw std::runtime_error("avcodec_open2 failed");
  }
  options.reset(opts);

  return codec_context;
}

static SwsContextUP _create_converter(
    AVPixelFormat const pix_format, int const width, int const height) {
  // hacks to avoid deprecated warning.
  // must change `codec_context->color_range` somewhere
  AVPixelFormat new_pix_format;
  switch (pix_format) {
  case AV_PIX_FMT_YUVJ420P:
    new_pix_format = AV_PIX_FMT_YUV420P;
    break;
  case AV_PIX_FMT_YUVJ422P:
    new_pix_format = AV_PIX_FMT_YUV422P;
    break;
  case AV_PIX_FMT_YUVJ444P:
    new_pix_format = AV_PIX_FMT_YUV444P;
    break;
  case AV_PIX_FMT_YUVJ440P:
    new_pix_format = AV_PIX_FMT_YUV440P;
    break;
  default:
    new_pix_format = pix_format;
  }

  SwsContextUP converter{sws_getContext(
      width,
      height,
      new_pix_format,
      width,
      height,
      AV_PIX_FMT_RGB24,
      SWS_BICUBIC,
      nullptr,
      nullptr,
      nullptr)};
  if (!converter) {
    throw std::runtime_error("Converter initialization failed");
  }
  return converter;
}

struct AVFramePusher {
  enum class Type { INT64_T, INT } _type;
  void* AVFrame::*ref;

  AVFramePusher(int64_t AVFrame::*ref) :
      _type{Type::INT64_T},
      ref{reinterpret_cast<void * AVFrame::*>(ref)} {
  }
  AVFramePusher(int AVFrame::*ref) :
      _type{Type::INT},
      ref{reinterpret_cast<void * AVFrame::*>(ref)} {
  }

  void operator()(AVFrame const* frame, MallocStream& out) const {
    switch (this->_type) {
    case Type::INT64_T: {
      auto const value =
          frame->*(reinterpret_cast<int64_t AVFrame::*>(this->ref));
      thismsgpack::pack(value, out);
      break;
    }
    case Type::INT: {
      auto const value = frame->*(reinterpret_cast<int AVFrame::*>(this->ref));
      thismsgpack::pack(static_cast<int64_t>(value), out);
      break;
    }
    }
  }
};

// template<typename T>
// auto create_pusher(T AVFrame::*ref) {
//   return [ref](AVFrame const* frame, MallocStream &out) {
//     auto const value = frame->*(this->ref);
//     thismsgpack::pack(value, out);
//   }
// }

struct VideoReaderFFmpeg::Impl {
  decltype(VideoReader::Frame::number) current_frame = 0;
  std::atomic<bool> stop_requested;
  AVFormatContextUP format_context;
  AVStream* av_stream;
  AVCodecContextUP codec_context;
  AVFrameUP av_frame;
  SwsContextUP sws_context;

  std::thread read_thread;  // for network to work
  std::deque<AVPacket*> read_queue;  // read buffer
  SpinLock read_queue_lock;
  AVPacket* pop_packet();

  int print_prefix;
  std::vector<AVFramePusher> pushers;
  AllocateCallback allocate_callback;
  DeallocateCallback deallocate_callback;
  VideoReader::LogCallback log_callback;
  void* userdata;

  Impl(
      std::string const& url,
      std::vector<std::string> const& parameter_pairs,
      std::vector<std::string> const& extras,
      AllocateCallback allocate_callback,
      DeallocateCallback deallocate_callback,
      VideoReader::LogCallback log_callback,
      void* userdata) :
      stop_requested(false),
      allocate_callback{allocate_callback},
      deallocate_callback{deallocate_callback},
      userdata{userdata},
      log_callback{log_callback},
      print_prefix{1} {
    for (auto const& extra : extras) {
      if (extra == "pkt_pos") {
#if LIBAVUTIL_VERSION_INT <= AV_VERSION_INT(56, 70, 100)
        this->pushers.emplace_back(&AVFrame::pkt_pos);
#else
        throw std::runtime_error(
            "pkt_pos is deprecated in new versions of FFmpeg");
#endif
      } else if (extra == "quality") {
        this->pushers.emplace_back(&AVFrame::quality);
      } else if (extra == "pts") {
        this->pushers.emplace_back(&AVFrame::pts);
      } else if (extra == "pkt_dts") {
        this->pushers.emplace_back(&AVFrame::pkt_dts);
      } else {
        throw std::runtime_error(
            "unknown extra: `" + extra +
            "`. Possible extras are: "
            "'pkt_pos', 'quality', 'pts', 'pkt_dts'");
      }
    }
    AVDictionaryUP options = _create_dict_from_params_vec(parameter_pairs);
    this->format_context = _get_format_context(url, options, this);
    this->av_stream = _get_video_stream(format_context.get());
    this->codec_context =
        _get_codec_context(av_stream->codecpar, options, this);
    this->av_frame = AVFrameUP(av_frame_alloc());
    if (this->codec_context->pix_fmt != AV_PIX_FMT_NONE) {
      this->sws_context = _create_converter(
          this->codec_context->pix_fmt,
          this->codec_context->width,
          this->codec_context->height);
    }

    if (options) {
      char* buf = NULL;
      if (av_dict_get_string(options.get(), &buf, '=', ',') < 0) {
        throw std::runtime_error("error formatting parameters dictionary");
      }
      std::string options{buf};
      av_freep(&buf);
      throw std::runtime_error("unknown options: " + options);
    }

    this->read_thread = std::thread(&VideoReaderFFmpeg::Impl::read, this);
  }
  bool is_seekable() const {
    AVIOContext const* io_centext = this->format_context->pb;
    return io_centext && io_centext->seekable != 0;
  }

  void read() {
    if (this->is_seekable()) {
      // seeking to timestemp 0.0 helps prevent compression
      // artifacts on broken videos. `av_seek_frame` is known to hang
      // on streamed videos, so check `is_seekable` first
      av_seek_frame(this->format_context.get(), -1, 0, AVSEEK_FLAG_ANY);
    }
    while (!this->stop_requested) {
      AVPacketUP thread_packet(av_packet_alloc());
      int const read_ret =
          av_read_frame(this->format_context.get(), thread_packet.get());
      if (read_ret < 0) {  // error occured
        {  // unrecoverable error. exit the thread
          std::lock_guard<SpinLock> guard(this->read_queue_lock);
          this->read_queue.push_back(nullptr);
        }
        // this->queue_updated.notify_one();
        return;
      }
      if (thread_packet->stream_index == this->av_stream->index) {
        this->read_queue_lock.lock();
        if (this->read_queue.size() > 100) {
          if (this->is_seekable()) {  // offline - wait for data
            this->read_queue_lock.unlock();
            while (!this->stop_requested) {
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
              {
                std::lock_guard<SpinLock> guard(this->read_queue_lock);
                if (this->read_queue.size() < 80) {
                  break;
                }
              }
            }
            this->read_queue_lock.lock();  // lock here for easier unlock logic
          } else {  // realtime - clear buffer
            for (int i = 0; i < 90; ++i) {
              this->read_queue.pop_front();
            }
          }
        }
        this->read_queue_lock.unlock();
        {
          std::lock_guard<SpinLock> guard(this->read_queue_lock);
          this->read_queue.push_back(thread_packet.release());
        }
        //this->queue_updated.notify_one();
      }
    }
  }

  VideoReader::FrameUP next_frame(bool decode) {
    while (true) {
      AVPacket* raw_packet = this->pop_packet();
      if (reinterpret_cast<uintptr_t>(raw_packet) <= 1) {
        if (raw_packet == nullptr) {
          std::lock_guard<SpinLock> guard(this->read_queue_lock);
          this->read_queue.emplace_back(
              reinterpret_cast<AVPacket*>(uintptr_t{1}));
          break;
        }
        throw std::runtime_error("second call on ended stream");
      }
      AVPacketUP local_packet(raw_packet);
      int const send_ret =
          avcodec_send_packet(this->codec_context.get(), local_packet.get());
      if (send_ret != 0) {
        // Let's guesstimate that one packet is one frame
        this->current_frame++;
        continue;
      }
      //while (true)
      {
        int const receive_ret = avcodec_receive_frame(
            this->codec_context.get(), this->av_frame.get());
        if (receive_ret == AVERROR(EAGAIN)) {
          continue;
        }
        if (receive_ret != 0) {
          throw std::runtime_error(
              "avcodec_receive_frame failed " + get_av_error(receive_ret));
        }
        int32_t alignment = 16;
        int32_t const preferred_stride =
            (this->codec_context->width * 3 + alignment - 1) & ~(alignment - 1);

        Frame::timestamp_s_t timestamp_s = -1.0;
        if (this->av_frame->pkt_dts != AV_NOPTS_VALUE) {
          timestamp_s =
              this->av_frame->best_effort_timestamp *
              av_q2d(this->format_context->streams[local_packet->stream_index]
                         ->time_base);
        }
        Frame::number_t const number = this->current_frame++;

        FrameUP ret(new Frame(
            this->deallocate_callback,
            this->userdata,
            {
                this->codec_context->height,  // height
                this->codec_context->width,  // width
                3,  // channels
                SCALAR_TYPE::U8,  // scalar_type
                preferred_stride,  // stride
                nullptr,  // data
                nullptr,  // user_data
            },
            number,
            timestamp_s));
        VRImage* image = &ret->image;

        (*this->allocate_callback)(image, this->userdata);
        if (!image->data) {
          throw std::runtime_error(
              "allocation callback failed: data is nullptr");
        }
        if (!this->sws_context) {  // because broken videos are weird
          this->sws_context = _create_converter(
              (AVPixelFormat)this->av_frame->format,
              this->codec_context->width,
              this->codec_context->height);
        }
        if (decode) {
          sws_scale(
              this->sws_context.get(),
              this->av_frame->data,
              this->av_frame->linesize,
              0,
              this->av_frame->height,
              &image->data,
              &image->stride);
        }
        if (!this->pushers.empty()) {
          MallocStream stream{32};
          thismsgpack::pack_array_header(this->pushers.size(), stream);
          for (auto const& pusher : this->pushers) {
            pusher(this->av_frame.get(), stream);
          }
          ret->extras = stream.data();
          ret->extras_size = stream.size();
        }
        return ret;
      }
      //}
    }  // while (true)
    return {nullptr};
  }
};

static void videoreader_ffmpeg_callback(
    void* avcl, int level, const char* fmt, va_list vl) {
  if (!avcl) {
    return;  // can't access userdata (opaque)
  }
  if (level > av_log_get_level()) {
    return;
  }
  AVClass* avc = *(AVClass**)avcl;
  // possible class names:
  // * AVFormatContext
  // * AVCodecContext
  // * AVIOContext
  // * SWResampler
  // * SWScaler
  // * URLContext

  void* opaque = nullptr;
  switch (avc->class_name[2]) {
  case 'F':  // AVFormatContext
    opaque = static_cast<AVFormatContext*>(avcl)->opaque;
    break;
  case 'C':  // AVCodecContext
    opaque = static_cast<AVCodecContext*>(avcl)->opaque;
    break;
  case 'I':  // AVIOContext
    opaque = static_cast<AVIOContext*>(avcl)->opaque;
    break;
  // case 'R': // SWResampler
  //   opaque = static_cast<SWResampler*>(avcl)->opaque;
  //   break;
  // case 'S': // SWScaler
  //   opaque = static_cast<SWScaler*>(avcl)->opaque;
  //   break;
  case 'L':  // URLContext
    // Opaque struct that has no public interface. Ignore.
    break;
  }
  if (opaque == nullptr) {
    return;
  }
  auto* const impl = reinterpret_cast<VideoReaderFFmpeg::Impl*>(opaque);
  VideoReader::LogLevel const vr_level = [level] {
    if (level <= AV_LOG_FATAL) {
      return VideoReader::LogLevel::FATAL;
    }
    if (level <= AV_LOG_ERROR) {
      return VideoReader::LogLevel::ERROR;
    }
    if (level <= AV_LOG_WARNING) {
      return VideoReader::LogLevel::WARNING;
    }
    if (level <= AV_LOG_INFO) {
      return VideoReader::LogLevel::INFO;
    }
    return VideoReader::LogLevel::DEBUG;
  }();
  char message[2048];
  av_log_format_line(
      avcl, level, fmt, vl, message, sizeof(message), &impl->print_prefix);
  impl->log_callback(message, vr_level, impl->userdata);
}

VideoReaderFFmpeg::VideoReaderFFmpeg(
    std::string const& url,
    std::vector<std::string> const& parameter_pairs,
    std::vector<std::string> const& extras,
    AllocateCallback allocate_callback,
    DeallocateCallback deallocate_callback,
    LogCallback log_callback,
    void* userdata) {
  avformat_network_init();
  avdevice_register_all();
  av_log_set_level(AV_LOG_INFO);
  if (log_callback != nullptr) {
    av_log_set_callback(videoreader_ffmpeg_callback);
  }
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
  av_register_all();
#endif
  this->impl = std::make_unique<VideoReaderFFmpeg::Impl>(
      url,
      parameter_pairs,
      extras,
      allocate_callback,
      deallocate_callback,
      log_callback,
      userdata);
}

bool VideoReaderFFmpeg::is_seekable() const {
  return this->impl->is_seekable();
}

VideoReader::Frame::number_t VideoReaderFFmpeg::size() const {
  return static_cast<VideoReader::Frame::number_t>(
      this->impl->av_stream->nb_frames);
}

AVPacket* VideoReaderFFmpeg::Impl::pop_packet() {
  while (true) {
    this->read_queue_lock.lock();
    if (this->read_queue.empty()) {
      this->read_queue_lock.unlock();
      std::this_thread::yield();
    } else {
      AVPacket* ret = this->read_queue.front();
      this->read_queue.pop_front();
      this->read_queue_lock.unlock();
      return ret;
    }
  }
}

VideoReaderFFmpeg::~VideoReaderFFmpeg() {
  this->impl->stop_requested = true;
  this->impl->read_thread.join();
}

VideoReader::FrameUP VideoReaderFFmpeg::next_frame(bool decode) {
  return this->impl->next_frame(decode);
}
