#include "videoreader_pylon.hpp"

#include <thread>
#include <atomic>
#include <deque>
#include <mutex>
#include <cstring>
#include <stdexcept>  // std::runtime_error
#include <minimgapi/minimgapi.h>
#include <GxIAPI.h>
#include "videoreader_galaxy.hpp"
#include "spinlock.hpp"
#include <unordered_map>
#include <algorithm>  // std::transform


static std::string get_error_string(GX_STATUS emErrorStatus) {
  size_t size{};
  if(GXGetLastError(&emErrorStatus, NULL, &size) == GX_STATUS_SUCCESS) {
    std::string ret(size, '\0');
    if (GXGetLastError(&emErrorStatus, const_cast<char *>(ret.data()), &size)
        != GX_STATUS_SUCCESS) {
      ret = "<Error when calling GXGetLastError>";
    }
    return ret;
  }
  else {
    return "<Error when calling GXGetLastError>";
  }
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), (int(*)(int)) std::tolower );
    return s;
}

#define GALAXY_CHECK(cb) do { \
  auto const status = cb; \
  if (status != GX_STATUS_SUCCESS) { \
    throw std::runtime_error(get_error_string(status)); \
  } \
} while(false)

static std::unordered_map<std::string, GX_FEATURE_ID> const INT_FEATURES {
  {"device_link_selector", GX_INT_DEVICE_LINK_SELECTOR},
  {"device_link_throughput_limit", GX_INT_DEVICE_LINK_THROUGHPUT_LIMIT},
  {"device_link_current_throughput", GX_INT_DEVICE_LINK_CURRENT_THROUGHPUT},
  {"timestamp_tick_frequency", GX_INT_TIMESTAMP_TICK_FREQUENCY},
  {"timestamp_latch_value", GX_INT_TIMESTAMP_LATCH_VALUE},
  {"revision", GX_INT_REVISION},
  {"versions_supported", GX_INT_VERSIONS_SUPPORTED},
  {"version_used", GX_INT_VERSION_USED},
  {"temperature_detection_status", GX_INT_TEMPERATURE_DETECTION_STATUS},
  {"fan_speed", GX_INT_FAN_SPEED},
  {"air_change_detection_status", GX_INT_AIR_CHANGE_DETECTION_STATUS},
  {"air_tightness_detection_status", GX_INT_AIR_TIGHTNESS_DETECTION_STATUS},
  {"sensor_width", GX_INT_SENSOR_WIDTH},
  {"sensor_height", GX_INT_SENSOR_HEIGHT},
  {"width_max", GX_INT_WIDTH_MAX},
  {"height_max", GX_INT_HEIGHT_MAX},
  {"offset_x", GX_INT_OFFSET_X},
  {"offset_y", GX_INT_OFFSET_Y},
  {"width", GX_INT_WIDTH},
  {"height", GX_INT_HEIGHT},
  {"binning_horizontal", GX_INT_BINNING_HORIZONTAL},
  {"binning_vertical", GX_INT_BINNING_VERTICAL},
  {"decimation_horizontal", GX_INT_DECIMATION_HORIZONTAL},
  {"decimation_vertical", GX_INT_DECIMATION_VERTICAL},
  {"center_width", GX_INT_CENTER_WIDTH},
  {"center_height", GX_INT_CENTER_HEIGHT},
  {"decimation_linenumber", GX_INT_DECIMATION_LINENUMBER},
  {"sensor_decimation_horizontal", GX_INT_SENSOR_DECIMATION_HORIZONTAL},
  {"sensor_decimation_vertical", GX_INT_SENSOR_DECIMATION_VERTICAL},
  {"current_sensor_width", GX_INT_CURRENT_SENSOR_WIDTH},
  {"current_sensor_height", GX_INT_CURRENT_SENSOR_HEIGHT},
  {"current_sensor_offsetx", GX_INT_CURRENT_SENSOR_OFFSETX},
  {"current_sensor_offsety", GX_INT_CURRENT_SENSOR_OFFSETY},
  {"current_sensor_widthmax", GX_INT_CURRENT_SENSOR_WIDTHMAX},
  {"current_sensor_heightmax", GX_INT_CURRENT_SENSOR_HEIGHTMAX},
  {"payload_size", GX_INT_PAYLOAD_SIZE},
  {"estimated_bandwidth", GX_INT_ESTIMATED_BANDWIDTH},
  {"gev_heartbeat_timeout", GX_INT_GEV_HEARTBEAT_TIMEOUT},
  {"gev_packetsize", GX_INT_GEV_PACKETSIZE},
  {"gev_packetdelay", GX_INT_GEV_PACKETDELAY},
  {"gev_link_speed", GX_INT_GEV_LINK_SPEED},
  {"acquisition_speed_level", GX_INT_ACQUISITION_SPEED_LEVEL},
  {"acquisition_frame_count", GX_INT_ACQUISITION_FRAME_COUNT},
  {"transfer_block_count", GX_INT_TRANSFER_BLOCK_COUNT},
  {"acquisition_burst_frame_count", GX_INT_ACQUISITION_BURST_FRAME_COUNT},
  {"line_status_all", GX_INT_LINE_STATUS_ALL},
  {"line_range", GX_INT_LINE_RANGE},
  {"line_delay", GX_INT_LINE_DELAY},
  {"line_filter_raising_edge", GX_INT_LINE_FILTER_RAISING_EDGE},
  {"line_filter_falling_edge", GX_INT_LINE_FILTER_FALLING_EDGE},
  {"digital_shift", GX_INT_DIGITAL_SHIFT},
  {"blacklevel_calib_value", GX_INT_BLACKLEVEL_CALIB_VALUE},
  {"adc_level", GX_INT_ADC_LEVEL},
  {"h_blanking", GX_INT_H_BLANKING},
  {"v_blanking", GX_INT_V_BLANKING},
  {"gray_value", GX_INT_GRAY_VALUE},
  {"aaroi_offsetx", GX_INT_AAROI_OFFSETX},
  {"aaroi_offsety", GX_INT_AAROI_OFFSETY},
  {"aaroi_width", GX_INT_AAROI_WIDTH},
  {"aaroi_height", GX_INT_AAROI_HEIGHT},
  {"contrast_param", GX_INT_CONTRAST_PARAM},
  {"color_correction_param", GX_INT_COLOR_CORRECTION_PARAM},
  {"awbroi_offsetx", GX_INT_AWBROI_OFFSETX},
  {"awbroi_offsety", GX_INT_AWBROI_OFFSETY},
  {"awbroi_width", GX_INT_AWBROI_WIDTH},
  {"awbroi_height", GX_INT_AWBROI_HEIGHT},
  {"static_defect_correction_finish", GX_INT_STATIC_DEFECT_CORRECTION_FINISH},
  {"ffc_expected_gray", GX_INT_FFC_EXPECTED_GRAY},
  {"ffc_coefficients_size", GX_INT_FFC_COEFFICIENTS_SIZE},
  {"static_defect_correction_calib_status", GX_INT_STATIC_DEFECT_CORRECTION_CALIB_STATUS},
  {"ffc_factory_status", GX_INT_FFC_FACTORY_STATUS},
  {"dsnu_factory_status", GX_INT_DSNU_FACTORY_STATUS},
  {"prnu_factory_status", GX_INT_PRNU_FACTORY_STATUS},
  {"data_field_value_all_used_status", GX_INT_DATA_FIELD_VALUE_ALL_USED_STATUS},
  {"event_exposureend", GX_INT_EVENT_EXPOSUREEND},
  {"event_exposureend_timestamp", GX_INT_EVENT_EXPOSUREEND_TIMESTAMP},
  {"event_exposureend_frameid", GX_INT_EVENT_EXPOSUREEND_FRAMEID},
  {"event_block_discard", GX_INT_EVENT_BLOCK_DISCARD},
  {"event_block_discard_timestamp", GX_INT_EVENT_BLOCK_DISCARD_TIMESTAMP},
  {"event_overrun", GX_INT_EVENT_OVERRUN},
  {"event_overrun_timestamp", GX_INT_EVENT_OVERRUN_TIMESTAMP},
  {"event_framestart_overtrigger", GX_INT_EVENT_FRAMESTART_OVERTRIGGER},
  {"event_framestart_overtrigger_timestamp", GX_INT_EVENT_FRAMESTART_OVERTRIGGER_TIMESTAMP},
  {"event_block_not_empty", GX_INT_EVENT_BLOCK_NOT_EMPTY},
  {"event_block_not_empty_timestamp", GX_INT_EVENT_BLOCK_NOT_EMPTY_TIMESTAMP},
  {"event_internal_error", GX_INT_EVENT_INTERNAL_ERROR},
  {"event_internal_error_timestamp", GX_INT_EVENT_INTERNAL_ERROR_TIMESTAMP},
  {"event_frameburststart_overtrigger", GX_INT_EVENT_FRAMEBURSTSTART_OVERTRIGGER},
  {"event_frameburststart_overtrigger_frameid", GX_INT_EVENT_FRAMEBURSTSTART_OVERTRIGGER_FRAMEID},
  {"event_frameburststart_overtrigger_timestamp", GX_INT_EVENT_FRAMEBURSTSTART_OVERTRIGGER_TIMESTAMP},
  {"event_framestart_wait", GX_INT_EVENT_FRAMESTART_WAIT},
  {"event_framestart_wait_timestamp", GX_INT_EVENT_FRAMESTART_WAIT_TIMESTAMP},
  {"event_frameburststart_wait", GX_INT_EVENT_FRAMEBURSTSTART_WAIT},
  {"event_frameburststart_wait_timestamp", GX_INT_EVENT_FRAMEBURSTSTART_WAIT_TIMESTAMP},
  {"event_block_discard_frameid", GX_INT_EVENT_BLOCK_DISCARD_FRAMEID},
  {"event_framestart_overtrigger_frameid", GX_INT_EVENT_FRAMESTART_OVERTRIGGER_FRAMEID},
  {"event_block_not_empty_frameid", GX_INT_EVENT_BLOCK_NOT_EMPTY_FRAMEID},
  {"event_framestart_wait_frameid", GX_INT_EVENT_FRAMESTART_WAIT_FRAMEID},
  {"event_frameburststart_wait_frameid", GX_INT_EVENT_FRAMEBURSTSTART_WAIT_FRAMEID},
  {"lut_index", GX_INT_LUT_INDEX},
  {"lut_value", GX_INT_LUT_VALUE},
  {"lut_factory_status", GX_INT_LUT_FACTORY_STATUS},
  {"saturation", GX_INT_SATURATION},
  {"counter_duration", GX_INT_COUNTER_DURATION},
  {"counter_value", GX_INT_COUNTER_VALUE},
  {"hdr_target_long_value", GX_INT_HDR_TARGET_LONG_VALUE},
  {"hdr_target_short_value", GX_INT_HDR_TARGET_SHORT_VALUE},
  {"hdr_target_main_value", GX_INT_HDR_TARGET_MAIN_VALUE},
  {"mgc_selector", GX_INT_MGC_SELECTOR},
  {"frame_buffer_count", GX_INT_FRAME_BUFFER_COUNT},
  {"serialport_data_bits", GX_INT_SERIALPORT_DATA_BITS},
  {"transmit_queue_max_character_count", GX_INT_TRANSMIT_QUEUE_MAX_CHARACTER_COUNT},
  {"transmit_queue_current_character_count", GX_INT_TRANSMIT_QUEUE_CURRENT_CHARACTER_COUNT},
  {"receive_queue_max_character_count", GX_INT_RECEIVE_QUEUE_MAX_CHARACTER_COUNT},
  {"receive_queue_current_character_count", GX_INT_RECEIVE_QUEUE_CURRENT_CHARACTER_COUNT},
  {"receive_framing_error_count", GX_INT_RECEIVE_FRAMING_ERROR_COUNT},
  {"receive_parity_error_count", GX_INT_RECEIVE_PARITY_ERROR_COUNT},
  {"serialport_data_length", GX_INT_SERIALPORT_DATA_LENGTH},
  {"serial_port_detection_status", GX_INT_SERIAL_PORT_DETECTION_STATUS},
  {"image1_stream_id", GX_INT_IMAGE1_STREAM_ID},
  {"cxp_connection_test_error_count", GX_INT_CXP_CONNECTION_TEST_ERROR_COUNT},
  {"cxp_connection_test_packet_rx_count", GX_INT_CXP_CONNECTION_TEST_PACKET_RX_COUNT},
  {"cxp_connection_test_packet_tx_count", GX_INT_CXP_CONNECTION_TEST_PACKET_TX_COUNT},
  {"sequencer_set_selector", GX_INT_SEQUENCER_SET_SELECTOR},
  {"sequencer_set_count", GX_INT_SEQUENCER_SET_COUNT},
  {"sequencer_set_active", GX_INT_SEQUENCER_SET_ACTIVE},
  {"sequencer_path_selector", GX_INT_SEQUENCER_PATH_SELECTOR},
  {"sequencer_set_next", GX_INT_SEQUENCER_SET_NEXT},
  {"encoder_value", GX_INT_ENCODER_VALUE}
};

