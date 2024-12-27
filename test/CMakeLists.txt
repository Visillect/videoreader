#
# this file is intended for internal videoreader build testing
# i.e. how it
#
cmake_minimum_required(VERSION 3.17)

project(videoreader-build)

set(VIDEOREADER_BUILD_CAPI ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/..")
find_package(ffmpeg 57.1 REQUIRED)
find_package(galaxy 1.4)
find_package(pylon 9.0.0)
add_subdirectory(.. ../build/videoreader_build)
