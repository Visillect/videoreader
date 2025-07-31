#include <videoreader/videoreader.hpp>

class VideoReaderFFmpeg : public VideoReader {
public:
  VideoReaderFFmpeg(
      std::string const& url,
      std::vector<std::string> const& parameter_pairs,
      std::vector<std::string> const& extras,
      AllocateCallback allocate_cb,
      DeallocateCallback deallocate_cb,
      LogCallback log_callback,
      void* userdata);

  bool is_seekable() const override;
  FrameUP next_frame(bool decode) override;
  Frame::number_t size() const override;
  void stop() override;

  struct Impl;
  std::unique_ptr<struct Impl> impl;

  ~VideoReaderFFmpeg();
};
