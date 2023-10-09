#pragma once
#include "videoreader.h"

class VideoWriter
{
public:
  struct Impl;
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
