# Ad-hoc writings by Arseniy Terekhin<senyai@gmail.com>.
# Suggestions are welcomed!

set(FFMPEG_DIR "" CACHE STRING "ffmpeg directory hint")

find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
  HINTS
  ${FFMPEG_DIR}
  $ENV{FFMPEG_DIR}
  $ENV{HOME}/.local
  PATH_SUFFIXES include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/include
  /usr/include
  /usr/include/ffmpeg
  /sw/include
  /opt/local/include
  /opt/csw/include
  /opt/include
  /mingw/include
)

if (FFMPEG_INCLUDE_DIR AND EXISTS "${FFMPEG_INCLUDE_DIR}/libavcodec/version.h")
  file(STRINGS "${FFMPEG_INCLUDE_DIR}/libavcodec/version.h" _LIBAVCODEC_DEFS REGEX "^[ \t]*#define[ \t]+LIBAVCODEC_VERSION_(MAJOR|MINOR|MICRO)")
  # parse major version
  if (EXISTS "${FFMPEG_INCLUDE_DIR}/libavcodec/version_major.h")
    file(STRINGS "${FFMPEG_INCLUDE_DIR}/libavcodec/version_major.h" _LIBAVCODEC_MAJOR_DEFS REGEX "^[ \t]*#define[ \t]+LIBAVCODEC_VERSION_MAJOR")
    string(REGEX REPLACE ".*_MAJOR[ \t]+([0-9]+).*" "\\1" LIBAVCODEC_VERSION_MAJ "${_LIBAVCODEC_MAJOR_DEFS}")
  else()
    string(REGEX REPLACE ".*_MAJOR[ \t]+([0-9]+).*" "\\1" LIBAVCODEC_VERSION_MAJ "${_LIBAVCODEC_DEFS}")
  endif()
  # parse minor and micro versions
  string(REGEX REPLACE ".*_MINOR[ \t]+([0-9]+).*" "\\1" LIBAVCODEC_VERSION_MIN "${_LIBAVCODEC_DEFS}")
  string(REGEX REPLACE ".*_MICRO[ \t]+([0-9]+).*" "\\1" LIBAVCODEC_VERSION_MIC "${_LIBAVCODEC_DEFS}")
  set(LIBAVCODEC_VERSION_STRING "${LIBAVCODEC_VERSION_MAJ}.${LIBAVCODEC_VERSION_MIN}.${LIBAVCODEC_VERSION_MIC}")
  unset(LIBAVCODEC_VERSION_MAJ)
  unset(LIBAVCODEC_VERSION_MIN)
  unset(LIBAVCODEC_VERSION_MIC)
endif()

include(FindPackageHandleStandardArgs)

macro(ffmpeg_find_library varname)
  find_library(${varname}
    NAMES ${ARGN}
    HINTS
    ${FFMPEG_DIR}
    $ENV{FFMPEG_DIR}
    $ENV{HOME}/.local
    PATH_SUFFIXES lib64 lib bin
    PATHS
    /usr/local
    /usr
    /sw
    /opt/local
    /opt/csw
    /opt
    /mingw
  )
endmacro()

ffmpeg_find_library(AVCODEC_LIBRARY avcodec)
ffmpeg_find_library(AVDEVICE_LIBRARY avdevice)
ffmpeg_find_library(AVFORMAT_LIBRARY avformat)
ffmpeg_find_library(AVUTIL_LIBRARY avutil)
ffmpeg_find_library(SWSCALE_LIBRARY swscale)
ffmpeg_find_library(SWRESAMPLE_LIBRARY swresample)

