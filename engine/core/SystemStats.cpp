#include "core/SystemStats.h"

#include <chrono>
#include <cstdint>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#endif

namespace vj {
namespace {

#if defined(__APPLE__)
double residentMb() {
  task_vm_info_data_t info;
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
  return static_cast<double>(info.phys_footprint) / (1024.0 * 1024.0);  // lo mismo que el Monitor de Actividad
}

double gpuPercent() {
  io_iterator_t it = 0;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it) != KERN_SUCCESS) return -1;
  double out = -1;
  while (io_object_t service = IOIteratorNext(it)) {
    CFMutableDictionaryRef props = nullptr;
    if (IORegistryEntryCreateCFProperties(service, &props, kCFAllocatorDefault, 0) == KERN_SUCCESS && props) {
      const auto stats = static_cast<CFDictionaryRef>(CFDictionaryGetValue(props, CFSTR("PerformanceStatistics")));
      if (stats) {
        const auto value = static_cast<CFNumberRef>(CFDictionaryGetValue(stats, CFSTR("Device Utilization %")));
        int percent = 0;
        if (value && CFNumberGetValue(value, kCFNumberIntType, &percent)) out = percent;
      }
      CFRelease(props);
    }
    IOObjectRelease(service);
  }
  IOObjectRelease(it);
  return out;
}
#elif defined(_WIN32)
double residentMb() {
  PROCESS_MEMORY_COUNTERS c{};
  return GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof c) ? static_cast<double>(c.WorkingSetSize) / (1024.0 * 1024.0) : 0;
}
double gpuPercent() { return -1; }
#else
double residentMb() { return 0; }
double gpuPercent() { return -1; }
#endif

}  // namespace

SystemStats SystemSampler::sample() {
#if defined(_WIN32)
  FILETIME created, ended, kernel, user;
  GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user);
  const auto ticks = [](const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32 | f.dwLowDateTime) / 1e7; };
  const double cpu = ticks(kernel) + ticks(user);
#else
  rusage u{};
  getrusage(RUSAGE_SELF, &u);
  const double cpu = u.ru_utime.tv_sec + u.ru_utime.tv_usec / 1e6 + u.ru_stime.tv_sec + u.ru_stime.tv_usec / 1e6;
#endif
  const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  SystemStats s;
  if (lastCpu_ >= 0 && wall > lastWall_) s.cpu = (cpu - lastCpu_) / (wall - lastWall_) * 100;
  lastCpu_ = cpu;
  lastWall_ = wall;
  s.memoryMb = residentMb();
  s.gpu = gpuPercent();
  return s;
}

}  // namespace vj
