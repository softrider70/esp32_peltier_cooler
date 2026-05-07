idf_component_register(
    SRCS "main.c" "sensor.c" "pid.c" "peltier.c" "fan.c"
         "wifi.c" "webserver.c" "scheduler.c" "nvs_config.c" "ota.c"
         "cyd_display.c"
    INCLUDE_DIRS "."
    EMBED_FILES "index.html" "captive.html"
)
