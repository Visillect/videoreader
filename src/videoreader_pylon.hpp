#include <videoreader/videoreader.h>


class VideoReaderPylon : public VideoReader
{
public:
  VideoReaderPylon(
    std::string const& url,
    std::vector<std::string> const& parameter_pairs,
    std::vector<std::string> const& extras,
    AllocateCallback allocate_cb,
    DeallocateCallback deallocate_cb,
    VideoReader::LogCallback log_callback,
    void* userdata
  );

  bool is_seekable() const override;
  FrameUP next_frame(bool decode) override;
  Frame::number_t size() const override;

private:
  struct Impl;
  std::unique_ptr<struct Impl> impl;
  ~VideoReaderPylon();
};
