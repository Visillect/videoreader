#include <videoreader/videoreader.hpp>

class VideoReaderGalaxy : public VideoReader {
public:
  VideoReaderGalaxy(
      std::string const& url,
      std::vector<std::string> const& parameter_pairs,
      std::vector<std::string> const& extras,
      AllocateCallback allocate_callback,
      DeallocateCallback deallocate_callback,
      LogCallback log_callback,
      void* userdata);

  bool is_seekable() const override;
  VideoReader::FrameUP next_frame(bool decode) override;
  VideoReader::Frame::number_t size() const override;
  void set(std::vector<std::string> const& parameter_pairs) override;
  void stop() override;

private:
  struct Impl;
  std::unique_ptr<struct Impl> impl;
  ~VideoReaderGalaxy() override;
};
