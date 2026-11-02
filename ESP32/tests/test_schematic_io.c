/* Production GPIO decoding and ISR latches with injected electrical levels. */
#include <assert.h>
#include <stdio.h>
#include "../src/SafetyIO.c"

static int levels[49];
static EventBits_t bits;
EventGroupHandle_t handle_ButtonEventGroup = &bits;
int gpio_get_level(int pin) { return levels[pin]; }
esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return ESP_OK; }
esp_err_t gpio_set_intr_type(int pin, int type) { (void)pin; (void)type; return ESP_OK; }
esp_err_t gpio_isr_handler_add(int pin, void (*handler)(void *), void *arg)
{ (void)pin; (void)handler; (void)arg; return ESP_OK; }
int64_t esp_timer_get_time(void) { return 1000000; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t value) { return *g |= value; }

static void healthy_levels(void)
{
    for (unsigned n = 0; n < 49; ++n) levels[n] = 0;
    levels[4] = levels[7] = levels[8] = 1;
    emergency_latch = relay_latch = stop_latch = false;
    stop_was_ok = false;
}

int main(void)
{
    _Static_assert(ESTOP_PRESSED_GPIO == 4 && RESET_BUTTON_GPIO == 5 &&
        START_BUTTON_GPIO == 6 && STOP_BUTTON_OK_GPIO == 7 && SAFETY_RELAY_OK_GPIO == 8,
        "30 October schematic mapping");
    _Static_assert(ESTOP_PRESSED_ACTIVE_LEVEL == 0 && RESET_BUTTON_ACTIVE_LEVEL == 1 &&
        START_BUTTON_ACTIVE_LEVEL == 1 && STOP_BUTTON_OK_ACTIVE_LEVEL == 1 &&
        SAFETY_RELAY_OK_ACTIVE_LEVEL == 1, "NC trip LOW; NO press HIGH");
    healthy_levels();
    SafetyIO_Init();
    SafetySnapshot s = SafetyIO_Read();
    assert(s.valid && !s.estop_pressed && s.stop_ok && s.relay_ok && s.raw_levels == 25);
    assert(!s.reset_pressed && !s.start_pressed && !SafetyIO_Inhibited());
    levels[5] = levels[6] = 1;
    s = SafetyIO_Read();
    assert(s.reset_pressed && s.start_pressed && s.raw_levels == 31);
    for (unsigned input = 0; input < 2; input++) {
        healthy_levels();
        (void)SafetyIO_Read();
        int pin = input ? 8 : 4;
        levels[pin] = 0;
        input_isr(NULL);
        levels[pin] = 1; /* A short pulse must remain latched after release. */
        assert(SafetyIO_Fault() == (input ? FAULT_SAFETY_FEEDBACK : FAULT_EMERGENCY_STOP));
        assert(SafetyIO_Inhibited());
        levels[pin] = 0;
        SafetyIO_Acknowledge();
        assert(SafetyIO_Inhibited());
        levels[pin] = 1;
        SafetyIO_Acknowledge();
        assert(SafetyIO_Fault() == FAULT_NONE && !SafetyIO_Inhibited());
    }
    healthy_levels();
    (void)SafetyIO_Read();
    levels[7] = 0;
    input_isr(NULL);
    (void)SafetyIO_Read();
    assert(SafetyIO_Stop());
    (void)SafetyIO_Read();
    assert(!SafetyIO_Stop()); /* No retrigger while held LOW. */
    input_isr(NULL); /* An unrelated DI edge while STOP is held must not retrigger. */
    assert(!SafetyIO_Stop());
    assert(SafetyIO_Fault() == FAULT_NONE && !SafetyIO_Inhibited());
    levels[7] = 1;
    input_isr(NULL);
    (void)SafetyIO_Read();
    assert(!SafetyIO_Stop()); /* Rising release is not STOP. */
    levels[7] = 0;
    (void)SafetyIO_Read(); /* Polling also captures falling edges. */
    assert(SafetyIO_Stop());
    healthy_levels();
    levels[4] = levels[8] = 0;
    (void)SafetyIO_Read();
    assert(SafetyIO_Fault() == FAULT_EMERGENCY_STOP);
    puts("PASS schematic pin/polarity decoding, NC wire-open levels, latched ISR pulses, E-stop priority, STOP falling edge");
    return 0;
}
