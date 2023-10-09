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
