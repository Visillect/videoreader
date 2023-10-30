#pragma once
#include "videoreader.h"

class VideoWriter
{
public:
  struct Impl;

  // uri: path to a file
  // format: initial format for the data (should not be needed in the feature)
  // parameter_pairs: codec parameters
  // realtime: when true, "push" sends frames to writing queue and exits
  // log_callback: log callback (currently unused)
  // userdata: data for log_callback (currently unused)
  VideoWriter(
    std::string const& uri,
    MinImg const& format,
    std::vector<std::string> const& parameter_pairs = {}, // size % 2 == 0
    bool realtime = false,
    VideoReader::LogCallback log_callback = nullptr,
    void* userdata = nullptr
  );
  VideoWriter& operator=(VideoWriter const&) = delete;
  bool push(VideoReader::Frame const&);
  void close();

  ~VideoWriter();
protected:
    std::unique_ptr<Impl> impl;
};
