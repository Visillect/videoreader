#include <videoreader/videoreader.h>
#include <stdint.h>

#define API extern "C"
typedef void (*videoreader_log)(char*, int, void*);

static std::string videoreader_what_str;

API char const* videoreader_what(void) {
  return videoreader_what_str.c_str();
}

API int videoreader_create(
    struct videoreader** reader,
    const char* video_path,
    char const* argv[],
    int argc,
    videoreader_log callback,
    void* userdata) {
  try {
    std::vector<std::string> parameter_pairs;
    for (int idx{} ; idx < argc; ++idx ) {
      parameter_pairs.emplace_back(argv[idx]);
    }
    auto video_reader = VideoReader::create(
        video_path,
        std::move(parameter_pairs),
        reinterpret_cast<VideoReader::LogCallback>(callback),
        userdata);
    *reader = reinterpret_cast<struct videoreader*>(video_reader.release());
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
    bool decode) {
  try {
    VideoReader::FrameUP frame =
        reinterpret_cast<VideoReader*>(reader)->next_frame(decode);
    if (!frame)
      return 1;

    *dst_img = frame->image;
    *number = frame->number;
    *timestamp_s = frame->timestamp_s;
    frame->image.is_owner = false;
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
