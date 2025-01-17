#include "videoreader_idatum.hpp"
#include "/opt/iDatum/include/MvCameraControl.h"
#include "spinlock.hpp"
#include "thismsgpack.hpp"
#include <MvCameraControl.h>
#include <algorithm>  // std::transform
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>  // std::runtime_error
#include <thread>
#include <unordered_map>

static char const* get_error_description(unsigned int error_code) {
  switch (error_code) {
  case MV_E_HANDLE:
    return "Error or invalid handle";
  case MV_E_SUPPORT:
    return "Not supported function";
  case MV_E_BUFOVER:
    return "Buffer overflow";
  case MV_E_CALLORDER:
    return "Function calling order error";
  case MV_E_PARAMETER:
    return "Incorrect parameter";
  case MV_E_RESOURCE:
    return "Applying resource failed";
  case MV_E_NODATA:
    return "No data";
  case MV_E_PRECONDITION:
    return "Precondition error, or running environment changed";
  case MV_E_VERSION:
    return "Version mismatches";
  case MV_E_NOENOUGH_BUF:
    return "Insufficient memory";
  case MV_E_ABNORMAL_IMAGE:
    return "Abnormal image, maybe incomplete image because of lost packet";
  case MV_E_LOAD_LIBRARY:
    return "Load library failed";
  case MV_E_NOOUTBUF:
    return "No Available Buffer";
  case MV_E_UNKNOW:
    return "Unknown error";
  case MV_E_GC_GENERIC:
    return "General error";
  case MV_E_GC_ARGUMENT:
    return "Illegal parameters";
  case MV_E_GC_RANGE:
    return "The value is out of range";
  case MV_E_GC_PROPERTY:
    return "Property";
  case MV_E_GC_RUNTIME:
    return "Running environment error";
  case MV_E_GC_LOGICAL:
    return "Logical error";
  case MV_E_GC_ACCESS:
    return "Node accessing condition error";
  case MV_E_GC_TIMEOUT:
    return "Timeout";
  case MV_E_GC_DYNAMICCAST:
    return "Transformation exception";
  case MV_E_GC_UNKNOW:
    return "GenICam unknown error";
  case MV_E_NOT_IMPLEMENTED:
    return "The command is not supported by device";
  case MV_E_INVALID_ADDRESS:
    return "The target address being accessed does not exist";
  case MV_E_WRITE_PROTECT:
    return "The target address is not writable";
  case MV_E_ACCESS_DENIED:
    return "No permission";
  case MV_E_BUSY:
    return "Device is busy, or network disconnected";
  case MV_E_PACKET:
    return "Network data packet error";
  case MV_E_NETER:
    return "Network error";
  case MV_E_IP_CONFLICT:
    return "Device IP conflict";
  case MV_E_USB_READ:
    return "Reading USB error";
  case MV_E_USB_WRITE:
    return "Writing USB error";
  case MV_E_USB_DEVICE:
    return "Device exception";
  case MV_E_USB_GENICAM:
    return "GenICam error";
  case MV_E_USB_BANDWIDTH:
    return "Insufficient bandwidth, this error code is newly added";
  case MV_E_USB_DRIVER:
    return "Driver mismatch or unmounted drive";
  case MV_E_USB_UNKNOW:
    return "USB unknown error";
  case MV_E_UPG_FILE_MISMATCH:
    return "Firmware mismatches";
  case MV_E_UPG_LANGUSGE_MISMATCH:
    return "Firmware language mismatches";
  case MV_E_UPG_CONFLICT:
    return "Upgrading conflicted (repeated upgrading requests during device "
           "upgrade)";
  case MV_E_UPG_INNER_ERR:
    return "Camera internal error during upgrade";
  case MV_E_UPG_UNKNOW:
    return "Unknown error during upgrade";
  default:
    return nullptr;
  }
}

static void throw_ret(unsigned int ret, char const* info) {
  const char* description = get_error_description(ret);
  std::string const message{
      std::string{"iDatum: "} + info + ": " +
      (description ? std::string{description} : "code " + std::to_string(ret))};
  throw std::runtime_error(message.c_str());
}

#define IDATUM_CHECK(cb, info) \
  do { \
    auto const ret = cb; \
    if (ret != MV_OK) { \
      throw_ret(ret, info); \
    } \
  } while (false)

static std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), (int (*)(int))std::tolower);
  return s;
}

