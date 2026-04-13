#include "include/config.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "include/alarm.h"
#include "include/sensors.h"

extern volatile bool g_disarm_requested;
extern volatile bool g_log_requested;

// ============================================================================
// SNAPSHOT — copiado com state_mutex, impresso com uart_mutex (nunca ambos)
// ============================================================================

typedef struct {
    SystemMode       mode;
    ThreatLevel      threat_level;
    SensitivityIndex sensitivity;
    float            sound_level;
    float            temperature;
    float            threshold;
    float            acustica_mean;
    float            acustica_std;
    bool             is_learned;
    uint32_t         uptime_seconds;
} UartLogSnapshot;

static void uart_log_print(const UartLogSnapshot* s, mutex_t* uart_mutex) {
    const char* mode_str;
    switch (s->mode) {
        case MODE_DISARMED:  mode_str = "DESARMADO";  break;
        case MODE_LEARNING:  mode_str = "CALIBRANDO"; break;
        case MODE_ARMED:     mode_str = "ARMADO";     break;
        case MODE_TRIGGERED: mode_str = "DISPARADO";  break;
        case MODE_SNOOZED:   mode_str = "SNOOZE";     break;
        default:             mode_str = "DESCONHEC."; break;
    }
    const char* sensib_str;
    switch (s->sensitivity) {
        case SENSITIVITY_IDX_HIGH:   sensib_str = "ALTA(2s)";  break;
        case SENSITIVITY_IDX_MEDIUM: sensib_str = "MEDIA(3s)"; break;
        case SENSITIVITY_IDX_LOW:    sensib_str = "BAIXA(4s)"; break;
        default:                     sensib_str = "---";        break;
    }

    mutex_enter_blocking(uart_mutex);
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║         LOG — T=%05lu s                         ║\n", s->uptime_seconds);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Modo        : %-33s║\n", mode_str);
    printf("║  Ameaca      : %-33s║\n", alarm_threat_to_string(s->threat_level));
    printf("║  Sensibilid. : %-33s║\n", sensib_str);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Som atual   : %-6.1f  threshold: %-10.1f  ║\n",
           s->sound_level, s->is_learned ? s->threshold : 0.0f);
    printf("║  Temperatura : %-6.1f C                         ║\n", s->temperature);
    printf("╠══════════════════════════════════════════════════╣\n");
    if (s->is_learned)
        printf("║  Acustica u  : %-6.1f  sigma: %-6.1f            ║\n",
               s->acustica_mean, s->acustica_std);
    else
        printf("║  Acustica    : sem calibracao                    ║\n");
    printf("║  Uptime      : %02lu:%02lu:%02lu                          ║\n",
           s->uptime_seconds / 3600,
           (s->uptime_seconds / 60) % 60,
           s->uptime_seconds % 60);
    printf("╚══════════════════════════════════════════════════╝\n\n");
    mutex_exit(uart_mutex);
}

static void take_snapshot(const SystemState* state, UartLogSnapshot* snap) {
    snap->mode           = state->mode;
    snap->threat_level   = state->threat_level;
    snap->sensitivity    = state->sensitivity;
    snap->sound_level    = state->current_reading.sound_level;
    snap->temperature    = state->current_reading.temperature;
    snap->threshold      = state->acoustic_sig.threshold;
    snap->acustica_mean  = state->acoustic_sig.mean;
    snap->acustica_std   = state->acoustic_sig.std_dev;
    snap->is_learned     = state->acoustic_sig.is_learned;
    snap->uptime_seconds = state->uptime_seconds;
}

// ============================================================================
// CORE 0 — LOOP DE SEGURANÇA
// ============================================================================

void core0_security_main(SystemState* state, mutex_t* state_mutex, mutex_t* uart_mutex) {
    UART_PRINT("\n[CORE 0] Seguranca ativa.\n\n");

    uint32_t last_sensor_read  = 0;
    uint32_t last_threat_check = 0;
    uint32_t last_uptime       = 0;

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // ── Desarme via IRQ ────────────────────────────────────────────────
        if (g_disarm_requested) {
            g_disarm_requested = false;
            mutex_enter_blocking(state_mutex);
            bool was_active = (state->mode != MODE_DISARMED &&
                               state->mode != MODE_LEARNING);
            if (was_active) {
                state->mode         = MODE_DISARMED;
                state->threat_level = THREAT_NONE;
            }
            mutex_exit(state_mutex);
            if (was_active) UART_PRINT("[IRQ] Desarme de emergencia!\n");
        }

        // ── Log sob demanda (SW pressionado sem joystick) ──────────────────
        if (g_log_requested) {
            g_log_requested = false;
            UartLogSnapshot snap;
            mutex_enter_blocking(state_mutex);
            take_snapshot(state, &snap);
            mutex_exit(state_mutex);
            uart_log_print(&snap, uart_mutex);
        }

        // ── Leitura de sensores (10 Hz) ────────────────────────────────────
        if (now - last_sensor_read >= SENSOR_READ_INTERVAL_MS) {
            SensorData reading = sensors_read_all();
            reading.timestamp  = now;

            mutex_enter_blocking(state_mutex);
            state->current_reading               = reading;
            state->history[state->history_index] = reading;
            state->history_index = (state->history_index + 1) % 60;
            mutex_exit(state_mutex);

            sensors_add_to_history(&reading);
            last_sensor_read = now;
        }

        // ── Verificação de ameaças (2 Hz) ──────────────────────────────────
        if (now - last_threat_check >= THREAT_CHECK_INTERVAL_MS) {
            mutex_enter_blocking(state_mutex);

            if (state->mode == MODE_ARMED) {
                ThreatAnalysis analysis;
                ThreatLevel level = alarm_detect(
                    &state->current_reading,
                    &state->baseline_reading,
                    &state->acoustic_sig,
                    &analysis
                );
                state->threat_level = level;

                if (level >= THREAT_PROBABLE) {
                    state->mode                 = MODE_TRIGGERED;
                    state->last_alert_timestamp = now;
                    char desc[128];
                    strncpy(desc, analysis.description, sizeof(desc));
                    mutex_exit(state_mutex);
                    UART_PRINT("\n[ALARME] %s\n", desc);
                } else {
                    mutex_exit(state_mutex);
                }
            } else {
                mutex_exit(state_mutex);
            }

            // Auto-reset após 5 min em TRIGGERED
            mutex_enter_blocking(state_mutex);
            if (state->mode == MODE_TRIGGERED &&
                (now - state->last_alert_timestamp) > 300000) {
                state->mode         = MODE_DISARMED;
                state->threat_level = THREAT_NONE;
                mutex_exit(state_mutex);
                UART_PRINT("[TIMEOUT] Alarme resetado.\n");
            } else {
                mutex_exit(state_mutex);
            }

            // Snooze expirado
            mutex_enter_blocking(state_mutex);
            if (state->mode == MODE_SNOOZED && now >= state->snooze_until_ms)
                state->mode = MODE_ARMED;
            mutex_exit(state_mutex);

            last_threat_check = now;
        }

        // ── Uptime (1 Hz) ──────────────────────────────────────────────────
        if (now - last_uptime >= 1000) {
            mutex_enter_blocking(state_mutex);
            state->uptime_seconds++;
            mutex_exit(state_mutex);
            last_uptime = now;
        }

        sleep_ms(10);
    }
}
