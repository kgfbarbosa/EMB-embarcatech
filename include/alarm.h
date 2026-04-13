#ifndef ALARM_H
#define ALARM_H

#include "config.h"

// ============================================================================
// ESTRUTURA DE ANÁLISE DE AMEAÇA
// ============================================================================

typedef struct {
    bool sound_threat;
    ThreatLevel level;
    uint32_t detection_time;
    char description[128];
} ThreatAnalysis;

// ============================================================================
// APRENDIZADO ACÚSTICO
// ============================================================================

/**
 * @brief Inicializa módulo de alarme
 */
void alarm_init(void);

/**
 * @brief Executa calibração do ambiente acústico da máquina
 * @param sig Ponteiro para estrutura que armazenará a assinatura
 * @return true se calibração bem-sucedida
 */
bool alarm_learn_environment(AcousticSignature* sig);

/**
 * @brief Verifica se nível de som está acima do threshold
 */
bool alarm_is_sound_anomalous(const AcousticSignature* sig, float sound_level);

/**
 * @brief Recalcula threshold com novo multiplicador de σ (sem reaprender)
 * @param sig Assinatura já aprendida
 * @param stddev_mult Multiplicador (ex: 2.0, 3.0, 4.0)
 */
void alarm_recalculate_threshold(AcousticSignature* sig, float stddev_mult);

/**
 * @brief Calcula estatísticas robustas ignorando outliers
 */
void alarm_calculate_statistics(const float* samples, size_t count,
                                float* mean, float* std_dev);

/**
 * @brief Retorna progresso da calibração (0-100%)
 */
uint8_t alarm_get_learning_progress(void);

/**
 * @brief Reseta assinatura acústica
 */
void alarm_reset_signature(AcousticSignature* sig);

// ============================================================================
// DETECÇÃO DE AMEAÇAS
// ============================================================================

/**
 * @brief Detecta anomalia de ruído baseado na assinatura aprendida
 * @param current Leitura atual
 * @param baseline Leitura baseline
 * @param sig Assinatura acústica aprendida
 * @param analysis Estrutura para resultado (pode ser NULL)
 * @return Nível de ameaça
 */
ThreatLevel alarm_detect(const SensorData* current,
                         const SensorData* baseline,
                         const AcousticSignature* sig,
                         ThreatAnalysis* analysis);

/**
 * @brief Converte ThreatLevel para string
 */
const char* alarm_threat_to_string(ThreatLevel level);

/**
 * @brief Retorna cor RGB correspondente ao nível de ameaça
 */
void alarm_get_threat_color(ThreatLevel level,
                            uint8_t* r, uint8_t* g, uint8_t* b);

#endif // ALARM_H
