#pragma once
#include <string>
#include <vector>
#include <memory>
#include <minbase/minimg.h>


class VideoReader
{
public:
  struct Frame {
    using number_t = uint64_t;
    using timestamp_s_t = real64_t;
    MinImg image;
    number_t number;  // zero-indexed; this number is not continuous due to possible invalid data frames
    timestamp_s_t timestamp_s;  // seconds since the start of the video
    ~Frame();  // Frees MinImg
  };

  enum class LogLevel : int {  // int so the interface can be used from `C`
    FATAL,
    ERROR,
    WARNING,
    INFO,
    DEBUG
  };

  using FrameUP = std::unique_ptr<Frame>;
  using LogCallback = void (*)(char const* message, LogLevel log_level, void* userdata);

  // url: file path or any ffmpeg url
  // parameter_pairs: protocol parameters, for example:
  //   {"analyzeduration", "0", "rtsp_transport", "http",
  //    "reorder_queue_size", "13", "probesize", "32",
  //    "fflags", "+nobuffer +igndts", "rtbuffsize", "64738",
  //    "flags", "low_delay"}
  //
  // see https://ffmpeg.org/ffmpeg-protocols.html for more details
  static std::unique_ptr<VideoReader> create(
    std::string const& url,
    std::vector<std::string> const& parameter_pairs = {}, // size % 2 == 0
    LogCallback log_callback = nullptr,
    void* userdata = nullptr
  );
  virtual ~VideoReader();

  VideoReader& operator=(VideoReader const&) = delete;

  // number of frames if known or 0 (see AVStream::nb_frames)
  virtual Frame::number_t size() const = 0;

  // offline video should be seekable, realtime not (see AVIOContext::seekable)
  virtual bool is_seekable() const = 0;

  // decode: decode the frame (false is useful for skipping frames,
  //         the result will be a valid frame with uinitialized pixel values)
  // frame data are read in a separate thread
  virtual FrameUP next_frame(bool decode=true) = 0;
};
