///////////////////////////////////////////////////////////////////////////////
// File information

/* Author: R.E. van Eekelen. These interrupts do not implement hardware safety.
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes

#include "freertos/FreeRTOS.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "AppConfig.h"
#include "ApplicationTasks.h"
#include "SafetyIO.h"

///////////////////////////////////////////////////////////////////////////////
// File-local variables

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool emergency_latch, relay_latch, stop_latch;
static volatile uint32_t unsafe_generation;
#if SIMULATION_MODE
static SafetySnapshot simulated = {
    .valid = true, .commissioned = true, .relay_ok = true, .stop_ok = true, .outputs_ok = true};
#endif

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static bool valid_active_level(int level);
#if !SIMULATION_MODE
static int input_level(const SafetySnapshot *snapshot, unsigned gpio);
#endif
#if !SIMULATION_MODE
static void input_isr(void *arg);
#endif

///////////////////////////////////////////////////////////////////////////////
// Initialization

///////////////////////////////////////////////////////////////////////////////
// SafetyIO_Init()

void SafetyIO_Init(void)
{
    /* Central installation precedes both input handlers and Ethernet startup. */
    esp_err_t result = gpio_install_isr_service(0);
    ESP_ERROR_CHECK(result == ESP_ERR_INVALID_STATE ? ESP_OK : result);
#if !SIMULATION_MODE
    gpio_config_t c = {
        .pin_bit_mask = 0xffULL << 4, .mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&c));
    const int pins[] = {SAFETY_RELAY_OK_GPIO, ESTOP_PRESSED_GPIO, STOP_BUTTON_OK_GPIO};
    for (unsigned i = 0; i < 3; i++)
    {
        ESP_ERROR_CHECK(gpio_set_intr_type(pins[i], GPIO_INTR_ANYEDGE));
        ESP_ERROR_CHECK(gpio_isr_handler_add(pins[i], input_isr, NULL));
    }
    (void)SafetyIO_Read();
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// valid_active_level()

static bool valid_active_level(int level)
{
    return level == 0 || level == 1;
}

#if !SIMULATION_MODE

///////////////////////////////////////////////////////////////////////////////
// input_level()

static int input_level(const SafetySnapshot *snapshot, unsigned gpio)
{
    return (int)((snapshot->raw_levels >> (gpio - 4u)) & 1u);
}
#endif

///////////////////////////////////////////////////////////////////////////////
// SafetyIO_InterpretLevel()

bool SafetyIO_InterpretLevel(int raw_level, int active_level, bool *active)
{
    if (!active || (raw_level != 0 && raw_level != 1) || !valid_active_level(active_level))
        return false;
    *active = raw_level == active_level;
    return true;
}

#if !SIMULATION_MODE

///////////////////////////////////////////////////////////////////////////////
// input_isr()

static void IRAM_ATTR input_isr(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&lock);
    if (valid_active_level(ESTOP_PRESSED_ACTIVE_LEVEL) &&
        gpio_get_level(ESTOP_PRESSED_GPIO) == ESTOP_PRESSED_ACTIVE_LEVEL)
    {
        emergency_latch = true;
        unsafe_generation++;
    }
    if (valid_active_level(SAFETY_RELAY_OK_ACTIVE_LEVEL) &&
        gpio_get_level(SAFETY_RELAY_OK_GPIO) != SAFETY_RELAY_OK_ACTIVE_LEVEL)
    {
        relay_latch = true;
        unsafe_generation++;
    }
    if (valid_active_level(STOP_BUTTON_OK_ACTIVE_LEVEL) &&
        gpio_get_level(STOP_BUTTON_OK_GPIO) != STOP_BUTTON_OK_ACTIVE_LEVEL)
        stop_latch = true;
    portEXIT_CRITICAL_ISR(&lock);
}
#endif

///////////////////////////////////////////////////////////////////////////////
// SafetyIO_Read()

SafetySnapshot SafetyIO_Read(void)
{
    SafetySnapshot s = {0};
#if SIMULATION_MODE
    portENTER_CRITICAL(&lock);
    s = simulated;
    portEXIT_CRITICAL(&lock);
#else
    for (int i = 0; i < 8; i++)
        s.raw_levels |= (uint32_t)gpio_get_level(i + 4) << i;
    s.valid = valid_active_level(SAFETY_RELAY_OK_ACTIVE_LEVEL) &&
              valid_active_level(ESTOP_PRESSED_ACTIVE_LEVEL) &&
              valid_active_level(STOP_BUTTON_OK_ACTIVE_LEVEL) &&
              valid_active_level(START_BUTTON_ACTIVE_LEVEL) &&
              valid_active_level(RESET_BUTTON_ACTIVE_LEVEL);
    s.commissioned = SAFETY_IO_COMMISSIONED;
    bool interpreted = false;
    if (SafetyIO_InterpretLevel(input_level(&s, ESTOP_PRESSED_GPIO), ESTOP_PRESSED_ACTIVE_LEVEL,
                                &interpreted))
        s.estop_pressed = interpreted;
    if (SafetyIO_InterpretLevel(input_level(&s, SAFETY_RELAY_OK_GPIO), SAFETY_RELAY_OK_ACTIVE_LEVEL,
                                &interpreted))
        s.relay_ok = interpreted;
    if (SafetyIO_InterpretLevel(input_level(&s, STOP_BUTTON_OK_GPIO), STOP_BUTTON_OK_ACTIVE_LEVEL,
                                &interpreted))
        s.stop_ok = interpreted;
    if (SafetyIO_InterpretLevel(input_level(&s, START_BUTTON_GPIO), START_BUTTON_ACTIVE_LEVEL,
                                &interpreted))
        s.start_pressed = interpreted;
    if (SafetyIO_InterpretLevel(input_level(&s, RESET_BUTTON_GPIO), RESET_BUTTON_ACTIVE_LEVEL,
                                &interpreted))
        s.reset_pressed = interpreted;
    s.vfd_fault_available = VFD_FAULT_DI_FITTED;
    s.vfd_fault = s.vfd_fault_available &&
                  (!SafetyIO_InterpretLevel(input_level(&s, VFD_FAULT_DI_GPIO),
                                            VFD_FAULT_DI_ACTIVE_LEVEL, &interpreted) ||
                   interpreted);
    s.contactor_available = CONTACTOR_FEEDBACK_FITTED;
    s.contactor_ok = SafetyIO_InterpretLevel(input_level(&s, CONTACTOR_FEEDBACK_GPIO),
                                             CONTACTOR_OK_ACTIVE_LEVEL, &interpreted) &&
                     interpreted;
#endif
    s.tick_ms = (uint32_t)(esp_timer_get_time() / 1000);
    portENTER_CRITICAL(&lock);
    if (s.valid && s.estop_pressed)
    {
        if (!emergency_latch)
            unsafe_generation++;
        emergency_latch = true;
    }
    if (s.valid && !s.relay_ok)
    {
        if (!relay_latch)
            unsafe_generation++;
        relay_latch = true;
    }
    if (s.valid && !s.stop_ok)
        stop_latch = true;
    portEXIT_CRITICAL(&lock);
    return s;
}

