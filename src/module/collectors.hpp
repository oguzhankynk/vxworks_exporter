#ifndef VXEXP_MODULE_COLLECTORS_HPP
#define VXEXP_MODULE_COLLECTORS_HPP

#include "core/metric_writer.hpp"

namespace vxexp {

/// @brief Writes every metric family: system, memory, tasks, CPU time, RTPs,
///        network interfaces and the exporter's own metrics.
///
/// Serialised by an internal mutex, so concurrent callers (the server task
/// and vxExporterShow from the shell) take turns.
/// @param out Destination writer.
void collectAll(MetricWriter& out);

}  // namespace vxexp

#endif  // VXEXP_MODULE_COLLECTORS_HPP
