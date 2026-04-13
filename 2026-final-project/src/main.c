#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"

#include "include/config.h"
#include "include/alarm.h"
#include "include/sensors.h"
#include "include/display.h"

extern void core0_security_main(SystemState* state, mutex_t* state_mutex, mutex_t* uart_mutex);
extern void core1_interface_main(SystemState* state, mutex_t* state_mutex, mutex_t* uart_mutex);

static SystemState g_system_state;
static mutex_t     g_state_mutex;
mutex_t            g_uart_mutex;   // definido aqui; extern em config.h
mutex_t g_adc_mutex;  // Adicione esta linha


// ============================================================================
// FLAGS DE INTERRUPÇÃO
// ============================================================================

volatile bool g_disarm_requested = false;
volatile bool g_button_a_pressed = false;
volatile bool g_joy_sw_pressed   = false;
volatile bool g_log_requested    = false;   // SW sozinho → emite log UART
volatile bool g_button_b_pressed = false;

// ============================================================================
// IRQ UNIFICADA — Botão A, Botão B, SW do Joystick
// ============================================================================

// void gpio_irq_callback(uint gpio, uint32_t events) {
//     if (!(events & GPIO_IRQ_EDGE_FALL)) return;

//     static uint32_t last_a  = 0;
//     static uint32_t last_b  = 0;
//     static uint32_t last_sw = 0;

//     uint32_t now = to_ms_since_boot(get_absolute_time());

//     if (gpio == BUTTON_B_PIN && (now - last_b) > 300) {
//         g_disarm_requested = true;
//         last_b = now;
//     } else if (gpio == BUTTON_A_PIN && (now - last_a) > 300) {
//         g_button_a_pressed = true;
//         last_a = now;
//     } else if (gpio == JOY_SW_PIN && (now - last_sw) > 300) {
//         g_joy_sw_pressed = true;
//         last_sw = now;
//     }
// }

// // No main.c, adicione esta linha com as outras flags:
// volatile bool g_button_b_pressed = false;   // ADICIONAR
// Na função gpio_irq_callback, adicione:

void gpio_irq_callback(uint gpio, uint32_t events) {
    if (!(events & GPIO_IRQ_EDGE_FALL)) return;

    static uint32_t last_a  = 0;
    static uint32_t last_b  = 0;
    static uint32_t last_sw = 0;

    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (gpio == BUTTON_B_PIN && (now - last_b) > 300) {
        g_disarm_requested = true;      // Desarme imediato
        g_button_b_pressed = true;      // Para combinação com joystick
        last_b = now;
    } else if (gpio == BUTTON_A_PIN && (now - last_a) > 300) {
        g_button_a_pressed = true;
        last_a = now;
    } else if (gpio == JOY_SW_PIN && (now - last_sw) > 300) {
        g_joy_sw_pressed = true;
        last_sw = now;
    }
}

// ============================================================================
// HARDWARE INIT
// printf direto — uart_mutex ainda não existe (fase single-core)
// ============================================================================

