#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <videoreader/videoreader.hpp>
#ifdef USE_MINVIEWER_CLIENT
#include <minviewer/client.hpp>
#endif
static bool ctrl_c = false;
#ifdef _WIN32
#include <Windows.h>

BOOL WINAPI consoleHandler(DWORD signal) {
  if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT ||
      signal == CTRL_CLOSE_EVENT) {
    ctrl_c = true;
    return TRUE;
  }
  return FALSE;
}

#endif

static char VR_RESET[] = "\033[0m";
static char VR_RED[] = "\033[31m";
static char VR_GREEN[] = "\033[32m";
static char VR_YELLOW[] = "\033[33m";
static char VR_BLUE[] = "\033[34m";
static char VR_MAGENTA[] = "\033[35m";
static char VR_CYAN[] = "\033[36m";
static char VR_WHITE[] = "\033[37m";

static void disable_colors() {
  *VR_RESET = *VR_RED = *VR_GREEN = *VR_YELLOW = *VR_BLUE = *VR_MAGENTA =
      *VR_CYAN = *VR_WHITE = 0;
}

static bool terminal_supports_colors() {
  const char* term = std::getenv("TERM");
  if (term) {
    std::string termStr(term);
    return termStr.find("xterm") != std::string::npos ||
           termStr.find("screen") != std::string::npos ||
           termStr.find("tmux") != std::string::npos;
  }
  return false;  // Default to no support if TERM is not set
}

static void log_callback(
    char const* message, VideoReader::LogLevel log_level, void* userdata) {
  std::cout << [log_level]() {
    switch (log_level) {
    case VideoReader::LogLevel::DEBUG:
      return VR_BLUE;
    case VideoReader::LogLevel::ERROR:
      return VR_RED;
    case VideoReader::LogLevel::FATAL:
      return VR_MAGENTA;
    case VideoReader::LogLevel::INFO:
      return VR_CYAN;
    case VideoReader::LogLevel::WARNING:
      return VR_YELLOW;
    default:
      return VR_WHITE;
    }
  }();
  std::cout << "[vr]" << message << VR_RESET;
}

static void
run(std::string const& url,
    std::vector<std::string> const& parameter_pairs,
    std::vector<std::string> const& extras) {
#ifdef _WIN32
  if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
    throw std::runtime_error("ERROR: Could not set control handler");
  }
#endif

  auto video_reader = VideoReader::create(
      url, parameter_pairs, extras, nullptr, nullptr, log_callback);
  uint64_t const frames_count = video_reader->size();
  std::cout << "frames_count: " << VR_CYAN << frames_count << VR_RESET << '\n'
            << std::boolalpha << "is_seekaable: " << VR_CYAN
            << video_reader->is_seekable() << VR_RESET << std::endl;
  unsigned int const FPS_SZ{16};
  std::vector<double> fps(FPS_SZ);
  std::vector<std::chrono::high_resolution_clock::duration> durations(FPS_SZ);

#ifdef USE_MINVIEWER_CLIENT
  auto c = MinViewerClient(url);
  minviewer_id_t img_id = c.add_image(MinImg{});
#endif

  unsigned int counter{};
  VideoReader::Frame::number_t missed_frames{};

  auto prev_time = std::chrono::high_resolution_clock::now();
  VideoReader::Frame::timestamp_s_t prev_timestamp{};
  VideoReader::Frame::number_t prev_frame_number{
      static_cast<VideoReader::Frame::number_t>(-1)};  // we start with #0
  std::cout << std::fixed;
  while (auto frame = video_reader->next_frame()) {
    if (ctrl_c) {
      break;
    }
    auto const cur_time = std::chrono::high_resolution_clock::now();
    fps[counter % FPS_SZ] = frame->timestamp_s - prev_timestamp;
    missed_frames += (frame->number - 1) - prev_frame_number;
    prev_timestamp = frame->timestamp_s;
    prev_frame_number = frame->number;
    durations[counter % FPS_SZ] = cur_time - prev_time;

#ifdef USE_MINVIEWER_CLIENT
    c.add_image(frame->image, {{"obj_id", img_id}});
#endif
    std::cout << "[" << frame->number << "/" << frames_count - 1 << "] "
              << VR_CYAN << frame->image.width << "x" << frame->image.height
              << "x" << frame->image.channels << VR_RESET << " @ "
              << std::setw(10) << VR_CYAN << frame->timestamp_s << "s"
              << VR_RESET << " [missed " << missed_frames << "]";
    if (counter >= FPS_SZ) {
      double const real_fps =
          fps.size() / std::accumulate(fps.begin(), fps.end(), 0.0);
      auto const total_duration = std::accumulate(
          durations.begin(),
          durations.end(),
          std::chrono::high_resolution_clock::duration{});
      std::chrono::duration<double> duration_s = total_duration;
      double const read_fps = FPS_SZ / duration_s.count();
      std::cout << " [real " << std::setw(7) << std::setprecision(2) << VR_CYAN
                << real_fps << "fps" << VR_RESET << " / read " << std::setw(7)
                << VR_CYAN << read_fps << "fps" << VR_RESET << ']';
    }
    std::cout << std::endl;
    ++counter;
    prev_time = std::chrono::high_resolution_clock::now();
  }
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout << "usage:" << argv[0]
              << " URL [PARAMETER VALUE] ... [--extras [EXTRAS]]\n";
    return 1;
  }
  if (!terminal_supports_colors()) {
    disable_colors();
  }
  try {
    std::string const uri = std::string(argv[1]);
    std::vector<std::string> parameter_pairs(argv + 2, argv + argc);
    std::vector<std::string> extras{};
    auto extras_it =
        std::find(parameter_pairs.begin(), parameter_pairs.end(), "--extras");
    if (extras_it != parameter_pairs.end()) {  // have extras
      std::move(
          extras_it + 1, parameter_pairs.end(), std::back_inserter(extras));
      parameter_pairs.pop_back();  // remove --extras
    }
    run(uri, parameter_pairs, extras);
  } catch (std::runtime_error& e) {
    std::cerr << VR_RED << "EXCEPTION: " << e.what() << VR_RESET << '\n';
    return 1;
  }
  return 0;
}
