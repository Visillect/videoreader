#include "videoreader_galaxy.hpp"
#include "spinlock.hpp"
#include "thismsgpack.hpp"
#include <GxIAPI.h>
#include <algorithm>  // std::transform
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>  // std::runtime_error
#include <thread>
#include <unordered_map>

static std::string get_error_string(GX_STATUS emErrorStatus) {
  size_t size{};
  if (GXGetLastError(&emErrorStatus, NULL, &size) == GX_STATUS_SUCCESS) {
    std::string ret(size, '\0');
    if (GXGetLastError(&emErrorStatus, const_cast<char*>(ret.data()), &size) !=
        GX_STATUS_SUCCESS) {
      ret = "<Error when calling GXGetLastError>";
    }
    return ret;
  } else {
    return "<Error when calling GXGetLastError>";
  }
}

static std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), (int (*)(int))std::tolower);
  return s;
}

#define GALAXY_CHECK(cb) \
  do { \
    auto const status = cb; \
    if (status != GX_STATUS_SUCCESS) { \
      throw std::runtime_error(get_error_string(status)); \
    } \
  } while (false)

static std::unordered_map<std::string, GX_FEATURE_ID> const INT_FEATURES{
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
    {"static_defect_correction_calib_status",
     GX_INT_STATIC_DEFECT_CORRECTION_CALIB_STATUS},
    {"ffc_factory_status", GX_INT_FFC_FACTORY_STATUS},
    {"dsnu_factory_status", GX_INT_DSNU_FACTORY_STATUS},
    {"prnu_factory_status", GX_INT_PRNU_FACTORY_STATUS},
    {"data_field_value_all_used_status",
     GX_INT_DATA_FIELD_VALUE_ALL_USED_STATUS},
    {"event_exposureend", GX_INT_EVENT_EXPOSUREEND},
    {"event_exposureend_timestamp", GX_INT_EVENT_EXPOSUREEND_TIMESTAMP},
    {"event_exposureend_frameid", GX_INT_EVENT_EXPOSUREEND_FRAMEID},
    {"event_block_discard", GX_INT_EVENT_BLOCK_DISCARD},
    {"event_block_discard_timestamp", GX_INT_EVENT_BLOCK_DISCARD_TIMESTAMP},
    {"event_overrun", GX_INT_EVENT_OVERRUN},
    {"event_overrun_timestamp", GX_INT_EVENT_OVERRUN_TIMESTAMP},
    {"event_framestart_overtrigger", GX_INT_EVENT_FRAMESTART_OVERTRIGGER},
    {"event_framestart_overtrigger_timestamp",
     GX_INT_EVENT_FRAMESTART_OVERTRIGGER_TIMESTAMP},
    {"event_block_not_empty", GX_INT_EVENT_BLOCK_NOT_EMPTY},
    {"event_block_not_empty_timestamp", GX_INT_EVENT_BLOCK_NOT_EMPTY_TIMESTAMP},
    {"event_internal_error", GX_INT_EVENT_INTERNAL_ERROR},
    {"event_internal_error_timestamp", GX_INT_EVENT_INTERNAL_ERROR_TIMESTAMP},
    {"event_frameburststart_overtrigger",
     GX_INT_EVENT_FRAMEBURSTSTART_OVERTRIGGER},
    {"event_frameburststart_overtrigger_frameid",
     GX_INT_EVENT_FRAMEBURSTSTART_OVERTRIGGER_FRAMEID},
    {"event_frameburststart_overtrigger_timestamp",
     GX_INT_EVENT_FRAMEBURSTSTART_OVERTRIGGER_TIMESTAMP},
    {"event_framestart_wait", GX_INT_EVENT_FRAMESTART_WAIT},
    {"event_framestart_wait_timestamp", GX_INT_EVENT_FRAMESTART_WAIT_TIMESTAMP},
    {"event_frameburststart_wait", GX_INT_EVENT_FRAMEBURSTSTART_WAIT},
    {"event_frameburststart_wait_timestamp",
     GX_INT_EVENT_FRAMEBURSTSTART_WAIT_TIMESTAMP},
    {"event_block_discard_frameid", GX_INT_EVENT_BLOCK_DISCARD_FRAMEID},
    {"event_framestart_overtrigger_frameid",
     GX_INT_EVENT_FRAMESTART_OVERTRIGGER_FRAMEID},
    {"event_block_not_empty_frameid", GX_INT_EVENT_BLOCK_NOT_EMPTY_FRAMEID},
    {"event_framestart_wait_frameid", GX_INT_EVENT_FRAMESTART_WAIT_FRAMEID},
    {"event_frameburststart_wait_frameid",
     GX_INT_EVENT_FRAMEBURSTSTART_WAIT_FRAMEID},
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
    {"transmit_queue_max_character_count",
     GX_INT_TRANSMIT_QUEUE_MAX_CHARACTER_COUNT},
    {"transmit_queue_current_character_count",
     GX_INT_TRANSMIT_QUEUE_CURRENT_CHARACTER_COUNT},
    {"receive_queue_max_character_count",
     GX_INT_RECEIVE_QUEUE_MAX_CHARACTER_COUNT},
    {"receive_queue_current_character_count",
     GX_INT_RECEIVE_QUEUE_CURRENT_CHARACTER_COUNT},
    {"receive_framing_error_count", GX_INT_RECEIVE_FRAMING_ERROR_COUNT},
    {"receive_parity_error_count", GX_INT_RECEIVE_PARITY_ERROR_COUNT},
    {"serialport_data_length", GX_INT_SERIALPORT_DATA_LENGTH},
    {"serial_port_detection_status", GX_INT_SERIAL_PORT_DETECTION_STATUS},
    {"image1_stream_id", GX_INT_IMAGE1_STREAM_ID},
    {"cxp_connection_test_error_count", GX_INT_CXP_CONNECTION_TEST_ERROR_COUNT},
    {"cxp_connection_test_packet_rx_count",
     GX_INT_CXP_CONNECTION_TEST_PACKET_RX_COUNT},
    {"cxp_connection_test_packet_tx_count",
     GX_INT_CXP_CONNECTION_TEST_PACKET_TX_COUNT},
    {"sequencer_set_selector", GX_INT_SEQUENCER_SET_SELECTOR},
    {"sequencer_set_count", GX_INT_SEQUENCER_SET_COUNT},
    {"sequencer_set_active", GX_INT_SEQUENCER_SET_ACTIVE},
    {"sequencer_path_selector", GX_INT_SEQUENCER_PATH_SELECTOR},
    {"sequencer_set_next", GX_INT_SEQUENCER_SET_NEXT},
    {"encoder_value", GX_INT_ENCODER_VALUE}};

