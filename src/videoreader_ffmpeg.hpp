#include <videoreader/videoreader.h>


class VideoReaderFFmpeg : public VideoReader
{
public:
  VideoReaderFFmpeg(
    std::string const& url,
    std::vector<std::string> const& parameter_pairs,
    std::vector<std::string> const& extras,
    VideoReader::LogCallback log_callback,
    void* userdata
  );

  bool is_seekable() const override;
  VideoReader::FrameUP next_frame(bool decode) override;
  VideoReader::Frame::number_t size() const override;

  struct Impl;
  std::unique_ptr<struct Impl> impl;

  ~VideoReaderFFmpeg();
};