if (WIN32)
  macro(ffmpeg_win_find_library varname)
    find_library(${varname}
      NAMES ${ARGN}
      HINTS
      ${FFMPEG_DIR}
      $ENV{FFMPEG_DIR}
      $ENV{HOME}/.local
      PATH_SUFFIXES bin
      PATHS
      /usr/local
      /usr
      /sw
      /opt/local
      /opt/csw
      /opt
      /mingw
    )
  endmacro()
  set(CMAKE_FIND_LIBRARY_SUFFIXES_ORIGINAL ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll")
  ffmpeg_win_find_library(AVCODEC_SHARED_OBJECT avcodec-57 avcodec-58)
  ffmpeg_win_find_library(AVDEVICE_SHARED_OBJECT avdevice-57 avdevice-58)
  ffmpeg_win_find_library(AVFORMAT_SHARED_OBJECT avformat-57 avformat-58)
  ffmpeg_win_find_library(AVUTIL_SHARED_OBJECT avutil-55 avutil-56)
  ffmpeg_win_find_library(SWSCALE_SHARED_OBJECT swscale-4 swscale-5)
  ffmpeg_win_find_library(SWRESAMPLE_SHARED_OBJECT swresample-2 swresample-3)
  ffmpeg_win_find_library(AVFILTER_SHARED_OBJECT avfilter-6 avfilter-7)
  ffmpeg_win_find_library(POSTPROC_SHARED_OBJECT postproc-54 postproc-55)
  SET(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES_ORIGINAL})

  find_package_handle_standard_args(
    ffmpeg
    REQUIRED_VARS
      FFMPEG_INCLUDE_DIR
      AVCODEC_LIBRARY
      AVDEVICE_LIBRARY
      AVFORMAT_LIBRARY
      AVUTIL_LIBRARY
      SWSCALE_LIBRARY
      AVCODEC_SHARED_OBJECT
      AVDEVICE_SHARED_OBJECT
      AVFORMAT_SHARED_OBJECT
      AVUTIL_SHARED_OBJECT
      SWSCALE_SHARED_OBJECT
      SWRESAMPLE_SHARED_OBJECT
      AVFILTER_SHARED_OBJECT
      POSTPROC_SHARED_OBJECT
    VERSION_VAR LIBAVCODEC_VERSION_STRING
  )
else()
  find_package_handle_standard_args(
    ffmpeg
    REQUIRED_VARS
      FFMPEG_INCLUDE_DIR
      AVCODEC_LIBRARY
      AVDEVICE_LIBRARY
      AVFORMAT_LIBRARY
      AVUTIL_LIBRARY
      SWSCALE_LIBRARY
    VERSION_VAR LIBAVCODEC_VERSION_STRING
  )
endif()

mark_as_advanced(
  FFMPEG_INCLUDE_DIR
  AVCODEC_LIBRARY
  AVDEVICE_LIBRARY
  AVFORMAT_LIBRARY
  AVUTIL_LIBRARY
  SWSCALE_LIBRARY
  SWRESAMPLE_LIBRARY
  LIBAVCODEC_VERSION_STRING
)

if(ffmpeg_FOUND AND NOT TARGET ffmpeg::avcodec)
  add_library(ffmpeg::avutil UNKNOWN IMPORTED)
  set_target_properties(ffmpeg::avutil PROPERTIES
    IMPORTED_LOCATION "${AVUTIL_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}")

  add_library(ffmpeg::swresample UNKNOWN IMPORTED)
  set_target_properties(ffmpeg::swresample PROPERTIES
    IMPORTED_LOCATION "${SWRESAMPLE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "ffmpeg::avutil")

  add_library(ffmpeg::avcodec UNKNOWN IMPORTED)
  set_target_properties(ffmpeg::avcodec PROPERTIES
    IMPORTED_LOCATION "${AVCODEC_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "ffmpeg::avutil;ffmpeg::swresample")

  add_library(ffmpeg::avformat UNKNOWN IMPORTED)
  set_target_properties(ffmpeg::avformat PROPERTIES
    IMPORTED_LOCATION "${AVFORMAT_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "ffmpeg::avutil;ffmpeg::avcodec")

  add_library(ffmpeg::avdevice UNKNOWN IMPORTED)
  set_target_properties(ffmpeg::avdevice PROPERTIES
    IMPORTED_LOCATION "${AVDEVICE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "ffmpeg::avformat;ffmpeg::avcodec;ffmpeg::avutil")

  add_library(ffmpeg::swscale UNKNOWN IMPORTED)
  set_target_properties(ffmpeg::swscale PROPERTIES
    IMPORTED_LOCATION "${SWSCALE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "ffmpeg::avutil")
endif()
