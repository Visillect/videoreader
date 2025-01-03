cmake_minimum_required(VERSION 3.17)
project(videoreader)

add_library(videoreader
  src/videoreader.cpp
  include/videoreader/videoreader.hpp
)
add_library(videowriter
  src/videowriter.cpp
  include/videoreader/videowriter.hpp
)

macro(videoreader_setup target)
  set_property(TARGET ${target} PROPERTY CXX_STANDARD 17)
  target_include_directories(${target} PUBLIC include)
  if (UNIX)
    target_link_libraries(${target} PRIVATE pthread)
  endif()
  set_property(TARGET ${target} PROPERTY POSITION_INDEPENDENT_CODE ON)
endmacro()

videoreader_setup(videoreader)
videoreader_setup(videowriter)

option(VIDEOREADER_BUILD_CAPI "Videoreader: build C API" OFF)
set(VIDEOREADER_CFFI OFF CACHE BOOL "Videoreader: build cffi backend")
if (VIDEOREADER_CFFI AND NOT VIDEOREADER_BUILD_CAPI)
  message(FATAL_ERROR "can't build cffi without CAPI")
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
    src/ffmpeg_common.cpp
    src/ffmpeg_common.hpp
    src/thismsgpack.cpp
    src/thismsgpack.hpp
  )

  # enable videowriter
  target_compile_definitions(videowriter PRIVATE VIDEOWRITER_WITH_FFMPEG)
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

  if (VIDEOREADER_CFFI)
    message(STATUS "WORKING_DIRECTORY: ${CMAKE_CURRENT_SOURCE_DIR}/python")
    add_custom_command(
      OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/videoreader/_videoreader.py"
      COMMAND
        ${PY_BUILD_CMAKE_PYTHON_INTERPRETER}
        "${CMAKE_CURRENT_SOURCE_DIR}/python/videoreader/videoreader_build.py"
    )
    add_custom_target(
      videoreader_cffi ALL
      DEPENDS
      "${CMAKE_CURRENT_BINARY_DIR}/videoreader/_videoreader.py"
    )
    add_dependencies(videoreader_c videoreader_cffi)

    install(TARGETS videoreader_c
      EXCLUDE_FROM_ALL
      COMPONENT python_modules
      DESTINATION ${PY_BUILD_CMAKE_IMPORT_NAME}
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/videoreader/_videoreader.py"
      EXCLUDE_FROM_ALL
      COMPONENT python_modules
      DESTINATION ${PY_BUILD_CMAKE_IMPORT_NAME}
    )

  endif()
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