static std::unordered_map<std::string, GX_FEATURE_ID> const FLOAT_FEATURES {
  {"device_temperature", GX_FLOAT_DEVICE_TEMPERATURE},
  {"tec_target_temperature", GX_FLOAT_TEC_TARGET_TEMPERATURE},
  {"device_humidity", GX_FLOAT_DEVICE_HUMIDITY},
  {"device_pressure", GX_FLOAT_DEVICE_PRESSURE},
  {"exposure_time", GX_FLOAT_EXPOSURE_TIME},
  {"trigger_filter_raising", GX_FLOAT_TRIGGER_FILTER_RAISING},
  {"trigger_filter_falling", GX_FLOAT_TRIGGER_FILTER_FALLING},
  {"trigger_delay", GX_FLOAT_TRIGGER_DELAY},
  {"acquisition_frame_rate", GX_FLOAT_ACQUISITION_FRAME_RATE},
  {"current_acquisition_frame_rate", GX_FLOAT_CURRENT_ACQUISITION_FRAME_RATE},
  {"exposure_delay", GX_FLOAT_EXPOSURE_DELAY},
  {"exposure_overlap_time_max", GX_FLOAT_EXPOSURE_OVERLAP_TIME_MAX},
  {"pulse_width", GX_FLOAT_PULSE_WIDTH},
  {"balance_ratio", GX_FLOAT_BALANCE_RATIO},
  {"gain", GX_FLOAT_GAIN},
  {"blacklevel", GX_FLOAT_BLACKLEVEL},
  {"gamma", GX_FLOAT_GAMMA},
  {"pga_gain", GX_FLOAT_PGA_GAIN},
  {"auto_gain_min", GX_FLOAT_AUTO_GAIN_MIN},
  {"auto_gain_max", GX_FLOAT_AUTO_GAIN_MAX},
  {"auto_exposure_time_min", GX_FLOAT_AUTO_EXPOSURE_TIME_MIN},
  {"auto_exposure_time_max", GX_FLOAT_AUTO_EXPOSURE_TIME_MAX},
  {"gamma_param", GX_FLOAT_GAMMA_PARAM},
  {"sharpness", GX_FLOAT_SHARPNESS},
  {"noise_reduction", GX_FLOAT_NOISE_REDUCTION},
  {"color_transformation_value", GX_FLOAT_COLOR_TRANSFORMATION_VALUE},
  {"timer_duration", GX_FLOAT_TIMER_DURATION},
  {"timer_delay", GX_FLOAT_TIMER_DELAY},
  {"mgc_exposure_time", GX_FLOAT_MGC_EXPOSURE_TIME},
  {"mgc_gain", GX_FLOAT_MGC_GAIN},
  {"contrast", GX_FLOAT_CONTRAST},
  {"imu_room_temperature", GX_FLOAT_IMU_ROOM_TEMPERATURE},
};

