/* Author: R.E. van Eekelen. Fixed-period accounting; never replay missed PI steps. */
#include "ControlTiming.h"
#include "AppConfig.h"
FaultReason_t ControlTiming_Tick(ControlTiming *t,uint64_t now,unsigned notifications) {
    uint64_t elapsed=t->previous_us?now-t->previous_us:CONTROL_PERIOD_US;
    uint64_t jitter=elapsed>CONTROL_PERIOD_US?elapsed-CONTROL_PERIOD_US:CONTROL_PERIOD_US-elapsed;
    if(jitter>t->max_jitter_us) t->max_jitter_us=(uint32_t)jitter;
    t->previous_us=now;
    if(!notifications) { t->missed_deadlines+=CONTROL_CLOCK_TIMEOUT_MS/(CONTROL_PERIOD_US/1000); return FAULT_CLOCK_TIMEOUT; }
    if(notifications>1||elapsed>=2*CONTROL_PERIOD_US) {
        unsigned missed=notifications>1?notifications-1:0;
        unsigned elapsed_missed=(unsigned)(elapsed/CONTROL_PERIOD_US);
        if(elapsed_missed) elapsed_missed--;
        t->missed_deadlines+=missed>elapsed_missed?missed:elapsed_missed;
        return FAULT_CONTROL_OVERRUN;
    }
    return FAULT_NONE;
}
bool ControlTiming_Execution(ControlTiming *t,uint32_t execution_us,uint32_t service_us) {
    if(execution_us<CONTROL_PERIOD_US&&service_us<V20_SERVICE_BUDGET_US) return true;
    t->overruns++; return false;
}