static std::string get_device_name(MV_CC_DEVICE_INFO const* info) {
  switch (info->nTLayerType) {
  case MV_GIGE_DEVICE: {
    unsigned int const ip = info->SpecialInfo.stGigEInfo.nCurrentIp;
    return std::to_string((ip >> 24) & 0xff) + "." +
           std::to_string((ip >> 16) & 0xff) + "." +
           std::to_string((ip >> 8) & 0xff) + "." +
           std::to_string((ip >> 0) & 0xff);
  }
  case MV_USB_DEVICE: {
    auto const name = reinterpret_cast<char const*>(
        info->SpecialInfo.stUsb3VInfo.chUserDefinedName);
    auto const sz = strnlen(name, INFO_MAX_BUFFER_SIZE);
    return std::string{name, sz};
  }
  default:
    return "device not implemented";
  }
}

static std::string join_entities(std::vector<std::string> const& entities) {
  std::string ret{};
  for (size_t entry_idx{}; entry_idx != entities.size(); ++entry_idx) {
    if (entry_idx) {
      ret += ", ";
    }
    ret += "`" + entities[entry_idx] + "`";
  }
  return ret;
}

struct HandleDeleter {
  void operator()(void* handle) const noexcept {
    MV_CC_DestroyHandle(&handle);
  }
};
using HandleUP = std::unique_ptr<void, HandleDeleter>;

struct VideoReaderIDatum::Impl {
  HandleUP handle;
  std::deque<FrameUP> read_queue;
  std::atomic<bool> stop_requested;
  SpinLock read_queue_lock;
  std::thread thread;
  std::exception_ptr exception;
  AllocateCallback allocate_callback;
  DeallocateCallback deallocate_callback;
  VideoReader::LogCallback log_callback;
  void* userdata;

  Impl(
      HandleUP handle,
      std::vector<std::string> const& parameter_pairs,
      std::vector<std::string> const& extras,
      AllocateCallback allocate_callback,
      DeallocateCallback deallocate_callback,
      VideoReader::LogCallback log_callback,
      void* userdata) :
      handle{std::move(handle)},
      stop_requested{false},
      allocate_callback{allocate_callback},
      deallocate_callback{deallocate_callback},
      log_callback{log_callback},
      userdata{userdata} {
    this->thread = std::thread(&VideoReaderIDatum::Impl::read, this);
  }

