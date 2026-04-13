#include "include/alarm.h"
#include "include/sensors.h"
#include "pico/stdlib.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Estado interno
// ============================================================================

static uint16_t learning_progress = 0;
static uint32_t last_threat_time  = 0;

// Comparador para qsort
static int cmp_float(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

// ============================================================================
// INICIALIZAÇÃO
// ============================================================================

void alarm_init(void) {
    learning_progress = 0;
    last_threat_time  = 0;
    DEBUG_PRINT("🔔 Módulo de alarme inicializado\n");
}

// ============================================================================
// APRENDIZADO ACÚSTICO
// ============================================================================

bool alarm_learn_environment(AcousticSignature* sig) {
    if (!sig) return false;

    UART_PRINT("\n╔════════════════════════════════════════╗\n");
    UART_PRINT("║  🧠 CALIBRANDO RUÍDO DA MÁQUINA      ║\n");
    UART_PRINT("╚════════════════════════════════════════╝\n\n");
    UART_PRINT("🔊 Analisando por %d segundos...\n", LEARNING_DURATION_MS / 1000);
    UART_PRINT("📊 Mantenha a máquina em operação normal.\n\n");

    memset(sig, 0, sizeof(AcousticSignature));
    sig->is_learned = false;
    learning_progress = 0;

    uint32_t last_print = to_ms_since_boot(get_absolute_time());

    for (uint16_t i = 0; i < LEARNING_SAMPLES; i++) {
        sig->samples[i] = microphone_read_filtered();
        sig->sample_count++;
        learning_progress = (i * 100) / LEARNING_SAMPLES;

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_print >= 500) {
            UART_PRINT("⏳ %d%% [", learning_progress);
            for (int j = 0; j < 20; j++)
                UART_PRINT(j < (learning_progress / 5) ? "█" : "░");
            UART_PRINT("]\n");
            last_print = now;
        }

        sleep_ms(1000 / LEARNING_SAMPLE_RATE);
    }

    learning_progress = 100;
    UART_PRINT("\n✅ Coleta concluída! Processando...\n");

    alarm_calculate_statistics(sig->samples, sig->sample_count,
                               &sig->mean, &sig->std_dev);

    sig->threshold  = sig->mean + (THRESHOLD_STDDEV_MULT * sig->std_dev);
    sig->is_learned = true;

    UART_PRINT("\n╔════════════════════════════════════════╗\n");
    UART_PRINT("║     📊 ASSINATURA ACÚSTICA            ║\n");
    UART_PRINT("╠════════════════════════════════════════╣\n");
    UART_PRINT("║  Média (μ):   %.2f                    ║\n", sig->mean);
    UART_PRINT("║  Desvio (σ):  %.2f                    ║\n", sig->std_dev);
    UART_PRINT("║  Threshold:   %.2f                    ║\n", sig->threshold);
    UART_PRINT("╚════════════════════════════════════════╝\n\n");

    return true;
}

void alarm_calculate_statistics(const float* samples, size_t count,
                                float* mean, float* std_dev) {
    if (!samples || count == 0 || !mean || !std_dev) return;

    float* sorted = (float*)malloc(count * sizeof(float));
    if (!sorted) return;
    memcpy(sorted, samples, count * sizeof(float));
    qsort(sorted, count, sizeof(float), cmp_float);

    size_t start = (size_t)(count * OUTLIER_PERCENTILE);
    size_t end   = (size_t)(count * (1.0f - OUTLIER_PERCENTILE));
    size_t valid = end - start;

    *mean = 0.0f;
    for (size_t i = start; i < end; i++) *mean += sorted[i];
    *mean /= valid;

    *std_dev = 0.0f;
    for (size_t i = start; i < end; i++) {
        float d = sorted[i] - (*mean);
        *std_dev += d * d;
    }
    *std_dev = sqrtf(*std_dev / valid);

    free(sorted);
    DEBUG_PRINT("📈 μ=%.2f σ=%.2f (válidas: %d/%d)\n", *mean, *std_dev, valid, count);
}

void alarm_recalculate_threshold(AcousticSignature* sig, float stddev_mult) {
    if (!sig || !sig->is_learned) return;
    sig->threshold = sig->mean + (stddev_mult * sig->std_dev);
    UART_PRINT("🎛️  Threshold recalculado: %.2f (%.1fσ)\n", sig->threshold, stddev_mult);
}

