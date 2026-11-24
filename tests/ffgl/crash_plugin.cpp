#include <cstdlib>

// Exporta plugMain para que parezca un plugin, y se cae en cuanto se carga.
extern "C" void* plugMain(unsigned, void*, void*) { return nullptr; }
__attribute__((constructor)) static void crash() { std::abort(); }
