#include "videoreader_ffmpeg.hpp"

//
// Resources:
//   * https://blogs.gentoo.org/lu_zero/2016/03/29/new-avcodec-api/
//
// Pitfalls (debugging requires a video for reproduction):
//   * Calls `avcodec_receive_frame` only once, unlike docs are suggesting
//   * Doesn't support dynamic resolution change

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavdevice/avdevice.h>
}
#include <vector>
#include <deque>
#include <thread>
#include <atomic>
#include <mutex>
#include <minimgapi/minimgapi.h>

class SpinLock {
  std::atomic_flag lck = ATOMIC_FLAG_INIT;
public:
  void lock() { while (lck.test_and_set(std::memory_order_acquire)); }
  void unlock() { lck.clear(std::memory_order_release); }
};

struct AVPacketDeleter {
  void operator()(AVPacket* p) const noexcept {
    av_packet_free(&p);
  }
};
using AVPacketUP = std::unique_ptr<AVPacket, AVPacketDeleter>;

struct AVFormatContextDeleter {
  void operator()(AVFormatContext *c) const noexcept {
    avformat_close_input(&c);
    avformat_free_context(c);
  }
};
using AVFormatContextUP = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;

struct AVCodecContextDeleter {
  void operator() (AVCodecContext* ctx) const noexcept {
    avcodec_close(ctx);
    avcodec_free_context(&ctx);
  }
};
using AVCodecContextUP = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;

struct SwsContextDeleter {
  void operator() (SwsContext* sws_context) const noexcept {
    sws_freeContext(sws_context);
  }
};
using SwsContextUP = std::unique_ptr<SwsContext, SwsContextDeleter>;

struct AVFrameDeleter {
  void operator() (AVFrame* av_frame) const noexcept {
    av_frame_free(&av_frame);
  }
};
using AVFrameUP = std::unique_ptr<AVFrame, AVFrameDeleter>;

struct AVDictionaryDeleter {
  void operator() (AVDictionary* av_dictionary) const noexcept {
    av_dict_free(&av_dictionary);
  }
};
using AVDictionaryUP = std::unique_ptr<AVDictionary, AVDictionaryDeleter>;

static AVDictionaryUP _create_dict_from_params_vec(
  std::vector<std::string> const& parameter_pairs)
{
  AVDictionary *options = nullptr;
  for (std::vector<std::string>::const_iterator it = parameter_pairs.begin();
    it != parameter_pairs.end(); ++it
  ) {
    std::string const& key = *it;
    std::string const& value = *++it;
    av_dict_set(&options, key.c_str(), value.c_str(), 0);
  }
  return AVDictionaryUP{options};
}

static std::string ret_to_string(int ernum)
{
  char buf[4096];
  if (av_strerror(ernum, buf, sizeof(buf)) == 0)
    return buf;
  return "unknown error (ret=" + std::to_string(ernum) + ")";
}

static AVFormatContextUP _get_format_context(
  std::string const& filename,
  AVDictionaryUP &options)
{
  AVInputFormat *input_format = nullptr;
  std::string path_to_use = filename;
  if (filename.substr(0, 8) == "dshow://") {
    input_format = av_find_input_format("dshow");
    path_to_use = filename.substr(8, filename.size());
  }
  AVFormatContext *format_context = nullptr;
  AVDictionary *opts = options.release();
  int const ret = avformat_open_input(
    &format_context, path_to_use.c_str(), input_format, &opts);
  options.reset(opts);
  if (ret < 0)
    throw std::runtime_error("Can't open `" + filename + "`, " + ret_to_string(ret));
  if (!format_context)
    throw std::runtime_error("invalid AVFormatContext");
  return AVFormatContextUP(format_context);
}

static AVStream* _get_video_stream(AVFormatContext* format_context)
{
  if (avformat_find_stream_info(format_context, NULL) != 0)
    throw std::runtime_error("avformat_find_stream_info failed");
  for (size_t stream_idx = 0; stream_idx < format_context->nb_streams; ++stream_idx)
  {
    AVStream* av_stream = format_context->streams[stream_idx];
    if (av_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
      return av_stream;
  }
  throw std::runtime_error("video steam not found");
}

static AVCodecContextUP _get_codec_context(
  AVCodecParameters const* av_codecpar,
  AVDictionaryUP &options)
{
  AVCodec const* av_codec = avcodec_find_decoder(av_codecpar->codec_id);
  if (!av_codec)
    throw std::runtime_error("Unsupported codec");
  auto codec_context = AVCodecContextUP(avcodec_alloc_context3(av_codec));

  if (avcodec_parameters_to_context(codec_context.get(), av_codecpar) != 0)
    throw std::runtime_error("avcodec_parameters_to_context failed");

  AVDictionary *opts = options.release();
  if (avcodec_open2(codec_context.get(), av_codec, &opts) != 0)
    throw std::runtime_error("avcodec_open2 failed");
  options.reset(opts);

  return codec_context;
}

static SwsContextUP _create_converter(
  AVPixelFormat const pix_format, int const width, int const height)
{
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
    width, height, new_pix_format,
    width, height, AV_PIX_FMT_RGB24,
    SWS_BICUBIC, nullptr, nullptr, nullptr)};
  if (!converter)
    throw std::runtime_error("Converter initialization failed");
  return converter;
}

