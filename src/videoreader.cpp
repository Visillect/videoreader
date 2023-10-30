#include <videoreader/videoreader.h>
#include <minimgapi/minimgapi.h>
#include <stdexcept>

#ifdef VIDEOREADER_WITH_FFMPEG
#include "videoreader_ffmpeg.hpp"
#endif

#ifdef VIDEOREADER_WITH_PYLON
#include "videoreader_pylon.hpp"
#endif

#ifdef VIDEOREADER_WITH_GALAXY
#include "videoreader_galaxy.hpp"
#endif

std::unique_ptr<VideoReader> VideoReader::create(
  std::string const& url,
  std::vector<std::string> const& parameter_pairs,
  std::vector<std::string> const& extras,
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
#ifdef VIDEOREADER_WITH_GALAXY
  if (url.find("galaxy://") == 0) {
    return std::unique_ptr<VideoReader>(
      new VideoReaderGalaxy(url, parameter_pairs, extras));
  }
#endif
#ifdef VIDEOREADER_WITH_FFMPEG
  return std::unique_ptr<VideoReader>(
    new VideoReaderFFmpeg(url, parameter_pairs, extras, log_callback, userdata));
#else
#  if !defined(VIDEOREADER_WITH_FFMPEG) && !defined(VIDEOREADER_WITH_PYLON) && !defined(VIDEOREADER_WITH_GALAXY)
  throw std::runtime_error("build without any video backed");
#  else
  throw std::runtime_error("unsupported uri");
#  endif
#endif
}

void VideoReader::set(
  std::vector<std::string> const& parameter_pairs
) {
  throw std::runtime_error("not implemented");
}


VideoReader::Frame::~Frame()
{
  FreeMinImage(&this->image);
  free(const_cast<unsigned char*>(this->extras));
  this->extras = nullptr;
  this->extras_size = 0;
}

VideoReader::~VideoReader() {}
