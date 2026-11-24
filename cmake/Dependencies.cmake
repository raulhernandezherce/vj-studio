include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(GLFW_INSTALL OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json glfw)

if(VJ_BUILD_TESTS)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(INSTALL_GTEST OFF CACHE INTERNAL "")
  set(gtest_force_shared_crt ON CACHE INTERNAL "")
  # SDK oficial, solo para compilar plugins reales con los que probar el host.
  FetchContent_Declare(ffgl_sdk
    GIT_REPOSITORY https://github.com/resolume/ffgl.git
    GIT_TAG 5b6e07840277df9ec35a228b9eb067636316350f)
  set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(googletest ffgl_sdk)
endif()
