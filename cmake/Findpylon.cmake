# Ad-hoc writings by Arseniy Terekhin<senyai@gmail.com>.
# Suggestions are welcomed!

set(PYLON_DIR "" CACHE STRING "pylon directory hint")

find_path(PYLON_INCLUDE_DIR GenICam.h
  HINTS
  ${PYLON_DIR}
  $ENV{PYLON_DIR}
  $ENV{HOME}/.local
  PATH_SUFFIXES include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/include
  /usr/include
  /usr/include/PYLON
  /sw/include
  /opt/local/include
  /opt/pylon/include
  /opt/include
  /mingw/include
  "C:/Program Files/Basler/pylon 6/Development/include"
  "C:/Program Files/Basler/pylon 5/Development/include"
  "D:/Program Files/Basler/pylon 6/Development/include"
  "D:/Program Files/Basler/pylon 5/Development/include"
)

if (PYLON_INCLUDE_DIR AND EXISTS "${PYLON_INCLUDE_DIR}/pylon/PylonVersionNumber.h")
  file(STRINGS "${PYLON_INCLUDE_DIR}/pylon/PylonVersionNumber.h" _VERSION_DEFS REGEX "^[ \t]*#define[ \t]+PYLON_VERSION_(MAJOR|MINOR|SUBMINOR)")
  string(REGEX REPLACE ".*_MAJOR[ \t]+([0-9]+).*" "\\1" PYLON_VERSION_MAJ "${_VERSION_DEFS}")
  string(REGEX REPLACE ".*_MINOR[ \t]+([0-9]+).*" "\\1" PYLON_VERSION_MIN "${_VERSION_DEFS}")
  string(REGEX REPLACE ".*SUBMINOR[ \t]+([0-9]+).*" "\\1" PYLON_VERSION_MIC "${_VERSION_DEFS}")
  set(PYLON_VERSION_STRING "${PYLON_VERSION_MAJ}.${PYLON_VERSION_MIN}.${PYLON_VERSION_MIC}")
  unset(PYLON_VERSION_MAJ)
  unset(PYLON_VERSION_MIN)
  unset(PYLON_VERSION_MIC)
endif()

include(FindPackageHandleStandardArgs)

macro(PYLON_find_library varname)
  find_library(${varname}
    NAMES ${ARGN}
    HINTS
    ${PYLON_DIR}
    $ENV{PYLON_DIR}
    $ENV{HOME}/.local
    PATH_SUFFIXES lib64 lib bin
    PATHS
    /usr/local
    /usr
    /sw
    /opt/local
    /opt/pylon
    /opt
    /mingw
    "${PYLON_INCLUDE_DIR}/../lib/x64"
  )
endmacro()

PYLON_find_library(PYLON_BASE_LIBRARY
  libpylonbase.so
  PylonBase_MD_VC120_v5_0.lib
)
PYLON_find_library(PYLON_GC_BASE_LIBRARY
  GCBase_MD_VC120_v3_0_Basler_pylon_v5_0.lib
  libGCBase_gcc_v3_1_Basler_pylon.so
)
PYLON_find_library(PYLON_GENAPI_LIBRARY
  GenApi_MD_VC120_v3_0_Basler_pylon_v5_0.lib
  libGenApi_gcc_v3_1_Basler_pylon.so
)
PYLON_find_library(PYLON_UTILITY_LIBRARY
  PylonUtility_MD_VC120_v5_0.lib
  libpylonutility.so
)


find_package_handle_standard_args(
  pylon
  REQUIRED_VARS
    PYLON_INCLUDE_DIR
    PYLON_BASE_LIBRARY
    PYLON_GC_BASE_LIBRARY
    PYLON_GENAPI_LIBRARY
    PYLON_UTILITY_LIBRARY
  VERSION_VAR PYLON_VERSION_STRING
)

if(pylon_FOUND AND NOT TARGET pylon::pylon_base)
  get_filename_component(pylon_LINK_DIRECTORIES "${PYLON_BASE_LIBRARY}" DIRECTORY)
  add_library(pylon::pylon_base UNKNOWN IMPORTED)
  set_target_properties(pylon::pylon_base PROPERTIES
    IMPORTED_LOCATION "${PYLON_BASE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${PYLON_INCLUDE_DIR}")

  add_library(pylon::gc UNKNOWN IMPORTED)
  set_target_properties(pylon::gc PROPERTIES
    IMPORTED_LOCATION "${PYLON_GC_BASE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${PYLON_INCLUDE_DIR}")

  add_library(pylon::genapi UNKNOWN IMPORTED)
  set_target_properties(pylon::genapi PROPERTIES
    IMPORTED_LOCATION "${PYLON_GENAPI_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${PYLON_INCLUDE_DIR}")

  add_library(pylon::utility UNKNOWN IMPORTED)
  set_target_properties(pylon::utility PROPERTIES
    IMPORTED_LOCATION "${PYLON_UTILITY_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${PYLON_INCLUDE_DIR}")

  add_library(pylon INTERFACE)
  target_link_libraries(pylon INTERFACE pylon::genapi pylon::gc pylon::utility pylon::pylon_base)

endif()