struct VideoReaderGalaxy::Impl {
  GX_DEV_HANDLE handle;
  std::deque<FrameUP> read_queue;
  std::atomic<bool> stop_requested;
  SpinLock read_queue_lock;
  std::thread thread;
  std::exception_ptr exception;


  Impl(GX_DEV_HANDLE handle, std::vector<std::string> const& parameter_pairs) : handle{handle}, stop_requested{false} {
    GALAXY_CHECK(GXSetEnum(handle, GX_ENUM_EXPOSURE_AUTO, 1));
    GALAXY_CHECK(GXSetEnum(handle, GX_ENUM_GAIN_AUTO, 1));
    GALAXY_CHECK(GXSetInt(handle, GX_INT_BINNING_HORIZONTAL, 2));
    GALAXY_CHECK(GXSetInt(handle, GX_INT_BINNING_VERTICAL, 2));
    for (std::vector<std::string>::const_iterator it = parameter_pairs.begin();
        it != parameter_pairs.end();
        ++it) {
      std::string const key = to_lower(*it);
      std::string const& value = *++it;
      if (auto pair = INT_FEATURES.find(key); pair != INT_FEATURES.end()) {
        int64_t int_value = std::stoll(value);
        GALAXY_CHECK(GXSetInt(handle, pair->second, int_value));
      }
      if (auto pair = FLOAT_FEATURES.find(key); pair != FLOAT_FEATURES.end()) {
        double float_value = std::stod(value);
        GALAXY_CHECK(GXSetFloat(handle, pair->second, float_value));
      }
    }

    this->thread = std::thread(&VideoReaderGalaxy::Impl::read, this);
  }
  ~Impl() {
    GXCloseDevice(this->handle);
  }
  void read() {
    // https://github.com/JerryAuas/RmEverTo2022/blob/6ca1c2f6d94934d8a1296f3ad45b9cc327b7fa9b/Sources/armordetect1.cpp#L205
    try {
      uint32_t const TIMEOUT_MS = 250;
      GXStreamOn(this->handle);
      PGX_FRAME_BUFFER pFrameBuffer[5]{};
      uint32_t nFrameCount{};
      uint32_t timeoutHit{};
      while (!this->stop_requested) {
        auto const status = GXDQAllBufs(this->handle, pFrameBuffer, sizeof(pFrameBuffer) / sizeof(*pFrameBuffer), &nFrameCount, TIMEOUT_MS);
        if (status != GX_STATUS_SUCCESS) {
          if (status == GX_STATUS_TIMEOUT) {
            ++timeoutHit;
            if (timeoutHit > 3000 / TIMEOUT_MS) {
              throw std::runtime_error("no galaxy data for 3 seconds");
            }
            continue;
          }
          else {
            throw std::runtime_error(get_error_string(status).c_str());
          }
        }
        timeoutHit = 0;
        if (pFrameBuffer[nFrameCount - 1]->nStatus != GX_FRAME_STATUS_SUCCESS) {
          GXQAllBufs(this->handle);
          continue;
        }
        for (uint32_t idx = 0; idx < nFrameCount; ++idx) {
          FrameUP frame{new Frame()};
          PGX_FRAME_BUFFER buffer = pFrameBuffer[idx];
          frame->number = buffer->nFrameID - 1; // -1 as Galaxy starts with 1
          frame->timestamp_s = static_cast<double>(buffer->nTimestamp) / 1e8;  // bad cast, sorry
          MinImg &img = frame->image;
          if (NewMinImagePrototype(&img, buffer->nWidth, buffer->nHeight, 1, TYP_UINT8, 0, AO_EMPTY) != 0) {
            throw std::runtime_error("NewMinImagePrototype err");
          }
          if (AllocMinImage(&img, 4) != 0) {
            throw std::runtime_error("AllocMinImage err");
          }
          std::memcpy(img.p_zero_line, buffer->pImgBuf, buffer->nWidth * buffer->nHeight);
          {
            std::lock_guard<SpinLock> guard(this->read_queue_lock);
            if (this->read_queue.size() > 10)
            {
              // cleanup queue
              for (int i = 0; i < 8; ++i)
                this->read_queue.pop_front();
            }
            this->read_queue.emplace_back(std::move(frame));
          }
        }
        GXQAllBufs(this->handle);
      }
    }
    catch (...) {
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

VideoReaderGalaxy::VideoReaderGalaxy(
  std::string const& url,
  std::vector<std::string> const& parameter_pairs) {

  auto const res = GXInitLib();
  if (res != GX_STATUS_SUCCESS) {
      throw std::runtime_error("GXInitLib was't successful");
  }

  auto const info = url.substr(9);
  GX_DEV_HANDLE handle{};

  static GX_OPEN_MODE_CMD modes[] = {
      GX_OPEN_IP,
      GX_OPEN_SN,
      GX_OPEN_MAC,
      GX_OPEN_INDEX,
      GX_OPEN_USERID
  };


  GX_OPEN_PARAM param{const_cast<char*>(info.c_str()), GX_OPEN_SN, GX_ACCESS_EXCLUSIVE};
  for (int mode_idx = 0; mode_idx < sizeof(modes) / sizeof(*modes); ++mode_idx) {
    param.openMode = modes[mode_idx];
    auto const ret = GXOpenDevice(&param, &handle);
    if (ret == GX_STATUS_SUCCESS) {
      this->impl = std::unique_ptr<Impl>(new Impl{handle, parameter_pairs});
      return;
    }
  }
  throw std::runtime_error("Galaxy device `" + info + "` not found");
}

bool VideoReaderGalaxy::is_seekable() const {
  return false;
}

VideoReader::FrameUP VideoReaderGalaxy::next_frame(bool decode) {
  return this->impl->pop_grab_result();
}

VideoReader::Frame::number_t VideoReaderGalaxy::size() const
{
  return 0;
}


VideoReaderGalaxy::~VideoReaderGalaxy() {
  this->impl->stop_requested = true;
  this->impl->thread.join();
  GXCloseLib();
}