bool alarm_is_sound_anomalous(const AcousticSignature* sig, float sound_level) {
    if (!sig || !sig->is_learned) return false;
    return (sound_level > sig->threshold);
}

void alarm_reset_signature(AcousticSignature* sig) {
    if (!sig) return;
    memset(sig, 0, sizeof(AcousticSignature));
    sig->is_learned = false;
    learning_progress = 0;
    DEBUG_PRINT("🔄 Assinatura acústica resetada\n");
}

uint8_t alarm_get_learning_progress(void) {
    return (uint8_t)learning_progress;
}

// ============================================================================
// DETECÇÃO DE AMEAÇAS
// ============================================================================

ThreatLevel alarm_detect(const SensorData* current,
                         const SensorData* baseline,
                         const AcousticSignature* sig,
                         ThreatAnalysis* analysis) {
    if (!current || !baseline || !sig) return THREAT_NONE;

    ThreatAnalysis local = {0};
    local.detection_time = to_ms_since_boot(get_absolute_time());

    // Detecção acústica
    if (sig->is_learned && alarm_is_sound_anomalous(sig, current->sound_level)) {
        local.sound_threat = true;
        float excess_pct = ((current->sound_level - sig->threshold) / sig->threshold) * 100.0f;
        DEBUG_PRINT("⚠️  [SOM] %.2f > %.2f (+%.1f%%)\n",
                    current->sound_level, sig->threshold, excess_pct);
    }

    // Nível de ameaça com debounce
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool debounce = (now - last_threat_time) < THREAT_DEBOUNCE_MS;

    if (!local.sound_threat) {
        local.level = THREAT_NONE;
        strcpy(local.description, "Nenhuma anomalia");
    } else if (debounce) {
        local.level = THREAT_NONE;
        strcpy(local.description, "Debounce ativo");
    } else {
        // Calcula intensidade para diferenciar PROBABLE vs CONFIRMED
        float excess = current->sound_level - sig->threshold;
        float excess_pct = (sig->threshold > 0) ? (excess / sig->threshold * 100.0f) : 0;

        if (excess_pct > 50.0f) {
            local.level = THREAT_CONFIRMED;
            snprintf(local.description, sizeof(local.description),
                     "RUÍDO CRÍTICO: %.0f (lim: %.0f)", current->sound_level, sig->threshold);
        } else {
            local.level = THREAT_PROBABLE;
            snprintf(local.description, sizeof(local.description),
                     "Ruído elevado: %.0f (lim: %.0f)", current->sound_level, sig->threshold);
        }
        last_threat_time = now;
    }

    if (local.level != THREAT_NONE) {
        UART_PRINT("\n╔══════════════════════════════════════════════╗\n");
        UART_PRINT("║  🚨 ANOMALIA DETECTADA                      ║\n");
        UART_PRINT("╠══════════════════════════════════════════════╣\n");
        UART_PRINT("║  Nível: %-36s ║\n", alarm_threat_to_string(local.level));
        UART_PRINT("║  %s\n", local.description);
        UART_PRINT("╚══════════════════════════════════════════════╝\n\n");
    }

    if (analysis) memcpy(analysis, &local, sizeof(ThreatAnalysis));
    return local.level;
}

const char* alarm_threat_to_string(ThreatLevel level) {
    switch (level) {
        case THREAT_NONE:       return "NORMAL";
        case THREAT_SUSPICIOUS: return "SUSPEITA";
        case THREAT_PROBABLE:   return "ELEVADO";
        case THREAT_CONFIRMED:  return "CRITICO";
        default:                return "DESCONHECIDO";
    }
}

void alarm_get_threat_color(ThreatLevel level,
                            uint8_t* r, uint8_t* g, uint8_t* b) {
    switch (level) {
        case THREAT_NONE:
            *r = 0;   *g = 255; *b = 0;   break; // Verde
        case THREAT_SUSPICIOUS:
            *r = 255; *g = 255; *b = 0;   break; // Amarelo
        case THREAT_PROBABLE:
            *r = 255; *g = 128; *b = 0;   break; // Laranja
        case THREAT_CONFIRMED:
            *r = 255; *g = 0;   *b = 0;   break; // Vermelho
        default:
            *r = 128; *g = 128; *b = 128; break; // Cinza
    }
}
