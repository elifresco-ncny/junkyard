include(FetchContent)
# Tarball downloads (no git clone needed; works behind the sandbox proxy).
FetchContent_Declare(JUCE
  URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.17-security.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(Catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
