cmake_minimum_required(VERSION 3.25)
project(vjstudio VERSION 0.10.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(APPLE)
  enable_language(OBJC OBJCXX)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "")
endif()

if(MSVC)
  add_compile_options(/utf-8 /bigobj)
  add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

option(VJ_BUILD_TESTS "Compilar las pruebas" ON)

include(cmake/Dependencies.cmake)
include(cmake/VjModule.cmake)

# Cada hito añade aquí su carpeta. El ejecutable vjengine las enlaza todas.
add_subdirectory(engine/core)
add_subdirectory(engine/gfx)
add_subdirectory(engine/media)
add_subdirectory(engine/audio)
add_subdirectory(engine/compositor)
add_subdirectory(engine/animation)
add_subdirectory(engine/control)
add_subdirectory(engine/project)
if(NOT WIN32)
  add_subdirectory(engine/ffgl)
endif()
add_subdirectory(engine/shortcuts)
add_subdirectory(engine/output)
add_subdirectory(engine/patch)
add_subdirectory(engine/dmx)
add_subdirectory(engine/app)

if(VJ_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Una sola orden para el programa instalable: cmake --build build --target app
find_program(NPM npm REQUIRED)
add_custom_target(app
  COMMAND ${NPM} run dist
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/ui
  DEPENDS vjengine
  USES_TERMINAL
  COMMENT "Empaquetando interfaz y motor en una sola aplicación")
