#include "videoreader_pylon.hpp"

#if defined(_DEBUG)
#undef _DEBUG
#include <pylon/ImageFormatConverter.h>
#include <pylon/PylonIncludes.h>
#include <pylon/TlFactory.h>
#include <pylon/gige/BaslerGigECamera.h>
#define _DEBUG 1
#else
#include <pylon/ImageFormatConverter.h>
#include <pylon/PylonIncludes.h>
#include <pylon/TlFactory.h>
#include <pylon/gige/BaslerGigECamera.h>
#endif
#include "spinlock.hpp"
#include <deque>
#include <mutex>
#include <thread>

struct VideoReaderPylon::Impl {
  Pylon::CInstantCamera camera;
  std::deque<Pylon::CGrabResultPtr> read_queue;
  std::atomic<bool> stop_requested;
  SpinLock read_queue_lock;
  Pylon::CImageFormatConverter converter;
  std::thread thread;
  AllocateCallback allocate_callback;
  DeallocateCallback deallocate_callback;
  void* userdata;

  Impl(
      AllocateCallback allocate_callback,
      DeallocateCallback deallocate_callback,
      void* userdata) :
      stop_requested{false},
      allocate_callback{allocate_callback},
      deallocate_callback{deallocate_callback},
      userdata{userdata} {
    this->converter.OutputPixelFormat = Pylon::PixelType_RGB8packed;
    this->converter.OutputBitAlignment = Pylon::OutputBitAlignment_MsbAligned;
    this->camera.Attach(Pylon::CTlFactory::GetInstance().CreateFirstDevice());
    try {
      this->camera.Open();
    } catch (Pylon::GenericException const& e) {
      throw std::runtime_error(e.what());
    }
    this->thread = std::thread(&VideoReaderPylon::Impl::read, this);
  }

  Pylon::CGrabResultPtr pop_grab_result() {
    Pylon::CGrabResultPtr ret;
    while (true) {
      std::lock_guard<SpinLock> guard(this->read_queue_lock);
      if (this->read_queue.empty()) {
        this->read_queue_lock.unlock();
        std::this_thread::yield();
        continue;
      }
      ret = this->read_queue.front();
      this->read_queue.pop_front();
      return ret;
    }
  }

  void read() {
    this->camera.StartGrabbing();
    while (!this->stop_requested) {
      Pylon::CGrabResultPtr grabResult;
      if (!this->camera.RetrieveResult(
              500, grabResult, Pylon::TimeoutHandling_Return)) {
        //this->impl->running = false;
        continue;
      }
      if (!grabResult->GrabSucceeded())
        continue;
      {
        std::lock_guard<SpinLock> guard(this->read_queue_lock);
        if (this->read_queue.size() > 10) {
          // cleanup queue
          for (int i = 0; i < 8; ++i) {
            this->read_queue.pop_front();
          }
        }
        this->read_queue.emplace_back(grabResult);
      }
    }
    {
      std::lock_guard<SpinLock> guard(this->read_queue_lock);
      this->read_queue.emplace_back();
    }
    this->camera.StopGrabbing();
    this->camera.Close();
    //this->impl->camera.DetachDevice();
    this->camera.DestroyDevice();
  }

  VideoReader::FrameUP next_frame(bool decode) {
    Pylon::CGrabResultPtr result = this->pop_grab_result();
    if (!result.IsValid()) {
      return nullptr;
    }
    int32_t const width = static_cast<int32_t>(result->GetWidth());
    int32_t const height = static_cast<int32_t>(result->GetHeight());
    int32_t const alignment = 16;
    int32_t const preferred_stride =
        (width * 3 + alignment - 1) & ~(alignment - 1);

    Frame::number_t const number = result->GetBlockID();
    Frame::timestamp_s_t const timestamp_s = result->GetTimeStamp() / 1000.0;

    FrameUP frame(new Frame(
        this->deallocate_callback,
        this->userdata,
        {
            height,  // height
            width,  // width
            3,  // channels
            SCALAR_TYPE::U8,  // scalar_type;
            preferred_stride,  // stride
            nullptr,  // data
            nullptr  // user_data
        },
        number,
        timestamp_s));
    if (decode) {
      VideoReader::VRImage* img = &frame->image;
      (*this->allocate_callback)(img, this->userdata);
      if (!img->data) {
        throw std::runtime_error("Failed to allocate image for pylon");
      }
      this->converter.Convert(img->data, img->stride * img->height, result);
    }
    return frame;
  }
};

VideoReaderPylon::VideoReaderPylon(
    std::string const& url,
    std::vector<std::string> const& parameter_pairs,
    std::vector<std::string> const& extras,
    AllocateCallback allocate_cb,
    DeallocateCallback deallocate_cb,
    VideoReader::LogCallback log_callback,
    void* userdata) {
  if (!extras.empty()) {
    throw std::runtime_error("extras not supported in pylon (yet)");
  }
  Pylon::PylonInitialize();
  this->impl =
      std::unique_ptr<Impl>(new Impl{allocate_cb, deallocate_cb, userdata});
}

bool VideoReaderPylon::is_seekable() const {
  return false;
}

VideoReader::FrameUP VideoReaderPylon::next_frame(bool decode) {
  return this->impl->next_frame(decode);
}

VideoReader::Frame::number_t VideoReaderPylon::size() const {
  return 0;
}

VideoReaderPylon::~VideoReaderPylon() {
  this->impl->stop_requested = true;
  this->impl->thread.join();
  Pylon::PylonTerminate();
}
