#include <videoreader/videoreader.h>
#include <minimgapi/minimgapi.h>
#include <stdexcept>

#ifdef VIDEOREADER_WITH_FFMPEG
#include "videoreader_ffmpeg.hpp"
#endif

#ifdef VIDEOREADER_WITH_PYLON
#include "videoreader_pylon.hpp"
#endif

std::unique_ptr<VideoReader> VideoReader::create(
  std::string const& url,
  std::vector<std::string> const& parameter_pairs,
  LogCallback log_callback,
  void* userdata
)
{
  if (parameter_pairs.size() % 2 != 0) {
    throw std::runtime_error("invalid videoreader parameters size");
  }

#ifdef VIDEOREADER_WITH_PYLON
  if (url == "pylon") {
    return std::unique_ptr<VideoReader>(
      new VideoReaderPylon(url, parameter_pairs));
  }
#endif
#ifdef VIDEOREADER_WITH_FFMPEG
  return std::unique_ptr<VideoReader>(
    new VideoReaderFFmpeg(url, parameter_pairs, log_callback, userdata));
#endif
#if !defined(VIDEOREADER_WITH_FFMPEG) && !defined(VIDEOREADER_WITH_PYLON)
  throw std::runtime_error("build without any video backed")
#endif
}

VideoReader::Frame::~Frame()
{
  FreeMinImage(&this->image);
}

VideoReader::~VideoReader() {}
