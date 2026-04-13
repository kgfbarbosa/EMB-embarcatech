#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "pico/sync.h"

// ============================================================================
// CONFIGURAÇÃO DE HARDWARE - BitDogLab V7
// ============================================================================

// Display OLED SSD1306 (I2C1)
// BitDogLab V7: SDA=GP14, SCL=GP15  (i2c1)
#define DISPLAY_I2C_PORT    i2c1
#define DISPLAY_SDA_PIN     14
#define DISPLAY_SCL_PIN     15
#define DISPLAY_I2C_ADDR    0x3C
#define DISPLAY_WIDTH       128
#define DISPLAY_HEIGHT      64
#define DISPLAY_I2C_FREQ    400000

// Microfone Analógico
#define MIC_ADC_PIN         28      // ADC0
#define MIC_ADC_CHANNEL     2
#define MIC_SAMPLE_RATE     100
#define MIC_BUFFER_SIZE     512

// Sensor de Temperatura Interno RP2040
#define INT_TEMP_ADC_CHANNEL 4

// Joystick Analógico
#define JOY_X_ADC_PIN       26      // ADC2
#define JOY_X_ADC_CHANNEL   0
#define JOY_Y_ADC_PIN       29      // ADC3
#define JOY_Y_ADC_CHANNEL   3
#define JOY_SW_PIN          22

// Limiar de detecção — zona morta ampliada para evitar leituras fantasma
#define JOY_THRESHOLD_HIGH  3200
#define JOY_THRESHOLD_LOW    800
// Número mínimo de leituras consecutivas na mesma direção antes de confirmar
#define JOY_CONFIRM_COUNT     3

// Botões
#define BUTTON_A_PIN        5
#define BUTTON_B_PIN        6

// Buzzer Estéreo
#define BUZZER_A_PIN        10
#define BUZZER_B_PIN        11

// LED RGB Comum
#define LED_R_PIN           13
#define LED_G_PIN           17
#define LED_B_PIN           16

// ============================================================================
// CONFIGURAÇÃO DO SISTEMA DE APRENDIZADO
// ============================================================================

#define LEARNING_DURATION_MS    5000
#define LEARNING_SAMPLE_RATE    100
#define LEARNING_SAMPLES        (LEARNING_DURATION_MS / (1000 / LEARNING_SAMPLE_RATE))
#define OUTLIER_PERCENTILE      0.1f
#define THRESHOLD_STDDEV_MULT   3.0f

#define SENSITIVITY_LOW     4.0f
#define SENSITIVITY_MEDIUM  3.0f
#define SENSITIVITY_HIGH    2.0f

#define THREAT_DEBOUNCE_MS      2000
#define SNOOZE_DURATION_MS      300000

// ============================================================================
// ENUMS E TIPOS
// ============================================================================

typedef enum {
    MODE_DISARMED,
    MODE_LEARNING,
    MODE_ARMED,
    MODE_TRIGGERED,
    MODE_SNOOZED
} SystemMode;

typedef enum {
    SCREEN_MAIN,
    SCREEN_STATUS,
    SCREEN_LEARNING,
    SCREEN_ALERT
} ScreenMode;

typedef enum {
    THREAT_NONE = 0,
    THREAT_SUSPICIOUS = 1,
    THREAT_PROBABLE = 2,
    THREAT_CONFIRMED = 3
} ThreatLevel;

typedef enum {
    SENSITIVITY_IDX_HIGH   = 0,
    SENSITIVITY_IDX_MEDIUM = 1,
    SENSITIVITY_IDX_LOW    = 2
} SensitivityIndex;

typedef enum {
    JOY_NONE  = 0,
    JOY_UP    = 1,
    JOY_RIGHT = 2,
    JOY_DOWN  = 3,
    JOY_LEFT  = 4
} JoystickDir;

// ============================================================================
// ESTRUTURAS
// ============================================================================

typedef struct {
    float sound_level;
    float temperature;
    uint32_t timestamp;
} SensorData;

typedef struct {
    float samples[LEARNING_SAMPLES];
    uint16_t sample_count;
    float mean;
    float std_dev;
    float threshold;
    bool is_learned;
} AcousticSignature;

typedef struct {
    float sound_threshold;
    float temp_change;
} AlertThresholds;

typedef struct {
    SystemMode mode;
    ScreenMode screen;
    ThreatLevel threat_level;
    SensitivityIndex sensitivity;

    SensorData current_reading;
    SensorData baseline_reading;
    SensorData history[60];
    uint8_t history_index;

    AcousticSignature acoustic_sig;
    AlertThresholds thresholds;

    bool alerts_enabled;
    uint8_t battery_percent;

    uint32_t armed_timestamp;
    uint32_t last_alert_timestamp;
    uint32_t snooze_until_ms;
    uint32_t uptime_seconds;
} SystemState;

// ============================================================================
// PERFORMANCE
// ============================================================================

#define SENSOR_READ_INTERVAL_MS     100
#define THREAT_CHECK_INTERVAL_MS    500
#define DISPLAY_UPDATE_RATE_MS      33
#define INPUT_POLL_RATE_MS          20
#define LED_ANIMATION_RATE_MS       50

// ============================================================================
// MACROS
// ============================================================================

#define CLAMP(x, min, max) ((x)<(min)?(min):((x)>(max)?(max):(x)))

// ============================================================================
// UART MUTEX — serializa printf entre Core 0 e Core 1
// ============================================================================

extern mutex_t g_uart_mutex;

#define UART_PRINT(...)                       \
    do {                                      \
        mutex_enter_blocking(&g_uart_mutex);  \
        printf(__VA_ARGS__);                  \
        mutex_exit(&g_uart_mutex);            \
    } while (0)

// ============================================================================
// DEBUG
// ============================================================================

#define DEBUG_ENABLED 1

#if DEBUG_ENABLED
    #define DEBUG_PRINT(...) UART_PRINT(__VA_ARGS__)
#else
    #define DEBUG_PRINT(...)
#endif

#endif // CONFIG_H
