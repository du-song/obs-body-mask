cmake_minimum_required(VERSION 3.28)

project(obs-body-mask VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_CLI "Build the standalone body-mask-cli test tool" ON)
option(BUILD_PLUGIN "Build the OBS Studio plugin (requires libobs)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- ONNX Runtime (prebuilt, with CoreML EP on macOS) ---------------------
include(FetchOnnxruntime)

# --- Shared, OBS-free inference + compositing core ------------------------
add_library(body-segmenter-core STATIC
  src/ort-utils/ort-session-utils.cpp
  src/core/body-segmenter.cpp
  src/core/pose-detector.cpp
  src/core/pose-tracker.cpp
  src/core/compositor.cpp
  src/core/text-raster.cpp
  src/core/image-io.cpp)
target_include_directories(body-segmenter-core PUBLIC src)
target_link_libraries(body-segmenter-core PUBLIC Ort::onnxruntime)

# --- CLI test tool (Linux + macOS) ----------------------------------------
if(BUILD_CLI)
  add_executable(body-mask-cli tools/body-mask-cli/main.cpp)
  target_link_libraries(body-mask-cli PRIVATE body-segmenter-core)

  add_executable(track-test tests/track_test.cpp)
  target_link_libraries(track-test PRIVATE body-segmenter-core)
  enable_testing()
  add_test(NAME tracker COMMAND track-test)
endif()

# --- OBS plugin -----------------------------------------------------------
# Only built when libobs is discoverable (i.e. on a machine with the OBS dev
# dependencies, e.g. via the obs-plugintemplate macOS preset). This Linux dev
# container builds with -DBUILD_PLUGIN=OFF.
if(BUILD_PLUGIN)
  find_package(libobs QUIET)
  if(NOT libobs_FOUND)
    message(WARNING "libobs not found - skipping OBS plugin target. "
                    "Configure with -DBUILD_PLUGIN=OFF to silence, or use the "
                    "macOS preset with OBS dependencies to build the plugin.")
  else()
    add_library(obs-body-mask MODULE
      src/plugin-main.c
      src/body-mask-filter.cpp
      src/obs-utils/obs-utils.cpp)
    target_link_libraries(obs-body-mask PRIVATE OBS::libobs body-segmenter-core)
    target_include_directories(obs-body-mask PRIVATE src)
    set_target_properties(obs-body-mask PROPERTIES PREFIX "")

    if(APPLE)
      set_target_properties(obs-body-mask PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "plugin"
        INSTALL_RPATH "@loader_path/../Frameworks")
      include(BundleOnnxruntimeMac)
    endif()
  endif()
endif()