///////////////////////////////////////////////////////////////////////////////
// SafetyIO_Fault()

FaultReason_t SafetyIO_Fault(void)
{
    portENTER_CRITICAL(&lock);
    FaultReason_t f = emergency_latch ? FAULT_EMERGENCY_STOP
                      : relay_latch   ? FAULT_SAFETY_FEEDBACK
                                      : FAULT_NONE;
    portEXIT_CRITICAL(&lock);
    return f;
}

///////////////////////////////////////////////////////////////////////////////
// SafetyIO_Stop()

bool SafetyIO_Stop(void)
{
    portENTER_CRITICAL(&lock);
    bool value = stop_latch;
    stop_latch = false;
    portEXIT_CRITICAL(&lock);
    return value;
}

///////////////////////////////////////////////////////////////////////////////
// SafetyIO_Acknowledge()

void SafetyIO_Acknowledge(void)
{
    portENTER_CRITICAL(&lock);
    uint32_t observed = unsafe_generation;
    portEXIT_CRITICAL(&lock);
    SafetySnapshot s = SafetyIO_Read();
    if (!s.valid || s.estop_pressed || !s.relay_ok)
        return;
    portENTER_CRITICAL(&lock);
    if (observed == unsafe_generation)
    {
        emergency_latch = false;
        relay_latch = false;
    }
    portEXIT_CRITICAL(&lock);
    /* Recheck levels to capture an edge concurrent with acknowledgement. */
    (void)SafetyIO_Read();
}

///////////////////////////////////////////////////////////////////////////////
// SafetyIO_Inhibited()

bool SafetyIO_Inhibited(void)
{
    SafetySnapshot s = SafetyIO_Read();
    return !s.valid || !s.commissioned || s.estop_pressed || !s.relay_ok ||
           SafetyIO_Fault() != FAULT_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// SafetyIO_Simulate()

bool SafetyIO_Simulate(const char *name, bool active)
{
#if SIMULATION_MODE
    bool ok = true;
    portENTER_CRITICAL(&lock);
    if (!strcmp(name, "ESTOP"))
        simulated.estop_pressed = active;
    else if (!strcmp(name, "RELAY"))
        simulated.relay_ok = active;
    else if (!strcmp(name, "STOP"))
        simulated.stop_ok = !active;
    else if (!strcmp(name, "START"))
        simulated.start_pressed = active;
    else if (!strcmp(name, "RESET"))
        simulated.reset_pressed = active;
    else
        ok = false;
    portEXIT_CRITICAL(&lock);
    (void)SafetyIO_Read();
    return ok;
#else
    (void)name;
    (void)active;
    return false;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// SafetyIO_PublishButtonEvents()

/*
 * Publishes new validated START/RESET sequences from InputHandlerTask.
 * STOP has no existing debounced sequence: debounce only its advisory event.
 * The ISR latch, SafetyIO_Stop() and raw stop_ok path remain immediate.
 */
void SafetyIO_PublishButtonEvents(const SafetySnapshot *current, const SafetySnapshot *previous)
{
    static bool stop_initialized;
    static bool stop_raw, stop_stable;
    static uint32_t stop_changed_ms;
    EventBits_t bits = 0;

    if (current->start_sequence != previous->start_sequence)
        bits |= EVT_START_BUTTON;
    if (current->reset_sequence != previous->reset_sequence)
        bits |= EVT_RESET_BUTTON;

    const bool pressed = !current->stop_ok;
    if (!stop_initialized)
    {
        stop_initialized = true;
        stop_raw = pressed;
        stop_changed_ms = current->tick_ms;
    }
    if (pressed != stop_raw)
    {
        stop_raw = pressed;
        stop_changed_ms = current->tick_ms;
    }
    if (current->tick_ms - stop_changed_ms >= BUTTON_DEBOUNCE_MS)
    {
        if (stop_raw && !stop_stable && current->valid)
            bits |= EVT_STOP_BUTTON;
        stop_stable = stop_raw;
    }
    if (bits != 0)
        xEventGroupSetBits(handle_ButtonEventGroup, bits);
}
