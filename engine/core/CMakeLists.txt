vj_module(core
  SOURCES SystemStats.cpp ParameterRegistry.cpp EventBus.cpp CommandBus.cpp ValueJson.cpp ModuleRegistry.cpp
  DEPS nlohmann_json::nlohmann_json)
if(APPLE)
  target_link_libraries(vj_core PUBLIC "-framework IOKit" "-framework CoreFoundation")
elseif(WIN32)
  target_link_libraries(vj_core PUBLIC ws2_32 psapi)
endif()
