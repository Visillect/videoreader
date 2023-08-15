#include "videoreader_pylon.hpp"

#if defined(_DEBUG)
#undef _DEBUG
#  include <pylon/TlFactory.h>
#  include <pylon/gige/BaslerGigECamera.h>
#  include <pylon/PylonIncludes.h>
#  include <pylon/ImageFormatConverter.h>
#define _DEBUG 1
#else
#  include <pylon/TlFactory.h>
#  include <pylon/gige/BaslerGigECamera.h>
#  include <pylon/PylonIncludes.h>
#  include <pylon/ImageFormatConverter.h>
#endif
#include <thread>
#include <deque>
#include <mutex>
#include <minimgapi/minimgapi.h>
#include "spinlock.hpp"


struct VideoReaderPylon::Impl {
  Pylon::CInstantCamera camera;
  std::deque<Pylon::CGrabResultPtr> read_queue;
  std::atomic<bool> stop_requested;
  SpinLock read_queue_lock;
  Pylon::CImageFormatConverter converter;
  std::thread thread;

  Impl() : stop_requested{false}
  {
    this->converter.OutputPixelFormat = Pylon::PixelType_RGB8packed;
    this->converter.OutputBitAlignment = Pylon::OutputBitAlignment_MsbAligned;
    this->camera.Attach(Pylon::CTlFactory::GetInstance().CreateFirstDevice());
    try {
      this->camera.Open();
    }
    catch (Pylon::GenericException const& e)
    {
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
    while (!this->stop_requested)
    {
      Pylon::CGrabResultPtr grabResult;
      if (!this->camera.RetrieveResult(500, grabResult, Pylon::TimeoutHandling_Return)) {
        //this->impl->running = false;
        continue;
      }
      if (!grabResult->GrabSucceeded())
        continue;
      {
        std::lock_guard<SpinLock> guard(this->read_queue_lock);
        if (this->read_queue.size() > 10)
        {
          // cleanup queue
          for (int i = 0; i < 8; ++i)
            this->read_queue.pop_front();
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

};

VideoReaderPylon::VideoReaderPylon(
  std::string const& url,
  std::vector<std::string> const& parameter_pairs) {
  Pylon::PylonInitialize();
  this->impl = std::unique_ptr<Impl>(new Impl{});
}

bool VideoReaderPylon::is_seekable() const {
  return false;
}

VideoReader::FrameUP VideoReaderPylon::next_frame(bool decode) {
  Pylon::CGrabResultPtr result = this->impl->pop_grab_result();
  if (!result.IsValid())
    return VideoReader::FrameUP();
  FrameUP ret(new Frame());
  MinImg &img = ret->image;
  NewMinImagePrototype(&img, result->GetWidth(), result->GetHeight(), 3, TYP_UINT8, 0, AO_EMPTY);
  AllocMinImage(&img, 1);
  if (decode) {
    this->impl->converter.Convert(img.p_zero_line, img.stride * img.height, result);
  }
  ret->number = result->GetBlockID();
  ret->timestamp_s = result->GetTimeStamp() / 1000.0;
  return ret;
}

VideoReader::Frame::number_t VideoReaderPylon::size() const
{
  return 0;
}


VideoReaderPylon::~VideoReaderPylon() {
  this->impl->stop_requested = true;
  this->impl->thread.join();
  Pylon::PylonTerminate();
}
