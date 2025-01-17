# Ad-hoc writings by Arseniy Terekhin<senyai@gmail.com>.
# Suggestions are welcomed!

set(GALAXY_DIR "" CACHE STRING "galaxy directory hint")

find_path(GALAXY_INCLUDE_DIR GxIAPI.h
  HINTS
  ${GALAXY_DIR}
  $ENV{GALAXY_DIR}
  $ENV{HOME}/.local
  PATH_SUFFIXES inc include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/include
  /usr/include
  /sw/include
  /opt/local/include
  /opt/include
  /mingw/include
)

if (GALAXY_INCLUDE_DIR)
  file(STRINGS "${GALAXY_INCLUDE_DIR}/GxIAPI.h" _GALAXY_VERSION_STRING REGEX "^@Version[ \t]+(.*)")
  string(REGEX REPLACE "@Version[ \t]+(.*)" "\\1" GALAXY_VERSION_STRING "${_GALAXY_VERSION_STRING}")
endif()

include(FindPackageHandleStandardArgs)

find_library(GALAXY_GXI_LIBRARY
NAMES libgxiapi.so
HINTS
${GALAXY_DIR}
$ENV{GALAXY_DIR}
$ENV{HOME}/.local
PATH_SUFFIXES lib lib/x86_64
PATHS
/usr/local
/usr
/sw
/opt/local
/opt
/mingw
"${GALAXY_INCLUDE_DIR}/../lib/x86_64"
)

find_package_handle_standard_args(
  galaxy
  REQUIRED_VARS
    GALAXY_INCLUDE_DIR
    GALAXY_GXI_LIBRARY
  VERSION_VAR GALAXY_VERSION_STRING
)

if(GALAXY_FOUND AND NOT TARGET galaxy)
    add_library(galaxy UNKNOWN IMPORTED)
    set_target_properties(galaxy PROPERTIES
        IMPORTED_LOCATION "${GALAXY_GXI_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${GALAXY_INCLUDE_DIR}")
endif()
