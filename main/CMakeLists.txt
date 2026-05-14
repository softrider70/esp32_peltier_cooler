idf_component_register(
    SRCS "main.c" "sensor.c" "peltier.c" "fan.c"
         "wifi.c" "webserver.c" "scheduler.c" "nvs_config.c" "ota.c"
         "data_logger.c" "task_monitor.c" "energy_tracker.c"
    INCLUDE_DIRS "."
    EMBED_FILES "index.html" "captive.html"
)
