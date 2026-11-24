vj_module(gfx
  SOURCES NullBackend.cpp OpenGL41Backend.cpp
  DEPS glfw)
if(APPLE)
  target_link_libraries(vj_gfx PUBLIC "-framework OpenGL")
  target_compile_definitions(vj_gfx PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(WIN32)
  # GLFW trae un cargador de OpenGL listo para usar: no hace falta otra dependencia.
  target_include_directories(vj_gfx PRIVATE ${glfw_SOURCE_DIR}/deps)
  target_link_libraries(vj_gfx PUBLIC opengl32)
endif()
