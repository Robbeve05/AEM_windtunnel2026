///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * FaultSnapshot.c
 *
 * Deferred formatting of immutable first-fault evidence.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "FaultSnapshot.h"

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// FaultSnapshot_Format()

size_t FaultSnapshot_Format(const FirstFaultSnapshot *f, char *buffer, size_t size)
{
    if (!f || !f->valid || !buffer || !size)
        return 0;
    const MotorStatus *m = &f->motor;
    const SafetySnapshot *s = &f->safety;
    const ModbusTransactionDiagnostics *t = &m->transaction;
    size_t used = 0;
    int n;
    n = snprintf(buffer + used, size - used,
        "FIRST_FAULT sequence=%lu timestamp_ms=%lu previous_machine_state=%s new_machine_state=%s"
        " first_fault_code=%s first_fault_source=%s fault_reason=%s",
        (unsigned long)f->sequence, (unsigned long)f->timestamp_ms,
        StateMachine_Name(f->previous_state), StateMachine_Name(f->new_state),
        FaultReason_Name(f->code), f->source, FaultReason_Name(f->code));
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    n = snprintf(buffer + used, size - used,
        " configuration_failure_reason=%s configuration_mode=%s control_type=%u"
        " controller_ready=%d calibration_valid=%d model_identified=%d model_commissioned=%d"
        " watchdog_mapping_required=%d model_gain=%.6g model_tau_s=%.6g"
        " model_delay_s=%.6g model_lambda_s=%.6g",
        f->configuration.failure_reason ? f->configuration.failure_reason : "NONE",
        f->configuration.mode == MODE_MANUAL ? "MANUAL" : "AUTO", f->configuration.control_type,
        f->configuration.controller_ready, f->configuration.calibration_valid,
        f->configuration.model_identified, f->configuration.model_commissioned,
        f->configuration.watchdog_mapping_required, (double)f->configuration.model_gain,
        (double)f->configuration.model_tau_s, (double)f->configuration.model_delay_s,
        (double)f->configuration.model_lambda_s);
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    n = snprintf(buffer + used, size - used,
        " heartbeat_age_ms=%lu heartbeat_timeout_ms=%lu heartbeat_valid=%d"
        " heartbeat_received=%d tcp_connected=%d ethernet_link=%d pi_last_message_time=%lu",
        (unsigned long)f->heartbeat_age_ms, (unsigned long)f->heartbeat_timeout_ms, f->heartbeat_valid,
        f->heartbeat.connected, f->heartbeat.tcp_connected, f->ethernet_link,
        (unsigned long)f->pi_last_message_ms);
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    n = snprintf(buffer + used, size - used,
        " modbus_last_valid_response_ms=%lu modbus_last_valid_process_data_ms=%lu"
        " modbus_last_process_data_tx_ms=%lu modbus_max_process_data_gap_ms=%lu"
        " modbus_max_response_gap_ms=%lu modbus_max_process_data_tx_gap_ms=%lu"
        " modbus_errors=%lu modbus_timeouts=%lu",
        (unsigned long)m->last_valid_response_ms, (unsigned long)m->last_valid_process_data_ms,
        (unsigned long)m->last_process_data_tx_ms, (unsigned long)m->max_process_data_gap_ms,
        (unsigned long)m->max_response_gap_ms, (unsigned long)m->max_process_data_tx_gap_ms,
        (unsigned long)m->errors, (unsigned long)m->timeouts);
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    n = snprintf(buffer + used, size - used,
        " modbus_last_error=%u modbus_last_operation=%u modbus_valid_reads=%lu modbus_valid_writes=%lu"
        " modbus_crc_errors=%lu modbus_invalid_frames=%lu modbus_late_replies=%lu modbus_exceptions=%lu",
        m->last_error, m->last_operation, (unsigned long)m->valid_reads, (unsigned long)m->valid_writes,
        (unsigned long)m->crc_errors, (unsigned long)m->invalid_frames,
        (unsigned long)m->late_replies, (unsigned long)m->exceptions);
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    n = snprintf(buffer + used, size - used,
        " modbus_transaction_id=%lu modbus_transaction_type=%s"
        " modbus_transaction_start_ms=%lu modbus_transaction_deadline_ms=%lu"
        " modbus_transaction_completed_ms=%lu modbus_transaction_pending=%d"
        " modbus_transaction_result=%u modbus_write_unconfirmed=%d",
        (unsigned long)t->id, (t->type == 4u ? "WRITE_CONTROL" : t->type == 1u ? "READ_STATUS" :
            t->type == 2u ? "READ_REFERENCE" : t->type == 3u ? "READ_WATCHDOG" : "NONE"),
        (unsigned long)t->start_ms, (unsigned long)t->deadline_ms,
        (unsigned long)t->completed_ms, t->pending, t->result, t->write_unconfirmed);
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    n = snprintf(buffer + used, size - used,
        " modbus_transaction_pdu_address=%u modbus_transaction_register=%u"
        " modbus_transaction_count=%u modbus_tx_bytes=%u modbus_rx_bytes=%u modbus_expected_rx_bytes=%u"
        " modbus_first_rx_ms=%lu modbus_last_rx_ms=%lu modbus_rtt_us=%lu modbus_rtt_max_us=%lu",
        t->pdu_address, 40001u + t->pdu_address, t->register_count,
        t->tx_bytes, t->rx_bytes, t->expected_rx_bytes,
        (unsigned long)t->first_rx_ms, (unsigned long)t->last_rx_ms,
        (unsigned long)t->rtt_us, (unsigned long)t->max_rtt_us);
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    n = snprintf(buffer + used, size - used,
        " modbus_enqueue_us=%lu modbus_service_gap_max_us=%lu modbus_deadline_lateness_us=%lu"
        " modbus_uart_error_flags=%lu modbus_discarded_before_tx=%lu"
        " v20_status_response_ms=%lu v20_status_request_ms=%lu",
        (unsigned long)t->enqueue_us, (unsigned long)t->max_service_gap_us,
        (unsigned long)t->deadline_lateness_us,
        (unsigned long)t->uart_error_flags, (unsigned long)t->discarded_before_tx,
        (unsigned long)m->last_status_response_ms, (unsigned long)m->last_status_request_ms);
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    n = snprintf(buffer + used, size - used,
        " v20_statusword=%u v20_status_valid=%d v20_controlword=%u v20_controlword_valid=%d"
        " v20_setpoint_hz=%.3f v20_actual_hz=%.3f",
        m->status, m->status_valid, m->control_word_requested, m->control_word_requested_valid,
        (double)m->requested_hz, (double)m->actual_hz);
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    n = snprintf(buffer + used, size - used,
        " safety_valid=%d safety_commissioned=%d safety_tick_ms=%lu estop_pressed=%d"
        " safety_relay_ok=%d stop_ok=%d outputs_ok=%d vfd_fault_available=%d vfd_fault=%d",
        s->valid, s->commissioned, (unsigned long)s->tick_ms, s->estop_pressed,
        s->relay_ok, s->stop_ok, s->outputs_ok, s->vfd_fault_available, s->vfd_fault);
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    n = snprintf(buffer + used, size - used,
        " contactor_available=%d contactor_ok=%d start_pressed=%d reset_pressed=%d"
        " raw_di=%lu do1_requested=%d do1_acknowledged=%d do1_ack_valid=%d history_count=%u",
        s->contactor_available, s->contactor_ok, s->start_pressed, s->reset_pressed,
        (unsigned long)s->raw_levels, f->do1_requested, f->do1_acknowledged,
        f->do1_valid, f->history_count);
    if (n < 0 || (size_t)n >= size - used)
        return 0;
    used += (size_t)n;
    for (unsigned i = 0; i < f->history_count && i < FAULT_HISTORY_SIZE; ++i)
    {
        const FaultEvent *e = &f->history[i];
        n = snprintf(buffer + used, size - used, " event%u=%lu,%s,%u,%d,%d,%d,%d,%d,%d", i,
            (unsigned long)e->timestamp_ms, StateMachine_Name(e->state), e->statusword,
            e->status_valid, e->do1_requested, e->do1_acknowledged, e->do1_valid,
            e->tcp_connected, e->heartbeat_valid);
        if (n < 0 || (size_t)n >= size - used)
            return 0;
        used += (size_t)n;
    }
    const uint8_t *frames[] = {t->tx_frame, t->rx_frame};
    const unsigned lengths[] = {t->tx_bytes, t->rx_bytes};
    for (unsigned frame = 0; frame < 2; ++frame)
    {
        n = snprintf(buffer + used, size - used, " modbus_%s_hex=", frame ? "rx" : "tx");
        if (n < 0 || (size_t)n >= size - used) return 0;
        used += (size_t)n;
        if (!lengths[frame])
        {
            if (used + 1u >= size) return 0;
            buffer[used++] = '-';
        }
        for (unsigned i = 0; i < lengths[frame] && i < 32u; ++i)
        {
            n = snprintf(buffer + used, size - used, "%02x", frames[frame][i]);
            if (n < 0 || (size_t)n >= size - used) return 0;
            used += (size_t)n;
        }
    }
    if (used + 2u > size)
        return 0;
    buffer[used++] = '\n';
    buffer[used] = 0;
    return used;
}
