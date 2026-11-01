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
  src/core/box-smoother.cpp
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
  add_executable(label-format-test tests/label_format_test.cpp)
  target_link_libraries(label-format-test PRIVATE body-segmenter-core)
  enable_testing()
  add_test(NAME tracker COMMAND track-test)
  add_test(NAME label_format COMMAND label-format-test)
  add_executable(box-smooth-test tests/box_smooth_test.cpp)
  target_link_libraries(box-smooth-test PRIVATE body-segmenter-core)
  add_test(NAME box_smooth COMMAND box-smooth-test)
endif()

# --- OBS plugin -----------------------------------------------------------
# Only built when libobs is discoverable (i.e. on a machine with the OBS dev
# dependencies, e.g. via the obs-plugintemplate macOS preset). This Linux dev
# container builds with -DBUILD_PLUGIN=OFF.
if(BUILD_PLUGIN)
  find_package(libobs QUIET)

  # Local fallback (no libobs dev package): build against an already-installed
  # OBS.app. Pass -DLIBOBS_INCLUDE_DIR=<fetched libobs headers> and optionally
  # -DOBS_APP=<path to OBS.app> (defaults to /Applications/OBS.app). We link the
  # framework dylib directly; its install-name is @rpath/libobs.framework/...,
  # which binds to the libobs already loaded by OBS at runtime, so no extra
  # rpath is needed. Use headers matching your installed OBS version.
  if(NOT libobs_FOUND AND DEFINED LIBOBS_INCLUDE_DIR)
    if(NOT OBS_APP)
      set(OBS_APP "/Applications/OBS.app")
    endif()
    set(_libobs_dylib "${OBS_APP}/Contents/Frameworks/libobs.framework/Versions/A/libobs")
    if(EXISTS "${LIBOBS_INCLUDE_DIR}/obs-module.h" AND EXISTS "${_libobs_dylib}")
      message(STATUS "libobs dev package not found; building against installed OBS at ${OBS_APP}")
      add_library(OBS::libobs SHARED IMPORTED GLOBAL)
      set_target_properties(OBS::libobs PROPERTIES
        IMPORTED_LOCATION "${_libobs_dylib}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_INCLUDE_DIR}")
      set(libobs_FOUND TRUE)
    else()
      message(WARNING "LIBOBS_INCLUDE_DIR/OBS_APP set but headers or framework "
                      "not found (looked for ${LIBOBS_INCLUDE_DIR}/obs-module.h "
                      "and ${_libobs_dylib}).")
    endif()
  endif()

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
        # Bake the install rpath into the built binary so the bundle is
        # self-contained (resolves the bundled ONNX dylib in ../Frameworks)
        # regardless of where the build tree lives. The OBS.app Frameworks dir
        # is included as a belt-and-suspenders path for libobs, which otherwise
        # binds to the already-loaded image via its @rpath install-name.
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@loader_path/../Frameworks;/Applications/OBS.app/Contents/Frameworks")
      include(BundleOnnxruntimeMac)

      # Copy plugin data (effects, locale, models, font) into the bundle's
      # Resources so obs_module_file() resolves them at runtime.
      add_custom_command(TARGET obs-body-mask POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/data"
                "$<TARGET_BUNDLE_CONTENT_DIR:obs-body-mask>/Resources"
        COMMENT "Bundling plugin data (effects, locale, models, font)"
        VERBATIM)

      # `cmake --install <build-dir>` copies the .plugin into the OBS user
      # plugins dir and ad-hoc re-signs it. The re-sign is required because the
      # post-build data copy above invalidates the linker signature, which can
      # otherwise make OBS's dlopen fail on recent macOS. Override the location
      # with -DOBS_PLUGIN_INSTALL_DIR=... ; then restart OBS to load it.
      set(OBS_PLUGIN_INSTALL_DIR
          "$ENV{HOME}/Library/Application Support/obs-studio/plugins"
          CACHE PATH "Destination for 'cmake --install' of the .plugin bundle")
      install(CODE "
        set(_dst \"${OBS_PLUGIN_INSTALL_DIR}\")
        set(_bundle \"$<TARGET_BUNDLE_DIR:obs-body-mask>\")
        get_filename_component(_name \"\${_bundle}\" NAME)
        message(STATUS \"Installing \${_name} -> \${_dst}\")
        file(REMOVE_RECURSE \"\${_dst}/\${_name}\")
        file(MAKE_DIRECTORY \"\${_dst}\")
        file(COPY \"\${_bundle}\" DESTINATION \"\${_dst}\")
        execute_process(
          COMMAND codesign --force --deep --sign - \"\${_dst}/\${_name}\"
          RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
          message(FATAL_ERROR \"codesign failed (rc=\${_rc})\")
        endif()
        message(STATUS \"Installed \${_name} (ad-hoc signed). Restart OBS to load it.\")
      ")
    endif()
  endif()
endif()