struct VideoReaderFFmpeg::Impl {
  decltype(VideoReader::Frame::number) current_frame = 0;
  std::atomic<bool> stop_requested;
  AVFormatContextUP format_context;
  AVStream* av_stream;
  AVCodecContextUP codec_context;
  AVFrameUP av_frame;
  SwsContextUP sws_context;

  std::thread read_thread;  // for network to work
  std::deque<AVPacket*> read_queue; // read buffer
  SpinLock read_queue_lock;
  AVPacket* pop_packet();

  Impl(
    std::string const& url,
    std::vector<std::string> const& parameter_pairs
  ) :
    stop_requested(false)
  {
    AVDictionaryUP options = _create_dict_from_params_vec(parameter_pairs);
    this->format_context = _get_format_context(url, options);
    this->av_stream = _get_video_stream(format_context.get());
    this->codec_context = _get_codec_context(av_stream->codecpar, options);
    this->av_frame = AVFrameUP(av_frame_alloc());
    if (this->codec_context->pix_fmt != AV_PIX_FMT_NONE) {
      this->sws_context = _create_converter(
        this->codec_context->pix_fmt,
        this->codec_context->width,
        this->codec_context->height);
    }

    if (options) {
      char *buf = NULL;
      if (av_dict_get_string(options.get(), &buf, '=', ',') < 0)
        throw std::runtime_error("error formatting parameters dictionary");
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
  // seeking to timestemt 0.0 helps prevent compression
  // artifacts on broken videos
  av_seek_frame(this->format_context.get(), -1, 0, AVSEEK_FLAG_ANY);
  while (!this->stop_requested) {
    AVPacketUP thread_packet(av_packet_alloc());
    int const read_ret = av_read_frame(this->format_context.get(), thread_packet.get());
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
      if (this->read_queue.size() > 100)
      {
        if (this->is_seekable()) {  // offline - wait for data
          this->read_queue_lock.unlock();
          while (!this->stop_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            {
              std::lock_guard<SpinLock> guard(this->read_queue_lock);
              if (this->read_queue.size() < 80)
                break;
            }
          }
          this->read_queue_lock.lock(); // lock here for easier unlock logic
        } else {  // realtime - clear buffer
          for (int i = 0; i < 90; ++i)
            this->read_queue.pop_front();
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

};

VideoReaderFFmpeg::VideoReaderFFmpeg(
  std::string const& url,
  std::vector<std::string> const& parameter_pairs )
  {
  avformat_network_init();
  avdevice_register_all();
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58,9,100)
  av_register_all();
#endif
  this->impl = std::unique_ptr<VideoReaderFFmpeg::Impl>(
    new VideoReaderFFmpeg::Impl(url, parameter_pairs)
  );
}

bool VideoReaderFFmpeg::is_seekable() const {
  return this->impl->is_seekable();
}

VideoReader::Frame::number_t VideoReaderFFmpeg::size() const
{
  return static_cast<VideoReader::Frame::number_t>(this->impl->av_stream->nb_frames);
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


VideoReader::FrameUP VideoReaderFFmpeg::next_frame()
{
  while (true)
  {
    AVPacket* raw_packet = this->impl->pop_packet();
    if (!raw_packet)
      break;
    AVPacketUP local_packet(raw_packet);
    int const send_ret = avcodec_send_packet(this->impl->codec_context.get(), local_packet.get());
    if (send_ret != 0) {
      // Let's guesstimate that one packet is one frame
      this->impl->current_frame++;
      continue;
    }
    //while (true)
    {
      int const receive_ret = avcodec_receive_frame(this->impl->codec_context.get(), this->impl->av_frame.get());
      if (receive_ret == AVERROR(EAGAIN))
        continue;
      if (receive_ret != 0)
        throw std::runtime_error("avcodec_receive_frame failed " + ret_to_string(receive_ret));

      FrameUP ret(new Frame());
      if (NewMinImagePrototype(&ret->image, this->impl->codec_context->width, this->impl->codec_context->height, 3, TYP_UINT8) != 0)
        throw std::runtime_error("NewMinImagePrototype failed");
      if (!this->impl->sws_context) {  // because broken videos are weird
        this->impl->sws_context = _create_converter(
          (AVPixelFormat)this->impl->av_frame->format,
          this->impl->codec_context->width,
          this->impl->codec_context->height);
      }
      sws_scale(
        this->impl->sws_context.get(),
        this->impl->av_frame->data,
        this->impl->av_frame->linesize,
        0,
        this->impl->av_frame->height,
        &ret->image.p_zero_line,
        &ret->image.stride
      );
      ret->number = this->impl->current_frame++;
      if (this->impl->av_frame->pkt_dts != AV_NOPTS_VALUE)
      {
        ret->timestamp_s = this->impl->av_frame->best_effort_timestamp * av_q2d(
          this->impl->format_context->streams[local_packet->stream_index]->time_base);
      } else {
        ret->timestamp_s = -1.0;
      }
      return ret;
    }
    //}
  }  // while (true)
  return FrameUP(nullptr);
}
