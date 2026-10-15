#include "TaskData.h"
#include "AppConfig.h"
#include "PiProtocol.h"
#include "Regelaar.h"
#include "freertos/task.h"
#include "esp_err.h"

void ControlTask(void *argument)
{
    (void)argument;
    Regelaar pid;
    ESP_ERROR_CHECK(Regelaar_Init(&pid, DEFAULT_KP, DEFAULT_KI, DEFAULT_KD,
        CONTROL_PERIOD_MS/1000.0f, 0, MAX_OUTPUT, 0.05f) ? ESP_OK : ESP_FAIL);
    TickType_t last = xTaskGetTickCount(), last_command = last;
    Telemetry t = {.reason="STOPPED"};
    for (;;) {
        Sample s = {0}; MotorStatus motor = {0};
        xQueuePeek(sensors, &s, 0);
        xQueuePeek(motor_status, &motor, 0);
        TickType_t now = xTaskGetTickCount();
        t.measurement = s.value;
        t.sensor_ok = s.valid && (TickType_t)(now-s.tick) < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS);
        t.motor_ok = motor.ready && motor.ok && (TickType_t)(now-motor.tick) < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS);
        bool stop = (xEventGroupWaitBits(events, STOP_REQUEST, pdTRUE, pdFALSE, 0) & STOP_REQUEST) != 0;
        PiCommand c;
        /* Begrensde verwerking: een volle commandostroom mag de regeling niet blokkeren. */
        for (int i=0; i<16 && xQueueReceive(commands, &c, 0)==pdTRUE; ++i) {
            last_command = now;
            switch (c.type) {
            case CMD_START:
                if (!stop && t.sensor_ok && t.motor_ok) { t.running=true; t.reason="RUNNING"; }
                else t.reason="NOT_READY";
                break;
            case CMD_SETPOINT: t.setpoint=c.values[0]; break;
            case CMD_PID:
                if (!t.running) Regelaar_Init(&pid, c.values[0], c.values[1], c.values[2],
                    CONTROL_PERIOD_MS/1000.0f, 0, MAX_OUTPUT, 0.05f);
                else t.reason="PID_REQUIRES_STOP";
                break;
            case CMD_STOP: stop=true; break;
            case CMD_PING: break;
            }
        }
        if (stop) { t.running=false; t.reason="STOPPED"; }
        else if (!t.sensor_ok) { t.running=false; t.reason="SENSOR_FAULT"; }
        else if (!t.motor_ok) { t.running=false; t.reason="MOTOR_FAULT"; }
        else if ((TickType_t)(now-last_command) >= pdMS_TO_TICKS(COMMAND_TIMEOUT_MS)) {
            t.running=false; t.reason="COMMAND_TIMEOUT";
        }
        t.output=0;
        if (t.running && !Regelaar_Step(&pid, t.setpoint, s.value, &t.output)) {
            t.running=false; t.reason="PID_FAULT";
        }
        if (!t.running) { t.output=0; Regelaar_Reset(&pid); }
        MotorRequest request = {.output=t.output, .tick=now};
        xQueueOverwrite(motor_requests, &request);
        xQueueOverwrite(telemetry, &t);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}
