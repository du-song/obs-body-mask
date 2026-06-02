# Copies the ONNX Runtime dylib into the plugin bundle's Frameworks dir and
# rewrites install names so the loader resolves it via @rpath. Ad-hoc signs the
# dylib for local development; CI / distribution should re-sign with the
# project's Developer ID and notarize.

if(NOT APPLE OR NOT TARGET obs-body-mask)
  return()
endif()

set(_fw "$<TARGET_BUNDLE_CONTENT_DIR:obs-body-mask>/Frameworks")
get_filename_component(_ortname "${ORT_DYLIB}" NAME)

add_custom_command(TARGET obs-body-mask POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_fw}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_DYLIB}" "${_fw}/${_ortname}"
  COMMAND install_name_tool -id "@rpath/${_ortname}" "${_fw}/${_ortname}"
  COMMAND install_name_tool -change "@rpath/${_ortname}" "@rpath/${_ortname}"
          "$<TARGET_FILE:obs-body-mask>"
  COMMAND codesign --force --sign - "${_fw}/${_ortname}"
  COMMENT "Bundling + fixing up ONNX Runtime dylib"
  VERBATIM)
