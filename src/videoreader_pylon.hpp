#include <videoreader/videoreader.h>


class VideoReaderPylon : public VideoReader
{
public:
  VideoReaderPylon(
    std::string const& url,
    std::vector<std::string> const& parameter_pairs
  );

  bool is_seekable() const override;
  VideoReader::FrameUP next_frame(bool decode) override;
  VideoReader::Frame::number_t size() const override;

private:
  struct Impl;
  std::unique_ptr<struct Impl> impl;
  ~VideoReaderPylon();
};
