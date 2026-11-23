// The kernel module's link unit. Everything lives in vxexp_module; this file
// pulls in the C API so the partial link keeps it.
//
//   -> ld < vxworks_exporter.out
//   -> vxExporterStart 0, 9100

#include "vxworks_exporter/vxworks_exporter.h"

// Referencing the entry points keeps them (and what they use) in the module.
extern "C" void* const vxExporterEntryPoints[] = {
    reinterpret_cast<void*>(&vxExporterStart),
    reinterpret_cast<void*>(&vxExporterStop),
    reinterpret_cast<void*>(&vxExporterShow),
};
