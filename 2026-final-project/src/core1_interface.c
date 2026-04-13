#include "include/config.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "include/alarm.h"
#include "include/display.h"

extern volatile bool g_disarm_requested;
extern volatile bool g_button_a_pressed;
extern volatile bool g_button_b_pressed;
extern volatile bool g_joy_sw_pressed;
extern volatile bool g_log_requested;

extern mutex_t g_adc_mutex;  // Para proteção do ADC

// ============================================================================
// BUZZER
// ============================================================================

static uint slice_a, slice_b;

static void buzzer_init(void) {
    slice_a = pwm_gpio_to_slice_num(BUZZER_A_PIN);
    slice_b = pwm_gpio_to_slice_num(BUZZER_B_PIN);
    pwm_set_chan_level(slice_a, PWM_CHAN_A, 0);
    pwm_set_chan_level(slice_b, PWM_CHAN_A, 0);
}

static void buzzer_play(ThreatLevel level) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    switch (level) {
        case THREAT_PROBABLE:
            pwm_set_chan_level(slice_a, PWM_CHAN_A, ((now / 500) % 2) ? 31250 : 0);
            pwm_set_chan_level(slice_b, PWM_CHAN_A, 0);
            break;
        case THREAT_CONFIRMED:
            if ((now / 200) % 2) {
                pwm_set_chan_level(slice_a, PWM_CHAN_A, 31250);
                pwm_set_chan_level(slice_b, PWM_CHAN_A, 0);
            } else {
                pwm_set_chan_level(slice_a, PWM_CHAN_A, 0);
                pwm_set_chan_level(slice_b, PWM_CHAN_A, 31250);
            }
            break;
        default:
            pwm_set_chan_level(slice_a, PWM_CHAN_A, 0);
            pwm_set_chan_level(slice_b, PWM_CHAN_A, 0);
    }
}

static void buzzer_stop(void) {
    pwm_set_chan_level(slice_a, PWM_CHAN_A, 0);
    pwm_set_chan_level(slice_b, PWM_CHAN_A, 0);
}

// ============================================================================
// JOYSTICK ANALÓGICO — leitura com filtro de confirmação
// ============================================================================

static JoystickDir joystick_read_confirmed(void) {
    static JoystickDir last_raw   = JOY_NONE;
    static uint8_t     count      = 0;
    static JoystickDir confirmed  = JOY_NONE;

    uint16_t x, y;
    
    mutex_enter_blocking(&g_adc_mutex);
    adc_select_input(JOY_X_ADC_CHANNEL);
    sleep_us(10);
    x = adc_read();
    adc_select_input(JOY_Y_ADC_CHANNEL);
    sleep_us(10);
    y = adc_read();
    mutex_exit(&g_adc_mutex);

    int dx = (int)x - 2048;
    int dy = (int)y - 2048;
    int thr = JOY_THRESHOLD_HIGH - 2048;

    JoystickDir raw = JOY_NONE;
    if (abs(dx) >= thr || abs(dy) >= thr) {
        if (abs(dx) >= abs(dy)) {
            raw = (x > JOY_THRESHOLD_HIGH) ? JOY_RIGHT : JOY_LEFT;
        } else {
            raw = (y > JOY_THRESHOLD_HIGH) ? JOY_DOWN : JOY_UP;
        }
    }

    if (raw == last_raw && raw != JOY_NONE) {
        count++;
        if (count >= JOY_CONFIRM_COUNT) confirmed = raw;
    } else {
        count = 0;
        if (raw == JOY_NONE) confirmed = JOY_NONE;
    }
    last_raw = raw;
    return confirmed;
}

static bool button_b_consume(void) {
    if (!g_button_b_pressed) return false;
    g_button_b_pressed = false;
    return true;
}

static bool joy_sw_consume(void) {
    if (!g_joy_sw_pressed) return false;
    g_joy_sw_pressed = false;
    return true;
}

static bool button_a_consume(void) {
    if (!g_button_a_pressed) return false;
    g_button_a_pressed = false;
    return true;
}

// ============================================================================
// SENSIBILIDADE
// ============================================================================

static const float sensitivity_mult[3] = {
    SENSITIVITY_HIGH, SENSITIVITY_MEDIUM, SENSITIVITY_LOW
};
static const char* sensitivity_names[3] = {
    "Alta (2s)", "Media (3s)", "Baixa (4s)"
};

// ============================================================================
// LED
// ============================================================================

static void led_set(bool r, bool g, bool b) {
    gpio_put(LED_R_PIN, r);
    gpio_put(LED_G_PIN, g);
    gpio_put(LED_B_PIN, b);
}

