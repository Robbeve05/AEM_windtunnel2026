/* Author: R.E. van Eekelen */
#pragma once
#include "StateMachine.h"
typedef struct {
    bool initialized,start_raw,start_stable,reset_raw,reset_stable,start_armed,reset_armed,reset_candidate;
    uint32_t start_changed,reset_changed;
} Buttons;
void Buttons_Update(Buttons *b,SafetySnapshot *s,uint32_t now);
