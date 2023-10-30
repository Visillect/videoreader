#include <videoreader/videoreader.h>
#include <videoreader/videowriter.h>
#include <stdint.h>

#define API extern "C"
typedef void (*videoreader_log)(char const*, int, void*);

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
    MinImg* dst_img,
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

    *dst_img = frame->image;
    *number = frame->number;
    *timestamp_s = frame->timestamp_s;
    *extras = frame->extras;
    *extras_size = frame->extras_size;
    frame->image.is_owner = false;
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
    struct MinImg const* frame_format,
    char const* argv[],
    int argc,
    bool realtime,
    videoreader_log callback,
    void* userdata
) {
  try {
    std::vector<std::string> parameter_pairs;
    for (int idx{} ; idx < argc; ++idx ) {
      parameter_pairs.emplace_back(argv[idx]);
    }
    *writer = reinterpret_cast<struct videowriter*>(new VideoWriter(
        video_path,
        *frame_format,
        std::move(parameter_pairs),
        realtime,
        reinterpret_cast<VideoReader::LogCallback>(callback),
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
  struct MinImg const* img,
  double timestamp_s) {
  try {
    VideoReader::Frame frame{*img, 0, timestamp_s};
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