// ============================================================================
// DISPLAY
// ============================================================================

static void display_update(SystemState* state, mutex_t* state_mutex) {
    mutex_enter_blocking(state_mutex);
    display_draw_screen(state);
    mutex_exit(state_mutex);
}

// ============================================================================
// FASE CONFIG
// ============================================================================

static void phase_config(SystemState* state, mutex_t* state_mutex, mutex_t* uart_mutex) {
    UART_PRINT("╔════════════════════════════════════════════╗\n");
    UART_PRINT("║                   ACOES                ║\n");
    UART_PRINT("╠════════════════════════════════════════════╣\n");
    UART_PRINT("║ [B]  = Alternar tela (Antes de Calibrar)   ║\n");
    UART_PRINT("║ [B]  = Desarmar Alarme (Depois de Calibrar)║\n");
    UART_PRINT("║ [SW] = Log UART                            ║\n");
    UART_PRINT("║ [A]  = Calibrar e Armar                    ║\n");
    UART_PRINT("╚════════════════════════════════════════════╝\n\n");

    JoystickDir pending   = JOY_NONE;
    uint32_t    last_disp = 0;
    JoystickDir last_dir  = JOY_NONE;
    bool        b_was_pressed = false;

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // ── Display (30 Hz) ────────────────────────────────────────────────
        if (now - last_disp >= DISPLAY_UPDATE_RATE_MS) {
            display_update(state, state_mutex);
            led_set(0, 0, (now / 500) % 2);
            last_disp = now;
        }

        // ── Joystick com filtro de confirmação ─────────────────────────────
        JoystickDir dir = joystick_read_confirmed();

        // Registra pending na borda de subida (centro → direção confirmada)
        if (dir != JOY_NONE && last_dir == JOY_NONE) {
            pending = dir;
        }
        
        last_dir = dir;

        // ── Botão B via IRQ (usado em combinação com o joystick) ───────────
        bool b_pressed = button_b_consume();
        
        if (b_pressed) {
            b_was_pressed = true;
        }

        // ── Se Botão B foi pressionado E temos direção pendente ────────────
        if (b_was_pressed && pending != JOY_NONE) {
            // Executa ação baseada na direção do joystick
            switch (pending) {
                case JOY_UP: {
                    mutex_enter_blocking(state_mutex);
                    state->screen = (state->screen == SCREEN_MAIN)
                                    ? SCREEN_STATUS : SCREEN_MAIN;
                    ScreenMode scr = state->screen;
                    mutex_exit(state_mutex);
                    UART_PRINT("Tela: %s\n",
                        scr == SCREEN_MAIN ? "Principal" : "Status");
                    break;
                }
                case JOY_DOWN: {
                    mutex_enter_blocking(state_mutex);
                    state->sensitivity = (SensitivityIndex)
                        ((state->sensitivity + 1) % 3);
                    SensitivityIndex si = state->sensitivity;
                    bool learned = state->acoustic_sig.is_learned;
                    mutex_exit(state_mutex);
                    if (learned) {
                        mutex_enter_blocking(state_mutex);
                        alarm_recalculate_threshold(&state->acoustic_sig,
                            sensitivity_mult[si]);
                        mutex_exit(state_mutex);
                    }
                    UART_PRINT("Sensibilidade: %s\n", sensitivity_names[si]);
                    break;
                }
                case JOY_RIGHT: {
                    mutex_enter_blocking(state_mutex);
                    state->mode = MODE_LEARNING;
                    mutex_exit(state_mutex);

                    UART_PRINT("Calibrando...\n");
                    alarm_learn_environment(&state->acoustic_sig);

                    mutex_enter_blocking(state_mutex);
                    if (state->acoustic_sig.is_learned) {
                        alarm_recalculate_threshold(&state->acoustic_sig,
                            sensitivity_mult[state->sensitivity]);
                    }
                    state->mode = MODE_DISARMED;
                    mutex_exit(state_mutex);
                    UART_PRINT("Calibracao OK! Pressione [A] para armar.\n");
                    break;
                }
                default:
                    break;
            }
            // Reseta estados
            pending  = JOY_NONE;
            last_dir = JOY_NONE;
            b_was_pressed = false;
        }
        
        // ── SW sozinho → log UART sob demanda ──────────────────────────────
        if (joy_sw_consume()) {
            g_log_requested = true;
        }

        // ── Botão A via IRQ ────────────────────────────────────────────────
        if (button_a_consume()) {
            mutex_enter_blocking(state_mutex);
            bool learned = state->acoustic_sig.is_learned;
            mutex_exit(state_mutex);

            if (!learned) {
                mutex_enter_blocking(state_mutex);
                state->mode = MODE_LEARNING;
                mutex_exit(state_mutex);

                UART_PRINT("[A] Calibrando antes de armar...\n");
                bool ok = alarm_learn_environment(&state->acoustic_sig);

                mutex_enter_blocking(state_mutex);
                if (!ok) {
                    state->mode = MODE_DISARMED;
                    mutex_exit(state_mutex);
                    UART_PRINT("Falha na calibracao. Tente de novo.\n");
                    continue;
                }
                alarm_recalculate_threshold(&state->acoustic_sig,
                    sensitivity_mult[state->sensitivity]);
                state->baseline_reading = state->current_reading;
                mutex_exit(state_mutex);
            }

            mutex_enter_blocking(state_mutex);
            state->mode         = MODE_ARMED;
            state->threat_level = THREAT_NONE;
            mutex_exit(state_mutex);

            UART_PRINT("[A] Sistema ARMADO.  [B] = parada de emergencia\n\n");
            return;
        }

        sleep_ms(20);
    }
}

