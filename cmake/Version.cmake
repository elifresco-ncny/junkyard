# JUNKYARD_VERSION lives in cmake/VersionNumber.cmake (included before project()).
# Repo URL shown in the About box and SOURCE.txt (also in README.md download links).
set(JUNKYARD_REPO_URL "https://github.com/elifresco-ncny/junkyard")
option(JUNKYARD_DEV_BUILD "Dev build (shows build stamp)" ON)

set(JUNKYARD_BUILD_NUMBER 1)
if(EXISTS "${CMAKE_BINARY_DIR}/.build_number")
  file(READ "${CMAKE_BINARY_DIR}/.build_number" JUNKYARD_BUILD_NUMBER)
  string(STRIP "${JUNKYARD_BUILD_NUMBER}" JUNKYARD_BUILD_NUMBER)
endif()
string(TIMESTAMP JUNKYARD_BUILD_TIME "%H:%M")
string(TIMESTAMP JUNKYARD_BUILD_DATE "%Y-%m-%d")
if(JUNKYARD_DEV_BUILD)
  set(JUNKYARD_DEV_BUILD_INT 1)
else()
  set(JUNKYARD_DEV_BUILD_INT 0)
endif()
configure_file(${CMAKE_SOURCE_DIR}/src/BuildInfo.h.in ${CMAKE_BINARY_DIR}/generated/BuildInfo.h @ONLY)

if(APPLE)
  set(JUNKYARD_FORMATS AU VST3 Standalone)
else()
  set(JUNKYARD_FORMATS VST3)
endif()
