#include "include/sensors.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include <string.h>
#include <math.h>
#include "pico/sync.h"

extern mutex_t g_adc_mutex;

// ============================================================================
// MICROFONE - Estado interno
// ============================================================================

#define MIC_FILTER_SIZE 10
static uint16_t mic_filter_buf[MIC_FILTER_SIZE];
static uint8_t  mic_filter_idx = 0;
static uint16_t mic_dc_offset  = 2048;

// ============================================================================
// FUSÃO - Estado interno
// ============================================================================

static SensorData history[60];
static uint8_t    history_index  = 0;
static bool       history_filled = false;
static SensorData baseline       = {0};

// ============================================================================
// INICIALIZAÇÃO
// ============================================================================

void sensors_init(void) {
    // ADC já inicializado em hardware_init(), apenas configura pinos
    adc_gpio_init(MIC_ADC_PIN);
    adc_set_temp_sensor_enabled(true);

    for (int i = 0; i < MIC_FILTER_SIZE; i++) mic_filter_buf[i] = 0;
    mic_filter_idx = 0;

    memset(history, 0, sizeof(history));
    history_index  = 0;
    history_filled = false;

    UART_PRINT("\n╔═══════════════════════════════════════╗\n");
    UART_PRINT("║  🔧 CALIBRANDO SENSORES              ║\n");
    UART_PRINT("╚═══════════════════════════════════════╝\n\n");

    microphone_calibrate();

    UART_PRINT("🌡️  Aguardando temperatura estabilizar...\n");
    sleep_ms(1000);

    sensors_update_baseline();

    UART_PRINT("\n✅ Sensores calibrados!\n\n");
    DEBUG_PRINT("🔗 Módulo de sensores inicializado\n");
}


// ============================================================================
// MICROFONE - Versão corrigida com mutex
// ============================================================================

uint16_t microphone_read_raw(void) {
    mutex_enter_blocking(&g_adc_mutex);
    adc_select_input(MIC_ADC_CHANNEL);
    uint16_t result = adc_read();
    mutex_exit(&g_adc_mutex);
    return result;
}

float microphone_read_level(void) {
    return (float)microphone_read_raw();
}

float microphone_read_filtered(void) {
    mutex_enter_blocking(&g_adc_mutex);
    adc_select_input(MIC_ADC_CHANNEL);
    uint16_t raw = adc_read();
    mutex_exit(&g_adc_mutex);
    
    int16_t ac = (int16_t)raw - (int16_t)mic_dc_offset;
    uint16_t rect = (uint16_t)abs(ac);

    mic_filter_buf[mic_filter_idx] = rect;
    mic_filter_idx = (mic_filter_idx + 1) % MIC_FILTER_SIZE;

    uint32_t sum = 0;
    for (int i = 0; i < MIC_FILTER_SIZE; i++) sum += mic_filter_buf[i];
    return (float)(sum / MIC_FILTER_SIZE);
}

void microphone_calibrate(void) {
    UART_PRINT("🎤 Calibrando microfone (mantenha silêncio)...\n");
    sleep_ms(1000);  // Aumentado para melhor calibração

    uint32_t sum = 0;
    const uint16_t n = 500;
    for (uint16_t i = 0; i < n; i++) {
        mutex_enter_blocking(&g_adc_mutex);
        adc_select_input(MIC_ADC_CHANNEL);
        sum += adc_read();
        mutex_exit(&g_adc_mutex);
        sleep_us(1000);
    }
    mic_dc_offset = sum / n;
    UART_PRINT("✅ DC Offset: %d\n", mic_dc_offset);
}

// ============================================================================
// TEMPERATURA INTERNA RP2040
// ============================================================================

static float read_internal_temp(void) {
    adc_select_input(INT_TEMP_ADC_CHANNEL);
    uint16_t raw  = adc_read();
    float voltage = raw * (3.3f / 4095.0f);
    return 27.0f - ((voltage - 0.706f) / 0.001721f);
}

// ============================================================================
// FUSÃO DE SENSORES
// ============================================================================

SensorData sensors_read_all(void) {
    SensorData d;
    d.sound_level = microphone_read_filtered();
    d.temperature = read_internal_temp();
    d.timestamp   = 0; // preenchido pelo chamador se necessário
    return d;
}

void sensors_update_baseline(void) {
    UART_PRINT("📊 Atualizando baseline...\n");

    const uint8_t n = 10;
    float sound_sum = 0;
    float temp_sum  = 0;

    for (uint8_t i = 0; i < n; i++) {
        sound_sum += microphone_read_filtered();
        temp_sum  += read_internal_temp();
        sleep_ms(50);
    }

    baseline.sound_level = sound_sum / n;
    baseline.temperature = temp_sum  / n;
    baseline.timestamp   = 0;

    UART_PRINT("✅ Baseline: Som=%.1f | Temp=%.1f°C\n",
           baseline.sound_level, baseline.temperature);
}

SensorData sensors_get_baseline(void) {
    return baseline;
}

void sensors_add_to_history(const SensorData* reading) {
    if (!reading) return;
    history[history_index] = *reading;
    history_index = (history_index + 1) % 60;
    if (history_index == 0) history_filled = true;
}

SensorData sensors_get_average(void) {
    SensorData avg = {0};
    uint8_t count = history_filled ? 60 : history_index;
    if (count == 0) return avg;

    for (uint8_t i = 0; i < count; i++) {
        avg.sound_level += history[i].sound_level;
        avg.temperature += history[i].temperature;
    }
    avg.sound_level /= count;
    avg.temperature /= count;
    return avg;
}

void sensors_print_diagnostic(void) {
    SensorData cur = sensors_read_all();
    SensorData avg = sensors_get_average();

    UART_PRINT("\n╔══════════════════════════════════════════╗\n");
    UART_PRINT("║       DIAGNÓSTICO DE SENSORES            ║\n");
    UART_PRINT("╠══════════════════════════════════════════╣\n");
    UART_PRINT("║  🎤 MICROFONE                            ║\n");
    UART_PRINT("║     Atual:    %.1f                      ║\n", cur.sound_level);
    UART_PRINT("║     Baseline: %.1f                      ║\n", baseline.sound_level);
    UART_PRINT("║     Média:    %.1f                      ║\n", avg.sound_level);
    UART_PRINT("║                                          ║\n");
    UART_PRINT("║  🌡️  TEMPERATURA INTERNA                 ║\n");
    UART_PRINT("║     Atual:    %.1f°C                    ║\n", cur.temperature);
    UART_PRINT("║     Baseline: %.1f°C                    ║\n", baseline.temperature);
    UART_PRINT("║     Média:    %.1f°C                    ║\n", avg.temperature);
    UART_PRINT("╚══════════════════════════════════════════╝\n\n");
}
