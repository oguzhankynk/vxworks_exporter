#!/usr/bin/env python3
"""Generates demo/grafana/dashboards/vxworks.json (the VxWorks dashboard).

Edit the panels here and rerun: python3 demo/grafana/make_dashboard.py
"""

import json
import os

DS = {"type": "prometheus", "uid": "prometheus"}
SEL = 'instance=~"$instance"'
next_id = 0


def target(expr, legend="", instant=False, ref="A"):
    t = {"datasource": DS, "expr": expr, "legendFormat": legend, "refId": ref}
    if instant:
        t.update({"instant": True, "range": False, "format": "table"})
    return t


def panel(kind, title, x, y, w, h, targets, unit=None, **extra):
    global next_id
    next_id += 1
    p = {
        "id": next_id, "type": kind, "title": title, "datasource": DS,
        "gridPos": {"x": x, "y": y, "w": w, "h": h}, "targets": targets,
        "fieldConfig": {"defaults": {}, "overrides": []}, "options": {},
    }
    if unit:
        p["fieldConfig"]["defaults"]["unit"] = unit
    for key, value in extra.items():
        if key == "defaults":
            p["fieldConfig"]["defaults"].update(value)
        elif key == "options":
            p["options"].update(value)
        else:
            p[key] = value
    return p


def stat(title, x, expr, unit, decimals=None):
    defaults = {"color": {"mode": "thresholds"},
                "thresholds": {"mode": "absolute", "steps": [{"color": "blue", "value": None}]}}
    if decimals is not None:
        defaults["decimals"] = decimals
    return panel("stat", title, x, 0, 4, 4, [target(expr)], unit, defaults=defaults,
                 options={"colorMode": "background", "graphMode": "area",
                          "reduceOptions": {"calcs": ["lastNotNull"]}})


def timeseries(title, x, y, w, h, targets, unit, stacked=False, maximum=None):
    custom = {"fillOpacity": 30 if stacked else 10, "lineWidth": 1, "showPoints": "never"}
    if stacked:
        custom["stacking"] = {"mode": "normal", "group": "A"}
    defaults = {"custom": custom}
    if maximum is not None:
        defaults["max"] = maximum
        defaults["min"] = 0
    return panel("timeseries", title, x, y, w, h, targets, unit, defaults=defaults,
                 options={"legend": {"displayMode": "table", "placement": "right",
                                     "calcs": ["mean", "max"], "sortBy": "Mean", "sortDesc": True},
                          "tooltip": {"mode": "multi", "sort": "desc"}})


panels = [
    stat("Uptime", 0, f'vxworks_uptime_seconds{{{SEL}}}', "s"),
    stat("CPU busy", 4,
         f'1 - sum(rate(vxworks_cpu_idle_seconds_total{{{SEL}}}[30s])) / sum(vxworks_cpus{{{SEL}}})',
         "percentunit", 1),
    stat("Tasks", 8, f'sum(vxworks_tasks{{{SEL}}})', "short"),
    stat("RTPs", 12, f'vxworks_rtps{{{SEL}}}', "short"),
    stat("Kernel heap free", 16, f'vxworks_kernel_heap_free_bytes{{{SEL}}}', "bytes", 1),
    stat("Scrape duration", 20, f'vxexporter_scrape_duration_seconds{{{SEL}}}', "s", 2),

    timeseries("CPU by task", 0, 4, 16, 10, [target(
        f'sum by (task) (rate(vxworks_task_cpu_seconds_total{{{SEL},task!~"tIdleTask.*"}}[30s]))',
        "{{task}}")], "percentunit", stacked=True),
    timeseries("Tasks by state", 16, 4, 8, 10, [target(
        f'sum by (state) (vxworks_tasks{{{SEL}}}) > 0', "{{state}}")], "short", stacked=True),

    timeseries("Kernel heap", 0, 14, 12, 8, [
        target(f'vxworks_kernel_heap_allocated_bytes{{{SEL}}}', "allocated", ref="A"),
        target(f'vxworks_kernel_heap_free_bytes{{{SEL}}}', "free", ref="B"),
        target(f'vxworks_kernel_heap_largest_free_block_bytes{{{SEL}}}', "largest free block",
               ref="C"),
    ], "bytes"),
    timeseries("Network throughput", 12, 14, 12, 8, [
        target(f'rate(vxworks_network_receive_bytes_total{{{SEL}}}[30s])', "{{device}} rx",
               ref="A"),
        target(f'-rate(vxworks_network_transmit_bytes_total{{{SEL}}}[30s])', "{{device}} tx",
               ref="B"),
    ], "Bps"),

    panel("bargauge", "Stack high-water mark (top 12, share of stack used)", 0, 22, 12, 10, [{
        **target(
            f'topk(12, max by (task) (vxworks_task_stack_used_max_bytes{{{SEL}}} '
            f'/ vxworks_task_stack_size_bytes{{{SEL}}}))', "{{task}}"),
        "instant": True, "range": False}],
        "percentunit",
        defaults={"min": 0, "max": 1, "thresholds": {"mode": "absolute", "steps": [
            {"color": "green", "value": None}, {"color": "orange", "value": 0.6},
            {"color": "red", "value": 0.85}]}},
        options={"displayMode": "gradient", "orientation": "horizontal", "showUnfilled": True,
                 "valueMode": "color", "namePlacement": "left", "sizing": "auto",
                 "reduceOptions": {"calcs": ["lastNotNull"], "values": False}}),
    panel("table", "Tasks", 12, 22, 12, 10, [
        target(f'max by (task, rtp) (vxworks_task_priority{{{SEL}}})', instant=True, ref="A"),
        target(f'sum by (task, rtp) (rate(vxworks_task_cpu_seconds_total{{{SEL}}}[1m]))',
               instant=True, ref="B"),
    ], None,
        transformations=[
            {"id": "merge", "options": {}},
            {"id": "organize", "options": {
                "excludeByName": {"Time": True},
                "renameByName": {"Value #A": "Priority", "Value #B": "CPU", "task": "Task",
                                 "rtp": "RTP"}}},
            {"id": "sortBy", "options": {"sort": [{"field": "CPU", "desc": True}]}},
        ],
        defaults={"custom": {"align": "auto"}},
        fieldConfig={"defaults": {"custom": {"align": "auto"}}, "overrides": [
            {"matcher": {"id": "byName", "options": "CPU"}, "properties": [
                {"id": "unit", "value": "percentunit"},
                {"id": "custom.cellOptions", "value": {"type": "gauge", "mode": "basic"}},
                {"id": "max", "value": 1}]}]}),
]

dashboard = {
    "uid": "vxworks-exporter",
    "title": "VxWorks",
    "tags": ["vxworks", "rtos"],
    "timezone": "browser",
    "schemaVersion": 39,
    "refresh": "5s",
    "time": {"from": "now-15m", "to": "now"},
    "templating": {"list": [{
        "name": "instance", "label": "Target", "type": "query", "datasource": DS,
        "query": {"query": "label_values(vxworks_build_info, instance)", "refId": "instance"},
        "refresh": 2, "includeAll": True, "multi": False, "current": {},
    }]},
    "panels": panels,
}

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dashboards", "vxworks.json")
with open(out, "w", encoding="utf-8") as f:
    json.dump(dashboard, f, indent=2)
    f.write("\n")
print(f"wrote {out}")
