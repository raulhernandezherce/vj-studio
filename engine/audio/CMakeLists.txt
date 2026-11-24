if(WIN32)
  vj_module(audio
    SOURCES ../platform-win/WinAudio.cpp AudioEngine.cpp AudioEffects.cpp Fft.cpp
    DEPS vj_core)
  target_link_libraries(vj_audio PUBLIC ole32 mfplat mfreadwrite mfuuid)
else()
  vj_module(audio
    SOURCES AudioData.mm AudioEngine.cpp AudioEffects.cpp CoreAudioDevice.cpp Fft.cpp
    DEPS vj_core)
  target_link_libraries(vj_audio PUBLIC "-framework AVFoundation" "-framework CoreMedia" "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit" "-framework Foundation" "-framework CoreFoundation")
  set_source_files_properties(AudioData.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-Wno-deprecated-declarations")
endif()
