#pragma once

namespace vj {

// Consumo del proceso del motor. cpu en % de un núcleo; gpu en % del dispositivo, -1 si no se sabe.
struct SystemStats {
  double cpu = 0, memoryMb = 0, gpu = -1;
};

class SystemSampler {
 public:
  SystemStats sample();

 private:
  double lastCpu_ = -1, lastWall_ = 0;
};

}  // namespace vj
