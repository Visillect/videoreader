project(videoreader)

add_library(videoreader
  src/videoreader.cpp
  include/videoreader/videoreader.h
)
add_library(videowriter
  src/videowriter.cpp
  include/videoreader/videowriter.h
)

macro(videoreader_setup target)
  set_property(TARGET ${target} PROPERTY CXX_STANDARD 17)
  target_link_libraries(${target}
  PUBLIC
    minbase
  PRIVATE
    minimgapi
  )
  target_include_directories(${target} PUBLIC include)
  if (UNIX)
    target_link_libraries(${target} PRIVATE pthread)
  endif()
endmacro()

videoreader_setup(videoreader)

option(VIDEOREADER_BUILD_CAPI "Videoreader: build C API" OFF)

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
    src/ffmpeg_common.cpp
    src/ffmpeg_common.hpp
    src/thismsgpack.cpp
    src/thismsgpack.hpp
  )

  # enable videowriter
  videoreader_setup(videowriter)
  target_link_libraries(videowriter PRIVATE videoreader)
  target_link_libraries(videowriter PRIVATE
    ffmpeg::avcodec
    ffmpeg::avdevice
    ffmpeg::avformat
    ffmpeg::avutil
    ffmpeg::swscale
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

set(WITH_GALAXY "OFF")
if (galaxy_FOUND)
  set(WITH_GALAXY "ON")
  target_compile_definitions(videoreader PRIVATE VIDEOREADER_WITH_GALAXY)
  target_link_libraries(videoreader PRIVATE galaxy)
  target_sources(videoreader PRIVATE
    src/videoreader_galaxy.cpp
    src/videoreader_galaxy.hpp
    src/thismsgpack.cpp
    src/thismsgpack.hpp
  )
endif()

message(STATUS
  "videoreader: "
  "FFMPEG[${WITH_FFMPEG}] "
  "PYLON[${WITH_PYLON}] "
  "GALAXY[${WITH_GALAXY}] "
  "CAPI[${VIDEOREADER_BUILD_CAPI}]"
)

if (VIDEOREADER_BUILD_CAPI)
  # we can expose C API from videoreader library itself, but
  # it can be any of type, so make sure we create a SHARED library
  add_library(videoreader_c SHARED src/videoreader_c.cpp)
  target_link_libraries(videoreader_c PRIVATE videoreader videowriter)
endif()

if(BUILD_TESTING)
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