static std::unordered_map<std::string, GX_FEATURE_ID> const FLOAT_FEATURES{
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

static std::unordered_map<std::string, GX_FEATURE_ID> const ENUM_FEATURES{
    {"device_link_throughput_limit_mode",
     GX_ENUM_DEVICE_LINK_THROUGHPUT_LIMIT_MODE},
    {"device_temperature_selector", GX_ENUM_DEVICE_TEMPERATURE_SELECTOR},
    {"lowpower_mode", GX_ENUM_LOWPOWER_MODE},
    {"close_ccd", GX_ENUM_CLOSE_CCD},
    {"pixel_size", GX_ENUM_PIXEL_SIZE},
    {"pixel_color_filter", GX_ENUM_PIXEL_COLOR_FILTER},
    {"pixel_format", GX_ENUM_PIXEL_FORMAT},
    {"test_pattern", GX_ENUM_TEST_PATTERN},
    {"test_pattern_generator_selector",
     GX_ENUM_TEST_PATTERN_GENERATOR_SELECTOR},
    {"region_send_mode", GX_ENUM_REGION_SEND_MODE},
    {"region_mode", GX_ENUM_REGION_MODE},
    {"rregion_selector", GX_ENUM_RREGION_SELECTOR},
    {"binning_horizontal_mode", GX_ENUM_BINNING_HORIZONTAL_MODE},
    {"binning_vertical_mode", GX_ENUM_BINNING_VERTICAL_MODE},
    {"sensor_shutter_mode", GX_ENUM_SENSOR_SHUTTER_MODE},
    {"sensor_selector", GX_ENUM_SENSOR_SELECTOR},
    {"sensor_bit_depth", GX_ENUM_SENSOR_BIT_DEPTH},
    {"device_tap_geometry", GX_ENUM_DEVICE_TAP_GEOMETRY},
    {"acquisition_mode", GX_ENUM_ACQUISITION_MODE},
    {"trigger_mode", GX_ENUM_TRIGGER_MODE},
    {"trigger_activation", GX_ENUM_TRIGGER_ACTIVATION},
    {"trigger_switch", GX_ENUM_TRIGGER_SWITCH},
    {"exposure_auto", GX_ENUM_EXPOSURE_AUTO},
    {"trigger_source", GX_ENUM_TRIGGER_SOURCE},
    {"exposure_mode", GX_ENUM_EXPOSURE_MODE},
    {"trigger_selector", GX_ENUM_TRIGGER_SELECTOR},
    {"transfer_control_mode", GX_ENUM_TRANSFER_CONTROL_MODE},
    {"transfer_operation_mode", GX_ENUM_TRANSFER_OPERATION_MODE},
    {"acquisition_frame_rate_mode", GX_ENUM_ACQUISITION_FRAME_RATE_MODE},
    {"fixed_pattern_noise_correct_mode",
     GX_ENUM_FIXED_PATTERN_NOISE_CORRECT_MODE},
    {"acquisition_status_selector", GX_ENUM_ACQUISITION_STATUS_SELECTOR},
    {"exposure_time_mode", GX_ENUM_EXPOSURE_TIME_MODE},
    {"acquisition_burst_mode", GX_ENUM_ACQUISITION_BURST_MODE},
    {"overlap_mode", GX_ENUM_OVERLAP_MODE},
    {"multisource_selector", GX_ENUM_MULTISOURCE_SELECTOR},
    {"user_output_selector", GX_ENUM_USER_OUTPUT_SELECTOR},
    {"user_output_mode", GX_ENUM_USER_OUTPUT_MODE},
    {"strobe_switch", GX_ENUM_STROBE_SWITCH},
    {"line_selector", GX_ENUM_LINE_SELECTOR},
    {"line_mode", GX_ENUM_LINE_MODE},
    {"line_source", GX_ENUM_LINE_SOURCE},
    {"gain_auto", GX_ENUM_GAIN_AUTO},
    {"gain_selector", GX_ENUM_GAIN_SELECTOR},
    {"blacklevel_auto", GX_ENUM_BLACKLEVEL_AUTO},
    {"blacklevel_selector", GX_ENUM_BLACKLEVEL_SELECTOR},
    {"balance_white_auto", GX_ENUM_BALANCE_WHITE_AUTO},
    {"balance_ratio_selector", GX_ENUM_BALANCE_RATIO_SELECTOR},
    {"color_correct", GX_ENUM_COLOR_CORRECT},
    {"dead_pixel_correct", GX_ENUM_DEAD_PIXEL_CORRECT},
    {"gamma_mode", GX_ENUM_GAMMA_MODE},
    {"light_source_preset", GX_ENUM_LIGHT_SOURCE_PRESET},
    {"aa_light_environment", GX_ENUM_AA_LIGHT_ENVIRONMENT},
    {"image_gray_raise_switch", GX_ENUM_IMAGE_GRAY_RAISE_SWITCH},
    {"awb_lamp_house", GX_ENUM_AWB_LAMP_HOUSE},
    {"sharpness_mode", GX_ENUM_SHARPNESS_MODE},
    {"user_data_filed_selector", GX_ENUM_USER_DATA_FILED_SELECTOR},
    {"flat_field_correction", GX_ENUM_FLAT_FIELD_CORRECTION},
    {"noise_reduction_mode", GX_ENUM_NOISE_REDUCTION_MODE},
    {"static_defect_correction", GX_ENUM_STATIC_DEFECT_CORRECTION},
    {"2d_noise_reduction_mode", GX_ENUM_2D_NOISE_REDUCTION_MODE},
    {"3d_noise_reduction_mode", GX_ENUM_3D_NOISE_REDUCTION_MODE},
    {"shading_correction_mode", GX_ENUM_SHADING_CORRECTION_MODE},
    {"ffc_generate_status", GX_ENUM_FFC_GENERATE_STATUS},
    {"ffc_expected_gray_value_enable", GX_ENUM_FFC_EXPECTED_GRAY_VALUE_ENABLE},
    {"dsnu_selector", GX_ENUM_DSNU_SELECTOR},
    {"dsnu_generate_status", GX_ENUM_DSNU_GENERATE_STATUS},
    {"prnu_selector", GX_ENUM_PRNU_SELECTOR},
    {"prnu_generate_status", GX_ENUM_PRNU_GENERATE_STATUS},
    {"ffc_coefficient", GX_ENUM_FFC_COEFFICIENT},
    {"user_set_selector", GX_ENUM_USER_SET_SELECTOR},
    {"user_set_default", GX_ENUM_USER_SET_DEFAULT},
    {"event_selector", GX_ENUM_EVENT_SELECTOR},
    {"event_notification", GX_ENUM_EVENT_NOTIFICATION},
    {"event_simple_mode", GX_ENUM_EVENT_SIMPLE_MODE},
    {"lut_selector", GX_ENUM_LUT_SELECTOR},
    {"chunk_selector", GX_ENUM_CHUNK_SELECTOR},
    {"color_transformation_mode", GX_ENUM_COLOR_TRANSFORMATION_MODE},
    {"color_transformation_value_selector",
     GX_ENUM_COLOR_TRANSFORMATION_VALUE_SELECTOR},
    {"saturation_mode", GX_ENUM_SATURATION_MODE},
    {"timer_selector", GX_ENUM_TIMER_SELECTOR},
    {"timer_trigger_source", GX_ENUM_TIMER_TRIGGER_SOURCE},
    {"counter_selector", GX_ENUM_COUNTER_SELECTOR},
    {"counter_event_source", GX_ENUM_COUNTER_EVENT_SOURCE},
    {"counter_reset_source", GX_ENUM_COUNTER_RESET_SOURCE},
    {"counter_reset_activation", GX_ENUM_COUNTER_RESET_ACTIVATION},
    {"counter_trigger_source", GX_ENUM_COUNTER_TRIGGER_SOURCE},
    {"timer_trigger_activation", GX_ENUM_TIMER_TRIGGER_ACTIVATION},
    {"remove_parameter_limit", GX_ENUM_REMOVE_PARAMETER_LIMIT},
    {"hdr_mode", GX_ENUM_HDR_MODE},
    {"mgc_mode", GX_ENUM_MGC_MODE},
    {"imu_config_acc_range", GX_ENUM_IMU_CONFIG_ACC_RANGE},
    {"imu_config_acc_odr_low_pass_filter_switch",
     GX_ENUM_IMU_CONFIG_ACC_ODR_LOW_PASS_FILTER_SWITCH},
    {"imu_config_acc_odr", GX_ENUM_IMU_CONFIG_ACC_ODR},
    {"imu_config_acc_odr_low_pass_filter_frequency",
     GX_ENUM_IMU_CONFIG_ACC_ODR_LOW_PASS_FILTER_FREQUENCY},
    {"imu_config_gyro_xrange", GX_ENUM_IMU_CONFIG_GYRO_XRANGE},
    {"imu_config_gyro_yrange", GX_ENUM_IMU_CONFIG_GYRO_YRANGE},
    {"imu_config_gyro_zrange", GX_ENUM_IMU_CONFIG_GYRO_ZRANGE},
    {"imu_config_gyro_odr_low_pass_filter_switch",
     GX_ENUM_IMU_CONFIG_GYRO_ODR_LOW_PASS_FILTER_SWITCH},
    {"imu_config_gyro_odr", GX_ENUM_IMU_CONFIG_GYRO_ODR},
    {"imu_config_gyro_odr_low_pass_filter_frequency",
     GX_ENUM_IMU_CONFIG_GYRO_ODR_LOW_PASS_FILTER_FREQUENCY},
    {"imu_temperature_odr", GX_ENUM_IMU_TEMPERATURE_ODR},
    {"serialport_selector", GX_ENUM_SERIALPORT_SELECTOR},
    {"serialport_source", GX_ENUM_SERIALPORT_SOURCE},
    {"serialport_baudrate", GX_ENUM_SERIALPORT_BAUDRATE},
    {"serialport_stop_bits", GX_ENUM_SERIALPORT_STOP_BITS},
    {"serialport_parity", GX_ENUM_SERIALPORT_PARITY},
    {"cxp_link_configuration", GX_ENUM_CXP_LINK_CONFIGURATION},
    {"cxp_link_configuration_preferred",
     GX_ENUM_CXP_LINK_CONFIGURATION_PREFERRED},
    {"cxp_link_configuration_status", GX_ENUM_CXP_LINK_CONFIGURATION_STATUS},
    {"cxp_connection_selector", GX_ENUM_CXP_CONNECTION_SELECTOR},
    {"cxp_connection_test_mode", GX_ENUM_CXP_CONNECTION_TEST_MODE},
    {"sequencer_mode", GX_ENUM_SEQUENCER_MODE},
    {"sequencer_configuration_mode", GX_ENUM_SEQUENCER_CONFIGURATION_MODE},
    {"sequencer_feature_selector", GX_ENUM_SEQUENCER_FEATURE_SELECTOR},
    {"sequencer_trigger_source", GX_ENUM_SEQUENCER_TRIGGER_SOURCE},
    {"encoder_selector", GX_ENUM_ENCODER_SELECTOR},
    {"encoder_direction", GX_ENUM_ENCODER_DIRECTION},
    {"encoder_sourcea", GX_ENUM_ENCODER_SOURCEA},
    {"encoder_sourceb", GX_ENUM_ENCODER_SOURCEB},
    {"encoder_mode", GX_ENUM_ENCODER_MODE},
    {"um_resend_mode", GX_DS_ENUM_RESEND_MODE},
    {"um_stop_acquisition_mode", GX_DS_ENUM_STOP_ACQUISITION_MODE},
    {"um_stream_buffer_handling_mode", GX_DS_ENUM_STREAM_BUFFER_HANDLING_MODE},
};

struct DoublePusher {
  const int gx_float;  // `GX_FLOAT_GAIN` or `GX_FLOAT_EXPOSURE_TIME`

  DoublePusher(int gx_float) : gx_float{gx_float} {
  }
  void operator()(GX_DEV_HANDLE handle, MallocStream& out) const {
    double dValue{};
    GX_STATUS const emStatus = GXGetFloat(handle, this->gx_float, &dValue);
    if (emStatus != GX_STATUS_SUCCESS) {
      dValue = 0.0;
    }
    thismsgpack::pack(dValue, out);
  }
};

struct VideoReaderGalaxy::Impl {
  GX_DEV_HANDLE handle;
  std::deque<FrameUP> read_queue;
  std::atomic<bool> stop_requested;
  SpinLock read_queue_lock;
  std::condition_variable_any cv;
  std::thread thread;
  std::exception_ptr exception;
  std::vector<DoublePusher> pushers;
  double timestamp_tick_frequency;
  AllocateCallback allocate_callback;
  DeallocateCallback deallocate_callback;
  VideoReader::LogCallback log_callback;
  void* userdata;

  Impl(
      GX_DEV_HANDLE handle,
      std::vector<std::string> const& parameter_pairs,
      std::vector<std::string> const& extras,
      AllocateCallback allocate_callback,
      DeallocateCallback deallocate_callback,
      VideoReader::LogCallback log_callback,
      void* userdata) :
      handle{handle},
      stop_requested{false},
      allocate_callback{allocate_callback},
      deallocate_callback{deallocate_callback},
      log_callback{log_callback},
      userdata{userdata} {
    for (const std::string& extra : extras) {
      if (extra == "exposure") {
        this->pushers.push_back(DoublePusher(GX_FLOAT_EXPOSURE_TIME));
      } else if (extra == "gain") {
        this->pushers.push_back(DoublePusher(GX_FLOAT_GAIN));
      } else {
        throw std::runtime_error("unknown extra: `" + extra + "`");
      }
    }
    int64_t out{};
    GALAXY_CHECK(GXGetInt(handle, GX_INT_TIMESTAMP_TICK_FREQUENCY, &out));
    this->timestamp_tick_frequency = static_cast<double>(out);
    GALAXY_CHECK(GXSetEnum(handle, GX_ENUM_EXPOSURE_AUTO, 1));
    GALAXY_CHECK(GXSetEnum(handle, GX_ENUM_GAIN_AUTO, 1));
    GALAXY_CHECK(GXSetInt(handle, GX_INT_BINNING_HORIZONTAL, 2));
    GALAXY_CHECK(GXSetInt(handle, GX_INT_BINNING_VERTICAL, 2));
    this->set(parameter_pairs);

    this->thread = std::thread(&VideoReaderGalaxy::Impl::read, this);
  }

  void set(std::vector<std::string> const& parameter_pairs) {

    for (std::vector<std::string>::const_iterator it = parameter_pairs.begin();
         it != parameter_pairs.end();
         ++it) {
      std::string const key = to_lower(*it);
      std::string const& value = *++it;
      set_pair(this->handle, key, value);
    }
  }

  ~Impl() {
    GXCloseDevice(this->handle);
  }

  void set_pair(
      GX_DEV_HANDLE handle, std::string const& key, std::string const& value) {
    if (auto pair = INT_FEATURES.find(key); pair != INT_FEATURES.end()) {
      int64_t int_value = std::stoll(value);
      GALAXY_CHECK(GXSetInt(handle, pair->second, int_value));
    } else if (auto pair = FLOAT_FEATURES.find(key);
               pair != FLOAT_FEATURES.end()) {
      double float_value = std::stod(value);
      GALAXY_CHECK(GXSetFloat(handle, pair->second, float_value));
    } else if (auto pair = ENUM_FEATURES.find(key);
               pair != ENUM_FEATURES.end()) {
      uint32_t nums = 0;
      GALAXY_CHECK(GXGetEnumEntryNums(handle, pair->second, &nums));
      size_t nBufferSize = nums * sizeof(GX_ENUM_DESCRIPTION);
      auto const pEnumDescription =
          std::unique_ptr<GX_ENUM_DESCRIPTION[]>(new GX_ENUM_DESCRIPTION[nums]);
      GALAXY_CHECK(GXGetEnumDescription(
          handle, pair->second, pEnumDescription.get(), &nBufferSize));
      uint32_t entry_idx = 0;
      for (; entry_idx < nums; ++entry_idx) {
        auto const& item = pEnumDescription.get()[entry_idx];
        if (item.szSymbolic == value) {
          GALAXY_CHECK(GXSetEnum(handle, pair->second, item.nValue));
          break;
        }
      }
      if (entry_idx == nums) {
        std::string valid_values;
        for (entry_idx = 0; entry_idx < nums; ++entry_idx) {
          if (entry_idx) {
            valid_values += ", ";
          }
          valid_values +=
              "`" + std::string(pEnumDescription.get()[entry_idx].szSymbolic) +
              "`";
        }
        throw std::runtime_error(
            "Failed to set `" + key + "` to `" + value +
            "`. Valid values are: " + valid_values + ".");
      }
    } else {
      if (this->log_callback) {
        std::string warning{"unknown key `" + key + "`. Available keys: "};
        warning.reserve(4096);
        for (auto const& it : INT_FEATURES) {
          warning.append(it.first);
          warning.append(", ", 2);
        }
        for (auto const& it : FLOAT_FEATURES) {
          warning.append(it.first);
          warning.append(", ", 2);
        }
        for (auto const& it : ENUM_FEATURES) {
          warning.append(it.first);
          warning.append(", ", 2);
        }
        warning.resize(warning.size() - 2);  // remove last comma

        this->log_callback(
            warning.c_str(), VideoReader::LogLevel::WARNING, this->userdata);
      }
    }
  }

  void read() noexcept {
    // https://github.com/JerryAuas/RmEverTo2022/blob/6ca1c2f6d94934d8a1296f3ad45b9cc327b7fa9b/Sources/armordetect1.cpp#L205
    try {
      uint32_t const TIMEOUT_MS = 250;
      GXStreamOn(this->handle);
      PGX_FRAME_BUFFER pFrameBuffer[5]{};
      uint32_t nFrameCount{};
      uint32_t timeoutHit{};
      uint64_t addFrames{};  // trying to make nFrameID contigious
      uint64_t previousFrameID{};  // trying to make nFrameID contigious
      while (!this->stop_requested) {
        auto const status = GXDQAllBufs(
            this->handle,
            pFrameBuffer,
            sizeof(pFrameBuffer) / sizeof(*pFrameBuffer),
            &nFrameCount,
            TIMEOUT_MS);
        if (status != GX_STATUS_SUCCESS) {
          if (status == GX_STATUS_TIMEOUT) {
            ++timeoutHit;
            if (timeoutHit > 3000 / TIMEOUT_MS) {
              throw std::runtime_error("no galaxy data for 3 seconds");
            }
            continue;
          } else {
            throw std::runtime_error(get_error_string(status));
          }
        }
        timeoutHit = 0;
        if (pFrameBuffer[nFrameCount - 1]->nStatus != GX_FRAME_STATUS_SUCCESS) {
          GXQAllBufs(this->handle);
          continue;
        }
        for (uint32_t idx = 0; idx < nFrameCount; ++idx) {
          PGX_FRAME_BUFFER buffer = pFrameBuffer[idx];
          if (buffer->nStatus != GX_FRAME_STATUS_SUCCESS) {
            if (this->log_callback) {
              std::string const last_error =
                  get_error_string(GX_STATUS_SUCCESS);
              this->log_callback(
                  ("buffer status is " + std::to_string(buffer->nStatus) +
                   ": " + last_error)
                      .c_str(),
                  VideoReader::LogLevel::WARNING,
                  this->userdata);
            }
            continue;
          }

          int32_t const alignment = 16;
          int32_t const preferred_stride =
              (buffer->nWidth * 1 + alignment - 1) & ~(alignment - 1);

          Frame::timestamp_s_t const timestamp_s =
              (static_cast<double>(buffer->nTimestamp) /
               this->timestamp_tick_frequency);  // bad nTimestamp cast, sorry

          uint64_t const frame_id =
              buffer->nFrameID - 1;  // -1 as Galaxy starts with 1
          if (frame_id < previousFrameID) {
            addFrames += (previousFrameID - frame_id) + 1;
          }
          Frame::number_t const number = frame_id + addFrames;
          previousFrameID = frame_id;

          FrameUP frame(new Frame(
              this->deallocate_callback,
              this->userdata,
              {
                  buffer->nHeight,  // height
                  buffer->nWidth,  // width
                  1,  // channels
                  SCALAR_TYPE::U8,  // scalar_type
                  preferred_stride,  // stride
                  nullptr,  // data
                  nullptr,  // user_data
              },
              number,
              timestamp_s));

          if (!this->pushers.empty()) {
            MallocStream stream{32};
            thismsgpack::pack_array_header(this->pushers.size(), stream);
            for (auto& pusher : this->pushers) {
              pusher(this->handle, stream);
            }
            frame->extras = stream.data();
            frame->extras_size = stream.size();
          }
          VRImage* image = &frame->image;
          (*this->allocate_callback)(image, this->userdata);
          if (!image->data) {
            throw std::runtime_error(
                "allocation callback failed: data is nullptr");
          }

          std::memcpy(
              image->data, buffer->pImgBuf, buffer->nWidth * buffer->nHeight);
          {
            std::lock_guard<SpinLock> guard(this->read_queue_lock);
            if (this->read_queue.size() > 9) {
              remove_every_second_item(this->read_queue);
            }
            this->read_queue.emplace_back(std::move(frame));
          }
          this->cv.notify_one();
        }
        GXQAllBufs(this->handle);
      }
    } catch (...) {
      this->stop_requested = true;
      this->exception = std::current_exception();
    }
  }
  FrameUP pop_grab_result() {
    this->cv.wait(this->read_queue_lock, [&] {
      return !this->read_queue.empty() || this->stop_requested;
    });
    if (!this->stop_requested) {
      auto ret = std::move(this->read_queue.front());
      this->read_queue.pop_front();
      this->read_queue_lock.unlock();
      return ret;
    }
    this->read_queue_lock.unlock();
    if (this->thread.joinable()) {
      this->thread.join();
    }
    if (this->exception) {
      std::rethrow_exception(this->exception);
    }
    return nullptr;
  }

  void stop() {
    this->stop_requested = true;
    this->cv.notify_one();
  }
};

VideoReaderGalaxy::VideoReaderGalaxy(
    std::string const& url,
    std::vector<std::string> const& parameter_pairs,
    std::vector<std::string> const& extras,
    AllocateCallback allocate_callback,
    DeallocateCallback deallocate_callback,
    LogCallback log_callback,
    void* userdata) {

  auto const res = GXInitLib();
  if (res != GX_STATUS_SUCCESS) {
    throw std::runtime_error("GXInitLib was't successful");
  }

  auto const info = url.substr(9);
  GX_DEV_HANDLE handle{};

  static GX_OPEN_MODE_CMD modes[] = {
      GX_OPEN_IP, GX_OPEN_SN, GX_OPEN_MAC, GX_OPEN_INDEX, GX_OPEN_USERID};

  GX_OPEN_PARAM param{
      const_cast<char*>(info.c_str()), GX_OPEN_SN, GX_ACCESS_EXCLUSIVE};
  for (int mode_idx = 0; mode_idx < sizeof(modes) / sizeof(*modes);
       ++mode_idx) {
    param.openMode = modes[mode_idx];
    auto const ret = GXOpenDevice(&param, &handle);
    if (ret == GX_STATUS_SUCCESS) {
      this->impl = std::make_unique<Impl>(
          handle,
          parameter_pairs,
          extras,
          allocate_callback,
          deallocate_callback,
          log_callback,
          userdata);
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

VideoReader::Frame::number_t VideoReaderGalaxy::size() const {
  return 0;
}

void VideoReaderGalaxy::set(std::vector<std::string> const& parameter_pairs) {
  if (parameter_pairs.size() % 2 != 0) {
    throw std::runtime_error("invalid videoreader parameters size");
  }
  this->impl->set(parameter_pairs);
}

void VideoReaderGalaxy::stop() {
  this->impl->stop();
}

VideoReaderGalaxy::~VideoReaderGalaxy() {
  this->stop();
  if (this->impl->thread.joinable()) {
    this->impl->thread.join();
  }
  GXCloseLib();
}
