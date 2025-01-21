# Ad-hoc writings by Arseniy Terekhin<senyai@gmail.com>.
# Suggestions are welcomed!

set(IDATUM_DIR "" CACHE STRING "iDatum directory hint")

find_path(IDATUM_INCLUDE_DIR MvCameraControl.h
  HINTS
  ${IDATUM_DIR}
  $ENV{IDATUM_DIR}
  $ENV{HOME}/.local
  PATH_SUFFIXES inc include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/include
  /usr/include
  /sw/include
  /opt/local/include
  /opt/iDatum/include
  /opt/include
  /mingw/include
)

if (IDATUM_INCLUDE_DIR)
  find_library(IDATUM_MvCameraControl_LIBRARY
    NAMES libMvCameraControl.so
    HINTS
    ${IDATUM_DIR}
    $ENV{IDATUM_DIR}
    $ENV{HOME}/.local
    PATH_SUFFIXES lib lib/64
    PATHS
    /usr/local
    /usr
    /sw
    /opt/local
    /opt
    /mingw
    "${IDATUM_INCLUDE_DIR}/../lib/64"
  )
  get_filename_component(IDATUM_LIB_DIR "${IDATUM_MvCameraControl_LIBRARY}" DIRECTORY)
  set(IDATUM_GCBase_LIBRARY "${IDATUM_LIB_DIR}/libGCBase_gcc421_v3_0.so")

  set(IDATUM_VERSION_FILE "${CMAKE_BINARY_DIR}/idatum_version.c")
  file(WRITE "${IDATUM_VERSION_FILE}"
    "#include <stdio.h>\n"
    "#include <MvCameraControl.h>\n\n"
    "int main(){\n"
    "  union {\n"
    "    int version;\n"
    "    struct {unsigned char Test, Rev, Sub, Main;};\n"
    "  } version_s;\n\n"
    "  int version = MV_CC_GetSDKVersion();"
    "  version_s.version = version;"
    "  printf(\"idatum_version: %d.%d.%d.%d (%#08x)\", version_s.Main, version_s.Sub, version_s.Rev, version_s.Test, version);\n"
    "  return 0;\n"
    "}\n"
  )

  set(ENV{LD_LIBRARY_PATH} "${IDATUM_LIB_DIR}")

  try_run(
    # Name of variable to store the run result (process exit status; number) in:
    test_run_result

    # Name of variable to store the compile result (TRUE or FALSE) in:
    test_compile_result

    # Binary directory:
    "${CMAKE_CURRENT_BINARY_DIR}"

    # Source file to be compiled:
    "${IDATUM_VERSION_FILE}"

    # Where to store the output produced during compilation:
    COMPILE_OUTPUT_VARIABLE test_compile_output

    # Where to store the output produced by running the compiled executable:
    RUN_OUTPUT_VARIABLE test_run_output

    # LINK_LIBRARIES iDatum
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES:STRING=${IDATUM_INCLUDE_DIR}"

    LINK_LIBRARIES "${IDATUM_MvCameraControl_LIBRARY}" # "${IDATUM_GCBase_LIBRARY}"
  )
  if (NOT test_compile_result)
    message(FATAL_ERROR "${test_compile_output}")
  endif()
endif()

string(REGEX MATCH "\\d+\\.\\d+\\.\\d+\\.\\d+" IDATUM_VERSION_STRING "${test_run_output}")

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
  idatum
  REQUIRED_VARS
    IDATUM_INCLUDE_DIR
    IDATUM_GCBase_LIBRARY
  VERSION_VAR IDATUM_VERSION_STRING
)

if(IDATUM_FOUND AND NOT TARGET idatum)
  get_filename_component(idatum_LINK_DIRECTORIES "${IDATUM_GCBase_LIBRARY}" DIRECTORY)
  add_library(idatum::GCBase UNKNOWN IMPORTED)
  set_target_properties(idatum::GCBase PROPERTIES
    IMPORTED_LOCATION "${IDATUM_GCBase_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${IDATUM_INCLUDE_DIR}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
    BUILD_WITH_INSTALL_RPATH 1
    INSTALL_RPATH "${IDATUM_LIB_DIR}"
  )

  message(STATUS "GCMABSE = ${IDATUM_GCBase_LIBRARY}")
  add_library(idatum::MvCameraControl UNKNOWN IMPORTED)
  set_target_properties(idatum::MvCameraControl PROPERTIES
    IMPORTED_LOCATION "${IDATUM_MvCameraControl_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${IDATUM_INCLUDE_DIR}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
    BUILD_WITH_INSTALL_RPATH 1
  )
  message(STATUS "GCMABSE = ${IDATUM_MvCameraControl_LIBRARY}")

  add_library(idatum::idatum INTERFACE IMPORTED)
  set_target_properties(idatum::idatum PROPERTIES
    INTERFACE_LINK_LIBRARIES "idatum::GCBase;idatum::MvCameraControl"
    BUILD_WITH_INSTALL_RPATH 1
    INSTALL_RPATH "${IDATUM_LIB_DIR}"
  )
  message(STATUS "!!!${IDATUM_LIB_DIR}")
  # INSTALL_RPATH "$ORIGIN/.."
endif()
