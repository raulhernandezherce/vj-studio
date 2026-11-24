#pragma once
#include <cstdint>

// Parte del ABI de FFGL 2.x que usa el host, según FFGL.h del SDK oficial de Resolume.
// Se declara aquí para que compilar el programa no dependa del SDK.
namespace vj::ffgl::abi {

union Mixed {
  uint32_t UIntValue;
  void* PointerValue;
};
using InstanceId = void*;
using MainFn = Mixed (*)(uint32_t, Mixed, InstanceId);

inline constexpr uint32_t kGetInfo = 0, kInitialise = 34, kDeinitialise = 2, kGetNumParameters = 4, kGetParameterName = 5,
                          kGetParameterDefault = 6, kSetParameter = 8, kGetPluginCaps = 10, kGetParameterType = 15,
                          kProcessOpenGL = 17, kInstantiateGL = 18, kDeinstantiateGL = 19, kSetTime = 20, kConnect = 21,
                          kDisconnect = 22, kResize = 23, kSetBeatInfo = 38, kSetHostInfo = 39, kGetRange = 41;
inline constexpr uint32_t kSuccess = 0, kFail = 0xFFFFFFFF;
inline constexpr uint32_t kEffect = 0, kSource = 1, kMixer = 2;
inline constexpr uint32_t kCapSetTime = 5, kCapMinInputs = 10;
inline constexpr uint32_t kTypeBoolean = 0, kTypeEvent = 1, kTypeText = 100, kTypeFile = 14, kTypeBuffer = 12;

struct PluginInfo {
  uint32_t APIMajorVersion, APIMinorVersion;
  char PluginUniqueID[4];
  char PluginName[16];
  uint32_t PluginType;
};
struct SetParameter {
  uint32_t ParameterNumber;
  Mixed NewParameterValue;
};
struct SetBeatInfo {
  float bpm, barPhase;
};
struct SetHostInfo {
  const char* name;
  const char* version;
};
struct Range {
  float min, max;
};
struct GetRange {
  uint32_t parameterNumber;
  Range range;
};
struct Viewport {
  uint32_t x, y, width, height;
};
struct Texture {
  uint32_t Width, Height, HardwareWidth, HardwareHeight;
  uint32_t Handle;
};
struct ProcessOpenGL {
  uint32_t numInputTextures;
  Texture** inputTextures;
  uint32_t HostFBO;
};

}  // namespace vj::ffgl::abi