  void read() {
    MV_FRAME_OUT_INFO_EX frame_out_info_ex{};
    try {
      MVCC_INTVALUE_EX playload_value{};
      MV_CC_GetIntValueEx(this->handle.get(), "PayloadSize", &playload_value);
      IDATUM_CHECK(MV_CC_StartGrabbing(this->handle.get()), "start grabbing");
      MVCC_INTVALUE width_value{};
      IDATUM_CHECK(
          MV_CC_GetWidth(this->handle.get(), &width_value), "get width");
      MVCC_INTVALUE height_value{};
      IDATUM_CHECK(
          MV_CC_GetHeight(this->handle.get(), &height_value), "get height");
      MVCC_ENUMVALUE pixel_type_value{};
      IDATUM_CHECK(
          MV_CC_GetPixelFormat(this->handle.get(), &pixel_type_value),
          "get pixel type");

      int32_t const channels = [&]() {
        if (pixel_type_value.nCurValue & MV_GVSP_PIX_MONO) {
          return 1;
        }
        if (pixel_type_value.nCurValue & MV_GVSP_PIX_COLOR) {
          return 3;
        }
        throw std::runtime_error("not implemented pixel type");
      }();

      SCALAR_TYPE const scalar_type = [&]() {
        if ((pixel_type_value.nCurValue & MV_PIXEL_BIT_COUNT(8)) ==
            MV_PIXEL_BIT_COUNT(8)) {
          return SCALAR_TYPE::U8;
        }
        if ((pixel_type_value.nCurValue & MV_PIXEL_BIT_COUNT(8)) ==
            MV_PIXEL_BIT_COUNT(8)) {
          return SCALAR_TYPE::U16;
        }
        throw std::runtime_error("not implemented pixel depth");
      }();
      int32_t const width = width_value.nCurValue;
      int32_t const height = height_value.nCurValue;

      while (!this->stop_requested) {
        int32_t const preferred_stride = 0;
        Frame::timestamp_s_t const timestamp_s = 0.0;
        Frame::number_t const number = 0;
        FrameUP frame(new Frame(
            this->deallocate_callback,
            this->userdata,
            {
                height,  // height
                width,  // width
                channels,  // channels
                SCALAR_TYPE::U8,  // scalar_type
                width,  // stride
                nullptr,  // data
                nullptr,  // user_data
            },
            number,
            timestamp_s));
        VRImage* image = &frame->image;

        (*this->allocate_callback)(image, this->userdata);
        if (!image->data) {
          throw std::runtime_error(
              "allocation callback failed: data is nullptr");
        }
        IDATUM_CHECK(
            MV_CC_GetOneFrameTimeout(
                this->handle.get(),
                image->data,
                playload_value.nCurValue,  // this is 100% unsafe
                &frame_out_info_ex,
                3000),
            "get image buffer");
        frame->timestamp_s =
            ((static_cast<uint64_t>(frame_out_info_ex.nDevTimeStampHigh)
              << 32) |
             frame_out_info_ex.nDevTimeStampLow) *
            1e-8;
        frame->number = frame_out_info_ex.nFrameNum;
        {
          std::lock_guard<SpinLock> guard(this->read_queue_lock);
          if (this->read_queue.size() > 9) {
            remove_every_second_item(this->read_queue);
          }
          this->read_queue.emplace_back(std::move(frame));
        }
      }
    } catch (...) {
      this->stop_requested = true;
      this->exception = std::current_exception();
    }
  }
  FrameUP pop_grab_result() {
    while (true) {
      std::lock_guard<SpinLock> guard(this->read_queue_lock);
      if (this->read_queue.empty()) {
        if (this->stop_requested) {
          break;
        }
        this->read_queue_lock.unlock();
        std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      auto ret = std::move(this->read_queue.front());
      this->read_queue.pop_front();
      return ret;
    }
    if (this->thread.joinable()) {
      this->thread.join();
    }
    if (this->exception) {
      std::rethrow_exception(this->exception);
    }
    return nullptr;
  }
};

VideoReaderIDatum::VideoReaderIDatum(
    std::string const& url,
    std::vector<std::string> const& parameter_pairs,
    std::vector<std::string> const& extras,
    AllocateCallback allocate_callback,
    DeallocateCallback deallocate_callback,
    LogCallback log_callback,
    void* userdata) {

  auto const name = url.substr(9);

  MV_CC_DEVICE_INFO_LIST device_infos{};
  IDATUM_CHECK(
      MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &device_infos),
      "device enumeration");
  if (!device_infos.nDeviceNum) {
    throw std::runtime_error("No iDatum devices found");
  }
  std::vector<std::string> all_device_names;
  all_device_names.reserve(device_infos.nDeviceNum);
  HandleUP handle = [&]() {
    void* handle{};
    for (unsigned int dev_idx{}; dev_idx < device_infos.nDeviceNum; ++dev_idx) {
      MV_CC_DEVICE_INFO const* info = device_infos.pDeviceInfo[dev_idx];
      auto cur_name = get_device_name(info);
      if (cur_name == name) {
        IDATUM_CHECK(
            MV_CC_CreateHandleWithoutLog(&handle, info),
            "create device handle");
        return HandleUP(handle);
      }
      all_device_names.emplace_back(std::move(cur_name));
    }
    throw std::runtime_error(
        "Requested device not found, available devices are" +
        join_entities(all_device_names));
  }();
  IDATUM_CHECK(
      MV_CC_OpenDevice(handle.get(), MV_ACCESS_Exclusive, 0), "open device");

  this->impl = std::make_unique<Impl>(
      std::move(handle),
      parameter_pairs,
      extras,
      allocate_callback,
      deallocate_callback,
      log_callback,
      userdata);
}

bool VideoReaderIDatum::is_seekable() const {
  return false;
}

VideoReader::FrameUP VideoReaderIDatum::next_frame(bool decode) {
  return this->impl->pop_grab_result();
}

VideoReader::Frame::number_t VideoReaderIDatum::size() const {
  return 0;
}

void VideoReaderIDatum::set(std::vector<std::string> const& parameter_pairs) {
  if (parameter_pairs.size() % 2 != 0) {
    throw std::runtime_error("invalid videoreader parameters size");
  }
  throw std::runtime_error("parameter setting is not yet implemented");
}

VideoReaderIDatum::~VideoReaderIDatum() {
  this->impl->stop_requested = true;
  if (this->impl->thread.joinable()) {
    this->impl->thread.join();
  }
}