// ============================================================================
// FASE ARMED — somente [B]/IRQ desarma
// ============================================================================

static void phase_armed(SystemState* state, mutex_t* state_mutex, mutex_t* uart_mutex) {
    uint32_t last_disp   = 0;
    uint32_t last_buzzer = 0;

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // ── Desarme via IRQ (Botão B) ──────────────────────────────────────
        if (g_disarm_requested) {
            g_disarm_requested = false;
            mutex_enter_blocking(state_mutex);
            state->mode         = MODE_DISARMED;
            state->threat_level = THREAT_NONE;
            mutex_exit(state_mutex);
            buzzer_stop();
            UART_PRINT("[IRQ B] Parada de emergencia! Voltando para CONFIG.\n\n");
            return;
        }

        // Descarta eventos de A/SW — sem efeito na fase ARMED
        if (g_button_a_pressed) g_button_a_pressed = false;
        if (g_joy_sw_pressed) {
            g_joy_sw_pressed = false;
            g_log_requested  = true;   // SW na fase armada → log
        }
        if (g_button_b_pressed) {
            g_button_b_pressed = false;
            // Botão B na fase armada já tratado pelo disarm_requested
        }

        // ── Snooze expirado ────────────────────────────────────────────────
        mutex_enter_blocking(state_mutex);
        if (state->mode == MODE_SNOOZED && now >= state->snooze_until_ms) {
            state->mode = MODE_ARMED;
            mutex_exit(state_mutex);
            UART_PRINT("Snooze expirado — ARMADO novamente.\n");
        } else {
            mutex_exit(state_mutex);
        }

        mutex_enter_blocking(state_mutex);
        SystemMode  cur_mode   = state->mode;
        ThreatLevel cur_threat = state->threat_level;
        mutex_exit(state_mutex);

        // ── Display (30 Hz) ────────────────────────────────────────────────
        if (now - last_disp >= DISPLAY_UPDATE_RATE_MS) {
            display_update(state, state_mutex);

            switch (cur_mode) {
                case MODE_ARMED:
                case MODE_SNOOZED:   led_set(1, 0, 0); break;
                case MODE_TRIGGERED: led_set((now / 200) % 2, 0, 0); break;
                default:             led_set(0, 1, 0); break;
            }
            last_disp = now;
        }

        // ── Buzzer (10 Hz) ─────────────────────────────────────────────────
        if (now - last_buzzer >= 100) {
            if (cur_mode == MODE_TRIGGERED) buzzer_play(cur_threat);
            else                            buzzer_stop();
            last_buzzer = now;
        }

        sleep_ms(5);
    }
}

// ============================================================================
// CORE 1 — LOOP DE INTERFACE
// ============================================================================

void core1_interface_main(SystemState* state, mutex_t* state_mutex, mutex_t* uart_mutex) {
    UART_PRINT("[CORE 1] Interface iniciada.\n\n");
    buzzer_init();

    while (true) {
        mutex_enter_blocking(state_mutex);
        state->mode         = MODE_DISARMED;
        state->threat_level = THREAT_NONE;
        mutex_exit(state_mutex);

        g_button_a_pressed = false;
        g_button_b_pressed = false;   // ADICIONADO
        g_joy_sw_pressed   = false;
        g_disarm_requested = false;
        g_log_requested    = false;

        phase_config(state, state_mutex, uart_mutex);
        phase_armed (state, state_mutex, uart_mutex);
    }
}