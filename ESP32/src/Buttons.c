/* Author: R.E. van Eekelen */
#include "Buttons.h"
#include "AppConfig.h"
void Buttons_Update(Buttons *b,SafetySnapshot *s,uint32_t now) {
    if (!b->initialized) {
        *b=(Buttons){.initialized=true,.start_raw=s->start_pressed,.start_stable=s->start_pressed,
            .reset_raw=s->reset_pressed,.reset_stable=s->reset_pressed,.start_changed=now,.reset_changed=now};
        return;
    }
    if (!s->valid || s->estop_pressed) { b->reset_armed=false; b->reset_candidate=false; b->start_armed=false; }
    if (s->start_pressed!=b->start_raw) { b->start_raw=s->start_pressed; b->start_changed=now; }
    if (s->reset_pressed!=b->reset_raw) { b->reset_raw=s->reset_pressed; b->reset_changed=now; }
    if (now-b->start_changed>=BUTTON_DEBOUNCE_MS) {
        if (!b->start_raw) b->start_armed=true;
        if (b->start_raw && !b->start_stable && b->start_armed && s->valid && !s->estop_pressed && !s->reset_pressed) {
            s->start_sequence++; s->start_ms=now; b->start_armed=false;
        }
        b->start_stable=b->start_raw;
    }
    if (now-b->reset_changed>=BUTTON_DEBOUNCE_MS) {
        if (b->reset_raw && !b->reset_stable && b->reset_armed && s->valid && !s->estop_pressed) {
            b->reset_candidate=true; b->reset_armed=false;
        }
        if (!b->reset_raw && b->reset_stable && b->reset_candidate && !s->estop_pressed && s->valid) {
            s->reset_sequence++; s->reset_ms=now; b->reset_candidate=false;
        }
        if (!b->reset_raw && !s->estop_pressed && s->valid) b->reset_armed=true;
        b->reset_stable=b->reset_raw;
    }
}
