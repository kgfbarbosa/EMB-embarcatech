#ifndef SENSORS_H
#define SENSORS_H

#include "config.h"

// ============================================================================
// INICIALIZAÇÃO
// ============================================================================

/**
 * @brief Inicializa todos os sensores (microfone + temp interna)
 *        Inclui calibração do microfone e leitura de baseline.
 */
void sensors_init(void);

// ============================================================================
// MICROFONE
// ============================================================================

/**
 * @brief Lê nível bruto do microfone (0-4095)
 */
uint16_t microphone_read_raw(void);

/**
 * @brief Lê nível do microfone como float
 */
float microphone_read_level(void);

/**
 * @brief Lê nível filtrado (média móvel com remoção de DC)
 */
float microphone_read_filtered(void);

/**
 * @brief Calibra offset DC do microfone (chamar em silêncio)
 */
void microphone_calibrate(void);

// ============================================================================
// FUSÃO DE SENSORES
// ============================================================================

/**
 * @brief Lê todos os sensores e retorna dados consolidados
 */
SensorData sensors_read_all(void);

/**
 * @brief Atualiza baseline (condições normais da máquina)
 */
void sensors_update_baseline(void);

/**
 * @brief Retorna baseline atual
 */
SensorData sensors_get_baseline(void);

/**
 * @brief Adiciona leitura ao histórico circular (60s)
 */
void sensors_add_to_history(const SensorData* reading);

/**
 * @brief Retorna média de todas as leituras no histórico
 */
SensorData sensors_get_average(void);

/**
 * @brief Imprime diagnóstico no serial
 */
void sensors_print_diagnostic(void);

#endif // SENSORS_H
