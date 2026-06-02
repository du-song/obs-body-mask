# Downloads a prebuilt ONNX Runtime release and exposes it as the imported
# target `Ort::onnxruntime`. macOS arm64 builds ship the CoreML execution
# provider; the Linux x64 build is used for the standalone CLI / CI on Linux.

include(FetchContent)

set(ORT_VERSION "1.20.1" CACHE STRING "ONNX Runtime version to fetch")

if(APPLE)
  set(_ort_pkg "onnxruntime-osx-arm64-${ORT_VERSION}")
  set(_ort_sha "b678fc3c2354c771fea4fba420edeccfba205140088334df801e7fc40e83a57a")
  set(_ort_libname "libonnxruntime.${ORT_VERSION}.dylib")
elseif(UNIX)
  set(_ort_pkg "onnxruntime-linux-x64-${ORT_VERSION}")
  set(_ort_sha "67db4dc1561f1e3fd42e619575c82c601ef89849afc7ea85a003abbac1a1a105")
  set(_ort_libname "libonnxruntime.so.${ORT_VERSION}")
else()
  message(FATAL_ERROR "FetchOnnxruntime: unsupported platform (macOS arm64 / Linux x64 only)")
endif()

FetchContent_Declare(
  onnxruntime
  URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${_ort_pkg}.tgz"
  URL_HASH "SHA256=${_ort_sha}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(onnxruntime)

set(ORT_ROOT "${onnxruntime_SOURCE_DIR}")
set(ORT_LIB "${ORT_ROOT}/lib/${_ort_libname}")

add_library(Ort::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(Ort::onnxruntime PROPERTIES
  IMPORTED_LOCATION "${ORT_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${ORT_ROOT}/include")

if(APPLE)
  # Used by the plugin packaging step to copy + fix-up the dylib into the bundle.
  set(ORT_DYLIB "${ORT_LIB}" CACHE FILEPATH "Path to bundled ONNX Runtime dylib")
endif()
