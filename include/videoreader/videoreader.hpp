#pragma once
#include <memory>
#include <string>
#include <vector>

class VideoReader {
public:
  enum class SCALAR_TYPE : int32_t { U8, U16 };
  struct VRImage {
    int32_t height;
    int32_t width;
    int32_t channels;
    SCALAR_TYPE scalar_type;
    int32_t stride;  // 0 when unknown. number of bytes between rows
    uint8_t* data;  // pointer to the first pixel
    void*
        user_data;  // user supplied data, useful for freeing in DeallocateCallback
  };

  /**
   * The function that allocates "new" image.
   * Useful for using different image libraries and to allow memory reuse
   * user must fill `VRImage::data`, and optionally `VRImage::user_data`
   *
   * When `VRImage.data` is left `nullptr`, the library treats it like
   * memory allocation error
   */
  using AllocateCallback = void (*)(VRImage*, void*);
  using DeallocateCallback = void (*)(VRImage*, void*);

  struct Frame {
    using number_t = uint64_t;
    using timestamp_s_t = double;
    Frame(
        DeallocateCallback free,
        void* userdata,
        VRImage const& image,
        number_t number,
        timestamp_s_t timestamp_s) :
        number{number},
        timestamp_s{timestamp_s},
        free{free},
        userdata{userdata},
        image{image} {
    }
    number_t
        number;  // zero-indexed; this number is not continuous due to possible invalid data frames
    timestamp_s_t timestamp_s;  // seconds since the start of the video
    unsigned char const*
        extras{};  // nullptr ot msgpack list in requested order. MUST be freed by `free` ca;;
    unsigned int extras_size{};  // num bytes in extra
    DeallocateCallback free;
    void* userdata;
    VRImage image;
    ~Frame();  // Frees VRImage and extras
  };

  enum class LogLevel : int {  // int so the interface can be used from `C`
    FATAL,
    ERROR,
    WARNING,
    INFO,
    DEBUG
  };

  /**
   * The type that "next_frame" returns
   */
  using FrameUP = std::unique_ptr<Frame>;

  /**
   * Main logging callback. Useful for debugging and to not flood stdout
   */
  using LogCallback =
      void (*)(char const* message, LogLevel log_level, void* userdata);

public:
  // url: file path or any ffmpeg url
  // parameter_pairs: protocol parameters, for example:
  //   {"analyzeduration", "0", "rtsp_transport", "http",
  //    "reorder_queue_size", "13", "probesize", "32",
  //    "fflags", "+nobuffer +igndts", "rtbuffsize", "64738",
  //    "flags", "low_delay"}
  //
  // see https://ffmpeg.org/ffmpeg-protocols.html for more details
  static std::unique_ptr<VideoReader> create(
      std::string const& url,
      std::vector<std::string> const& parameter_pairs = {},  // size % 2 == 0
      std::vector<std::string> const& extras = {},  // for extra_data
      AllocateCallback alloc_callback = nullptr,
      DeallocateCallback dealloc_callback = nullptr,
      LogCallback log_callback = nullptr,
      void* userdata = nullptr);
  virtual ~VideoReader();

  VideoReader& operator=(VideoReader const&) = delete;

  // number of frames if known or 0 (see AVStream::nb_frames)
  virtual Frame::number_t size() const = 0;

  // see `parameter_pairs` from constructor
  virtual void set(std::vector<std::string> const& parameter_pairs);

  // offline video should be seekable, realtime not (see AVIOContext::seekable)
  virtual bool is_seekable() const = 0;

  // decode: decode the frame (false is useful for skipping frames,
  //         the result will be a valid frame with uinitialized pixel values)
  // frame data are read in a separate thread
  virtual FrameUP next_frame(bool decode = true) = 0;
};
