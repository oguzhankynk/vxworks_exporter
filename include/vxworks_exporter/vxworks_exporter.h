/**
 * @file vxworks_exporter.h
 * @brief C API of the VxWorks Prometheus exporter kernel module.
 *
 * Load the module and start the exporter from the kernel shell:
 *
 * @code
 * -> ld < vxworks_exporter.out
 * -> vxExporterStart 0, 9100
 * @endcode
 *
 * or from kernel code, e.g. usrAppInit():
 *
 * @code
 * vxExporterStart (NULL, 0);
 * @endcode
 *
 * Prometheus then scrapes http://&lt;target&gt;:9100/metrics.
 */

#ifndef VXWORKS_EXPORTER_VXWORKS_EXPORTER_H
#define VXWORKS_EXPORTER_VXWORKS_EXPORTER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts the exporter: CPU accounting and the HTTP server task.
 * @param bindAddr IPv4 address to listen on, or NULL / "" for all interfaces.
 * @param port TCP port, or 0 for the default (9100).
 * @return 0 (OK) on success; -1 (ERROR) if the exporter is already running,
 *         the address is invalid, the port cannot be bound, or a kernel hook
 *         cannot be installed. A message on the console gives the reason.
 */
int vxExporterStart(const char* bindAddr, int port);

/**
 * @brief Stops the exporter: closes the socket, ends the server task and
 *        removes the kernel hooks. The module can be unloaded afterwards.
 * @return 0 (OK) on success; -1 (ERROR) if the exporter is not running.
 */
int vxExporterStop(void);

/**
 * @brief Prints the current metrics to the console, in the same format as
 *        /metrics. Works whether or not the exporter is running; CPU time
 *        metrics appear only while it runs.
 */
void vxExporterShow(void);

/**
 * @brief Whether to export one series per task (1, the default) or only the
 *        aggregate series (0). Systems that create many short-lived tasks can
 *        set it to 0 to keep the number of series low. Read at every scrape,
 *        so it can be changed from the shell at any time.
 */
extern int vxExporterPerTask;

/**
 * @brief Priority of the server task created by the next vxExporterStart()
 *        (default 250). The low default keeps scrapes from delaying the
 *        application.
 */
extern int vxExporterPriority;

#ifdef __cplusplus
}
#endif

#endif /* VXWORKS_EXPORTER_VXWORKS_EXPORTER_H */
