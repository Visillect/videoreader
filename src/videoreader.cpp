#include <stdexcept>
#include <videoreader/videoreader.h>

#ifdef VIDEOREADER_WITH_FFMPEG
#include "videoreader_ffmpeg.hpp"
#endif

#ifdef VIDEOREADER_WITH_PYLON
#include "videoreader_pylon.hpp"
#endif

#ifdef VIDEOREADER_WITH_GALAXY
#include "videoreader_galaxy.hpp"
#endif

static void default_vr_allocate(VideoReader::VRImage* image, void* unused) {
  std::size_t size = image->stride * image->height;
  image->data = new (std::nothrow) uint8_t[size];
}

static void default_vr_deallocate(VideoReader::VRImage* image, void* unused) {
  delete[] static_cast<uint8_t*>(image->data);
  image->data = nullptr;
}

std::unique_ptr<VideoReader> VideoReader::create(
    std::string const& url,
    std::vector<std::string> const& parameter_pairs,
    std::vector<std::string> const& extras,
    AllocateCallback allocate_callback,
    DeallocateCallback delallocate_callback,
    LogCallback log_callback,
    void* userdata) {
  if (parameter_pairs.size() % 2 != 0) {
    throw std::runtime_error("invalid videoreader parameters size");
  }
  if (!allocate_callback && !delallocate_callback) {
    allocate_callback = default_vr_allocate;
    delallocate_callback = default_vr_deallocate;
  } else if (!(allocate_callback && delallocate_callback)) {
    throw std::runtime_error("all or no allocators MUST be specified");
  }

#ifdef VIDEOREADER_WITH_PYLON
  if (url == "pylon") {
    return std::unique_ptr<VideoReader>(new VideoReaderPylon(
        url,
        parameter_pairs,
        extras,
        allocate_callback,
        delallocate_callback,
        log_callback,
        userdata));
  }
#endif
#ifdef VIDEOREADER_WITH_GALAXY
  if (url.find("galaxy://") == 0) {
    return std::unique_ptr<VideoReader>(new VideoReaderGalaxy(
        url,
        parameter_pairs,
        extras,
        allocate_callback,
        delallocate_callback,
        log_callback,
        userdata));
  }
#endif
#ifdef VIDEOREADER_WITH_FFMPEG
  return std::unique_ptr<VideoReader>(new VideoReaderFFmpeg(
      url,
      parameter_pairs,
      extras,
      allocate_callback,
      delallocate_callback,
      log_callback,
      userdata));
#else
#if !defined(VIDEOREADER_WITH_FFMPEG) && !defined(VIDEOREADER_WITH_PYLON) && \
    !defined(VIDEOREADER_WITH_GALAXY)
  throw std::runtime_error("build without any video backed");
#else
  throw std::runtime_error("unsupported uri");
#endif
#endif
}

void VideoReader::set(std::vector<std::string> const& parameter_pairs) {
  throw std::runtime_error("not implemented");
}

VideoReader::Frame::~Frame() {
  if (this->free) {  // check that the frame wasn't moved
    (*this->free)(&this->image, this->userdata);
  }
  ::free(const_cast<unsigned char*>(this->extras));
  this->extras = nullptr;
  this->extras_size = 0;
}

VideoReader::~VideoReader() {
}