static void hardware_init(void) {
    stdio_init_all();
    sleep_ms(1500);

    printf("\n\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║   BitDogLab - Monitor de Ruido de Maquina       ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
    printf("Inicializando hardware...\n");

    // ADC
    adc_init();
    adc_gpio_init(MIC_ADC_PIN);
    adc_gpio_init(JOY_X_ADC_PIN);
    adc_gpio_init(JOY_Y_ADC_PIN);
    adc_set_temp_sensor_enabled(true);
    printf("  ADC... OK\n");

    // I2C1 — pinos GP14 (SDA) e GP15 (SCL), BitDogLab V7
    i2c_init(DISPLAY_I2C_PORT, DISPLAY_I2C_FREQ);
    gpio_set_function(DISPLAY_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DISPLAY_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DISPLAY_SDA_PIN);
    gpio_pull_up(DISPLAY_SCL_PIN);
    sleep_ms(200);
    printf("  I2C1 (GP%d SDA, GP%d SCL)... OK\n", DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);

    // Scan I2C — diagnóstico
    printf("  Scan I2C:");
    bool found = false;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        uint8_t buf;
        if (i2c_read_blocking(DISPLAY_I2C_PORT, addr, &buf, 1, false) >= 0) {
            printf(" 0x%02X", addr);
            found = true;
        }
    }
    if (!found) {
        printf(" nenhum! Verifique SDA=GP%d SCL=GP%d e alimentacao do display.\n",
               DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);
    } else {
        printf("\n");
    }

    // Botões e joystick SW
    gpio_init(BUTTON_A_PIN); gpio_set_dir(BUTTON_A_PIN, GPIO_IN); gpio_pull_up(BUTTON_A_PIN);
    gpio_init(BUTTON_B_PIN); gpio_set_dir(BUTTON_B_PIN, GPIO_IN); gpio_pull_up(BUTTON_B_PIN);
    gpio_init(JOY_SW_PIN);   gpio_set_dir(JOY_SW_PIN,   GPIO_IN); gpio_pull_up(JOY_SW_PIN);
    printf("  Botoes... OK\n");

    // IRQ unificada
    gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_callback);
    gpio_set_irq_enabled(BUTTON_A_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(JOY_SW_PIN,   GPIO_IRQ_EDGE_FALL, true);
    printf("  IRQ A, B, SW... OK\n");

    // LED RGB
    gpio_init(LED_R_PIN); gpio_set_dir(LED_R_PIN, GPIO_OUT);
    gpio_init(LED_G_PIN); gpio_set_dir(LED_G_PIN, GPIO_OUT);
    gpio_init(LED_B_PIN); gpio_set_dir(LED_B_PIN, GPIO_OUT);
    gpio_put(LED_R_PIN, 0); gpio_put(LED_G_PIN, 0); gpio_put(LED_B_PIN, 1);
    printf("  LED RGB... OK\n");

    // Buzzers PWM
    gpio_set_function(BUZZER_A_PIN, GPIO_FUNC_PWM);
    gpio_set_function(BUZZER_B_PIN, GPIO_FUNC_PWM);
    uint sa = pwm_gpio_to_slice_num(BUZZER_A_PIN);
    uint sb = pwm_gpio_to_slice_num(BUZZER_B_PIN);
    pwm_set_wrap(sa, 62500); pwm_set_enabled(sa, true);
    pwm_set_wrap(sb, 62500); pwm_set_enabled(sb, true);
    printf("  Buzzers... OK\n\n");
}

// ============================================================================
// ESTADO INICIAL
// ============================================================================

static void system_state_init(SystemState* state) {
    memset(state, 0, sizeof(SystemState));
    state->mode            = MODE_DISARMED;
    state->screen          = SCREEN_MAIN;
    state->threat_level    = THREAT_NONE;
    state->sensitivity     = SENSITIVITY_IDX_MEDIUM;
    state->alerts_enabled  = true;
    state->battery_percent = 100;
    state->thresholds.sound_threshold = 1000.0f;
    state->thresholds.temp_change     = 2.0f;
}

// ============================================================================
// CORE 1 ENTRY
// ============================================================================

static void* core1_args[2];

void core1_entry(void) {
    multicore_fifo_pop_blocking();
    core1_interface_main(
        &g_system_state,
        (mutex_t*)core1_args[0],
        (mutex_t*)core1_args[1]
    );
}

// ============================================================================
// MAIN
// ============================================================================

int main(void) {
    hardware_init();
    system_state_init(&g_system_state);

    mutex_init(&g_state_mutex);
    mutex_init(&g_uart_mutex);
    mutex_init(&g_adc_mutex);  // Adicione esta linha

    alarm_init();
    sensors_init();
    display_init();

    printf("Sistema pronto!\n");

    core1_args[0] = &g_state_mutex;
    core1_args[1] = &g_uart_mutex;

    multicore_launch_core1(core1_entry);
    sleep_ms(100);
    multicore_fifo_push_blocking(0x01);

    core0_security_main(&g_system_state, &g_state_mutex, &g_uart_mutex);
    return 0;
}
