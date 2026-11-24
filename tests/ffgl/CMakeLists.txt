# Plugins reales del SDK oficial, compilados como bundles, igual que los de terceros.
set(VJ_FFGL_PLUGIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/plugins CACHE INTERNAL "")
set(src ${ffgl_sdk_SOURCE_DIR}/source/plugins)

function(vj_ffgl_plugin name)
  add_library(${name} MODULE ${ARGN})
  set_target_properties(${name} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION bundle
    LIBRARY_OUTPUT_DIRECTORY ${VJ_FFGL_PLUGIN_DIR} CXX_VISIBILITY_PRESET default)
  target_link_libraries(${name} PRIVATE ffgl-sdk "-framework OpenGL" "-framework CoreFoundation")
  target_link_options(${name} PRIVATE "LINKER:-u,_plugMain")
  target_compile_options(${name} PRIVATE -w)
endfunction()

vj_ffgl_plugin(AddSubtract ${src}/AddSubtract/AddSubtract.cpp)
vj_ffgl_plugin(Gradients ${src}/Gradients/FFGLGradients.cpp)
vj_ffgl_plugin(Particles ${src}/Particles/Particles.cpp ${src}/Particles/GLResources.cpp ${src}/Particles/Constants.cpp)
vj_ffgl_plugin(Events ${src}/Events/FFGLEvents.cpp)
target_compile_options(ffgl-sdk PRIVATE -w)
target_compile_definitions(ffgl-sdk PUBLIC GL_SILENCE_DEPRECATION)

# Plugin hostil: se cae al cargarlo. El motor debe sobrevivir.
add_library(Crash MODULE crash_plugin.cpp)
set_target_properties(Crash PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION bundle LIBRARY_OUTPUT_DIRECTORY ${VJ_FFGL_PLUGIN_DIR})

add_custom_target(vj_ffgl_plugins DEPENDS AddSubtract Gradients Particles Events Crash)
