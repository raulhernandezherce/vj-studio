set(sources
  unit/parameter_registry_test.cpp
  unit/event_bus_test.cpp
  unit/module_registry_test.cpp
  unit/schema_test.cpp
  unit/ws_codec_test.cpp
  unit/api_dispatcher_test.cpp
  unit/generator_test.cpp
  unit/clip_clock_test.cpp
  unit/transform_test.cpp
  unit/composition_test.cpp
  unit/project_test.cpp
  unit/audio_test.cpp
  unit/animation_test.cpp
  unit/shortcuts_test.cpp
  unit/screen_setup_test.cpp
  protocol/osc_test.cpp
  protocol/rest_test.cpp
  protocol/control_server_test.cpp
  golden/gl_backend_test.cpp
  golden/compositor_test.cpp
  golden/effects_test.cpp
  golden/screens_test.cpp
  golden/recording_test.cpp
  golden/patch_test.cpp
  golden/dmx_test.cpp)
if(NOT WIN32)
  list(APPEND sources golden/ffgl_test.cpp)
endif()
if(APPLE)
  list(APPEND sources unit/fixture_video.mm unit/media_avf_test.cpp)
endif()
add_executable(vj_tests ${sources})
target_include_directories(vj_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vj_tests PRIVATE VJ_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/refs")
if(APPLE)
  target_link_libraries(vj_tests PRIVATE "-framework UniformTypeIdentifiers")
endif()
target_link_libraries(vj_tests PRIVATE vj_dmx vj_patch vj_core vj_gfx vj_media vj_compositor vj_control vj_animation vj_project vj_shortcuts vj_output GTest::gtest_main)
if(NOT WIN32)
  target_link_libraries(vj_tests PRIVATE vj_ffgl)
endif()
if(APPLE)
  set_source_files_properties(unit/fixture_video.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-Wno-deprecated-declarations")
endif()

if(NOT WIN32)
  add_subdirectory(ffgl)
  add_dependencies(vj_tests vj_ffgl_plugins)
  target_compile_definitions(vj_tests PRIVATE VJ_FFGL_DIR="${VJ_FFGL_PLUGIN_DIR}" VJ_ENGINE_EXE="$<TARGET_FILE:vjengine>")
endif()
add_test(NAME engine_unit COMMAND vj_tests)
if(APPLE)
  add_executable(vj_make_fixture tools/make_fixture.cpp unit/fixture_video.mm)
  target_link_libraries(vj_make_fixture PRIVATE "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework Foundation" "-framework ImageIO" "-framework CoreGraphics" "-framework UniformTypeIdentifiers")
endif()
find_program(NODE node)
if(NODE AND APPLE)
  add_test(NAME engine_e2e COMMAND ${NODE} ${CMAKE_CURRENT_SOURCE_DIR}/protocol/e2e.mjs
           $<TARGET_FILE:vjengine> $<TARGET_FILE:vj_make_fixture> ${VJ_FFGL_PLUGIN_DIR})
endif()
