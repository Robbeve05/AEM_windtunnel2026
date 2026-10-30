# Author: R.E. van Eekelen
file(GLOB APP_SOURCES "*.c")
idf_component_register(SRCS ${APP_SOURCES} INCLUDE_DIRS "../include"
    REQUIRES driver esp_timer esp_eth esp_netif esp_event esp_system app_update mbedtls)
