file(GLOB APP_SOURCES "*.c")
file(GLOB_RECURSE LIB_SOURCES "../lib/*.c")
idf_component_register(SRCS ${APP_SOURCES} ${LIB_SOURCES}
    INCLUDE_DIRS "../include" "../lib/Regelaar" "../lib/SetpointCurve"
    "../lib/PiProtocol" "../lib/V20Modbus" "../lib/WindtunnelHAL"
    REQUIRES driver esp_timer esp_eth esp_netif esp_event esp_system)
