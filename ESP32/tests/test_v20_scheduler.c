/* Production scheduler + UART fake. Deterministic timing; no hardware claims.
 * BASELINE disables assertions specific to the optimized driver.
 * Author: R.E. van Eekelen
 */
#define main existing_driver_suite
#include "test_v20.c"
#undef main

int main(void)
{
    const unsigned delays[] = {1000, 30000, 70000};
    for (unsigned c=0;c<3;c++) {
        boot();
        MotorRequest r=request(true,50);
        assert(V20Modbus_Arm());
        fake.response_delay_us=delays[c];
        pump(&r,100);
        unsigned writes=fake.commands, reads=fake.reads;
        int64_t prev_w=0,prev_r=0;
        unsigned max_w=0,max_r=0,miss=0;
        for(unsigned i=0;i<6000;i++) {
            unsigned w=fake.commands, rd=fake.reads;
            step(&r,true,true);
            assert(status().transaction.type==V20_OP_READ_STATUS ||
                   status().transaction.type==V20_OP_WRITE_CONTROL);
            if(fake.commands!=w) {
                if(prev_w) { unsigned gap=(unsigned)(now_us-prev_w); if(gap>max_w)max_w=gap; if(gap>100000)miss++; }
                prev_w=now_us;
            }
            if(fake.reads!=rd) {
                if(prev_r) {unsigned gap=(unsigned)(now_us-prev_r); if(gap>max_r)max_r=gap;}
                prev_r=now_us;
            }
        }
        printf("delay=%u write_hz=%.3f read_hz=%.3f max_write_us=%u max_read_us=%u missed=%u max_rtt_us=%u\n",
            delays[c],(fake.commands-writes)/60.0,(fake.reads-reads)/60.0,max_w,max_r,miss,status().transaction.max_rtt_us);
        assert(!status().run_locked && max_w<500000 && max_r<500000);
#ifndef BASELINE
        if (delays[c]<=30000) assert(max_w==100000 && max_r==100000 && miss==0);
        assert(status().bus.write_cycle_us==max_w);
        assert(status().bus.max_transaction_us>=delays[c]);
        assert(status().bus.min_idle_us>=MODBUS_SILENCE_US);
        assert(status().bus.wire_load_permille>0);
        printf("observed idle_min_us=%u idle_max_us=%u max_transaction_us=%u max_service_gap_us=%u wire_permille=%u\n",
            status().bus.min_idle_us,status().bus.max_idle_us,status().bus.max_transaction_us,
            status().bus.max_service_gap_us,status().bus.wire_load_permille);
#endif
    }
    /* Deterministic service jitter and changing slave latency. */
    boot(); MotorRequest jittered=request(true,50); assert(V20Modbus_Arm());
    const unsigned ticks[]={8000,12000,10000};
    const unsigned latency[]={1000,30000,50000};
    unsigned jw=fake.commands,jr=fake.reads;
    for(unsigned i=0;i<6000;i++) {
        now_us+=ticks[i%3]; jittered.tick=xTaskGetTickCount();
        fake.response_delay_us=latency[(i/17)%3];
        V20Modbus_Service(&jittered,true);
    }
    printf("jitter write_hz=%.3f read_hz=%.3f max_tx_gap_ms=%u max_ack_gap_ms=%u max_rtt_us=%u\n",
        (fake.commands-jw)/60.0,(fake.reads-jr)/60.0,status().max_process_data_tx_gap_ms,
        status().max_process_data_gap_ms,status().transaction.max_rtt_us);
    assert(!status().run_locked && fake.reads>jr+300 && fake.commands>jw+300);
    assert(status().max_process_data_tx_gap_ms<450);
    /* One lost status reply: bounded retry, watchdog and explicit STOP latency. */
    boot(); MotorRequest r=request(true,50); assert(V20Modbus_Arm());
    while (!(status().transaction.pending && status().transaction.type==V20_OP_READ_STATUS))
        step(&r,true,true);
    fake.rx_size=fake.rx_offset=0;
    pump(&r,60);
    printf("lost_read max_write_gap_ms=%u retries=%u recovered=%u locked=%u\n",
        status().max_process_data_tx_gap_ms,status().retries,status().recovered_transactions,status().run_locked);
    assert(status().retries==1 && status().recovered_transactions==1 && !status().run_locked);
    assert(status().max_process_data_tx_gap_ms<450);
    boot(); r=request(true,50); assert(V20Modbus_Arm()); fake.response_delay_us=70000;
    pump(&r,100);
    while (status().transaction.pending) step(&r,true,true);
    while (!(status().transaction.pending && status().transaction.type==V20_OP_READ_STATUS))
        step(&r,true,true);
    int64_t stop=now_us; unsigned writes=fake.commands;
    V20Modbus_Disarm(); r.run=false;
    while(fake.commands==writes) step(&r,true,true);
    printf("stop_behind_read_us=%u\n",(unsigned)(now_us-stop));
    assert(fake.stw==0x047e && now_us-stop<=90000);
#ifndef BASELINE
    /* Scheduler stalled past deadline with a complete reply buffered: fail closed. */
    boot(); r=request(true,50); assert(V20Modbus_Arm());
    while(!status().transaction.pending) step(&r,true,true);
    now_us+=90000; r.tick=xTaskGetTickCount(); V20Modbus_Service(&r,true);
    assert(status().transaction.result==V20_COMM_TIMEOUT);
#endif
    return 0;
}
