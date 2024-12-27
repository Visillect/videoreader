#include <videoreader/videoreader.h>
#include <videoreader/videowriter.h>
#include <stdint.h>

#define API extern "C"
typedef void (*videoreader_log)(char const*, int, void*);
typedef void (*videoreader_allocate)(char const*);

typedef struct {
  int32_t height;
  int32_t width;
  int32_t channels;
  int32_t scalar_type;
  int32_t stride;  // 0 when unknown. number of bytes between rows
  uint8_t *data;  // pointer to the first pixel
  void *user_data;  // user supplied data, useful for freeing in DeallocateCallback
} VRImage;


static_assert(sizeof(VRImage) == sizeof(VideoReader::VRImage), "error");

static std::string videoreader_what_str;

API char const* videoreader_what(void) {
  return videoreader_what_str.c_str();
}

API int videoreader_create(
    struct videoreader** reader,
    char const* video_path,
    char const* argv[],
    int argc,
    char const* extras[],
    int extrasc,
    videoreader_allocate alloc_callback,
    videoreader_allocate free_callback,
    videoreader_log callback,
    void* userdata) {
  try {
    std::vector<std::string> parameter_pairs;
    for (int idx{} ; idx < argc; ++idx ) {
      parameter_pairs.emplace_back(argv[idx]);
    }
    std::vector<std::string> extras_vec;
    for (int idx{} ; idx < extrasc; ++idx ) {
      extras_vec.emplace_back(extras[idx]);
    }
    auto video_reader = VideoReader::create(
        video_path,
        std::move(parameter_pairs),
        std::move(extras_vec),
        reinterpret_cast<VideoReader::AllocateCallback>(alloc_callback),
        reinterpret_cast<VideoReader::DeallocateCallback>(free_callback),
        reinterpret_cast<VideoReader::LogCallback>(callback),
        userdata);
    *reader = reinterpret_cast<struct videoreader*>(video_reader.release());
  } catch (std::exception& e) {
    videoreader_what_str = e.what();
    return -1;
  }
  return 0;
}

API int videoreader_set(
    struct videoreader* reader,
    char const* argv[],
    int argc
) {
  try {
    std::vector<std::string> parameter_pairs;
    for (int idx{} ; idx < argc; ++idx ) {
      parameter_pairs.emplace_back(argv[idx]);
    }
    reinterpret_cast<VideoReader*>(reader)->set(parameter_pairs);
  } catch (std::exception& e) {
    videoreader_what_str = e.what();
    return -1;
  }
  return 0;
}


API void videoreader_delete(struct videoreader* reader) {
  delete reinterpret_cast<VideoReader*>(reader);
}

API int videoreader_next_frame(
    struct videoreader* reader,
    VRImage* dst_img,
    uint64_t* number,
    double* timestamp_s,
    unsigned const char* extras[],
    unsigned int* extras_size,
    bool decode) {
  try {
    VideoReader::FrameUP frame =
        reinterpret_cast<VideoReader*>(reader)->next_frame(decode);
    if (!frame)
      return 1;

    auto const& image = frame->image;
    dst_img->height = image.height;
    dst_img->width = image.width;
    dst_img->channels = image.channels;
    dst_img->scalar_type = static_cast<int32_t>(image.scalar_type);
    dst_img->stride = image.stride;
    dst_img->data = image.data;
    dst_img->user_data = image.user_data;

    *number = frame->number;
    *timestamp_s = frame->timestamp_s;
    *extras = frame->extras;
    *extras_size = frame->extras_size;
    frame->extras = nullptr;
  } catch (std::exception& e) {
    videoreader_what_str = e.what();
    return -1;
  }
  return 0;
}

API int videoreader_size(struct videoreader* reader, uint64_t* count) {
  *count = reinterpret_cast<VideoReader*>(reader)->size();
  return 0;
}

API int videowriter_create(
    struct videowriter** writer,
    char const* video_path,
    VRImage const* frame_format,
    char const* argv[],
    int argc,
    bool realtime,
    videoreader_log log_callback,
    void* userdata
) {
  try {
    std::vector<std::string> parameter_pairs;
    for (int idx{} ; idx < argc; ++idx ) {
      parameter_pairs.emplace_back(argv[idx]);
    }
    *writer = reinterpret_cast<struct videowriter*>(new VideoWriter(
        video_path,
        *reinterpret_cast<VideoReader::VRImage const*>(frame_format),
        std::move(parameter_pairs),
        realtime,
        reinterpret_cast<VideoReader::LogCallback>(log_callback),
        userdata));
  } catch (std::exception& e) {
    videoreader_what_str = e.what();
    return -1;
  }
  return 0;
}

API void videowriter_delete(struct videowriter* reader) {
  delete reinterpret_cast<VideoWriter*>(reader);
}

API int videowriter_push(
  struct videowriter* writer,
  VRImage const* img,
  double timestamp_s) {
  try {
    VideoReader::Frame frame{0, nullptr, *reinterpret_cast<VideoReader::VRImage const*>(img), 0, timestamp_s};
    bool const successful_push = reinterpret_cast<VideoWriter*>(
      writer)->push(frame);
    return successful_push ? 0 : 1;
  } catch (std::exception& e) {
    videoreader_what_str = e.what();
    return -1;
  }
}

API int videowriter_close(
  struct videowriter* writer) {
  try {
    reinterpret_cast<VideoWriter*>(writer)->close();
  } catch (std::exception& e) {
    videoreader_what_str = e.what();
    return -1;
  }
  return 0;
}
