project(videoreader)


add_library(videoreader
  src/videoreader.cpp
  include/videoreader/videoreader.h
)
target_link_libraries(videoreader
PUBLIC
  minbase
PRIVATE
  minimgapi
)
target_include_directories(videoreader PUBLIC include)
if (UNIX)
  target_link_libraries(videoreader PRIVATE pthread)
endif()

set(WITH_FFMPEG "OFF")
if (FFMPEG_FOUND)
  set(WITH_FFMPEG "ON")
  target_compile_definitions(videoreader PRIVATE VIDEOREADER_WITH_FFMPEG)
  target_link_libraries(videoreader PRIVATE
    ffmpeg::avcodec
    ffmpeg::avdevice
    ffmpeg::avformat
    ffmpeg::avutil
    ffmpeg::swscale
  )
  target_sources(videoreader PRIVATE
    src/videoreader_ffmpeg.cpp
    src/videoreader_ffmpeg.hpp
  )
endif()

set(WITH_PYLON "OFF")
if (pylon_FOUND)
  set(WITH_PYLON "ON")
  target_compile_definitions(videoreader PRIVATE VIDEOREADER_WITH_PYLON)
  target_link_libraries(videoreader PRIVATE pylon)
  target_sources(videoreader PRIVATE
    src/videoreader_pylon.cpp
    src/videoreader_pylon.hpp
  )
endif()

message(STATUS "videoreader: FFMPEG[${WITH_FFMPEG}] PYLON[${WITH_PYLON}]")

if(WITH_TESTS)
  add_executable(test_videoreader test/test_videoreader.cpp)
  target_link_libraries(test_videoreader PRIVATE videoreader gtest)
  target_compile_definitions(test_videoreader PRIVATE "TEST_VIDEOPATH=\"${CMAKE_CURRENT_LIST_DIR}/test/big_buck_bunny_480p_1mb.mp4\"")
  add_test(
    NAME test_videoreader
    COMMAND test_videoreader)
endif()

add_executable(videoreader_go src/videoreader_go.cpp)
target_link_libraries(videoreader_go PRIVATE videoreader)
if(TARGET minviewer_client)
  target_link_libraries(videoreader_go PRIVATE minviewer_client)
  target_compile_definitions(videoreader_go PRIVATE USE_MINVIEWER_CLIENT)
endif()
