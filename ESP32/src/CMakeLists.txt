# Author: R.E. van Eekelen
idf_component_register(
    SRCS
        ApplicationTasks.c
        ControlTask.c
        DigitalOutputs.c
        Ethernet.c
        FanCalibration.c
        FrequencyModel.c
        HeartbeatTask.c
        I2CBus.c
        InputHandlerTask.c
        main.c
        OtaUpdate.c
        PiCommunicationTask.c
        PiProtocol.c
        PressureSensors.c
        Regelaar.c
        SafetyIO.c
        SensorTask.c
        SetpointCurve.c
        StateMachine.c
        StatusIndicators.c
        TorqueModel.c
        V20Modbus.c
    INCLUDE_DIRS "../include"
    REQUIRES nvs_flash driver esp_timer esp_eth esp_netif esp_event esp_system app_update mbedtls)
