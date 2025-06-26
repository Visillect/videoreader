#include "ffmpeg_common.hpp"
extern "C" {
#include <libavutil/avutil.h>
}

AVDictionaryUP
_create_dict_from_params_vec(std::vector<std::string> const& parameter_pairs) {
  AVDictionary* options = nullptr;

  for (std::vector<std::string>::const_iterator it = parameter_pairs.begin();
       it != parameter_pairs.end();
       ++it) {
    std::string const& key = *it;
    std::string const& value = *++it;
    av_dict_set(&options, key.c_str(), value.c_str(), 0);
  }
  return AVDictionaryUP{options};
}

std::string get_av_error(int const errnum) {
  std::string ret(512, '\0');
  if (av_strerror(errnum, ret.data(), 511) == 0) {
    ret.resize(ret.find('\0'));
  } else {
    ret = "unknown av error";
  }
  return ret;
}

void videoreader_ffmpeg_callback(
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
  auto* const impl = reinterpret_cast<FFmpegLogInfo*>(opaque);
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
