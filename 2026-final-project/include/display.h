#ifndef DISPLAY_H
#define DISPLAY_H

#include "config.h"
#include "hardware/dma.h"   // necessário para DMA no display

// ============================================================================
// SSD1306 - CONSTANTES
// ============================================================================

#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      64
#define SSD1306_PAGES       (SSD1306_HEIGHT / 8)
#define SSD1306_BUFFER_SIZE (SSD1306_WIDTH * SSD1306_PAGES)

// ============================================================================
// SSD1306 - INICIALIZAÇÃO E CONTROLE
// ============================================================================

bool    ssd1306_init(void);
void    ssd1306_show(void);
void    ssd1306_clear(void);
void    ssd1306_power(bool on);
void    ssd1306_set_contrast(uint8_t contrast);
uint8_t* ssd1306_get_buffer(void);

// ============================================================================
// SSD1306 - PRIMITIVAS DE DESENHO
// ============================================================================

void ssd1306_draw_pixel(int x, int y, bool on);
void ssd1306_draw_hline(int x, int y, int w, bool on);
void ssd1306_draw_vline(int x, int y, int h, bool on);
void ssd1306_draw_rect(int x, int y, int w, int h, bool on);
void ssd1306_fill_rect(int x, int y, int w, int h, bool on);
int  ssd1306_draw_char(int x, int y, char c, bool on);
int  ssd1306_draw_string(int x, int y, const char* str, bool on);
void ssd1306_draw_string_centered(int y, int width, const char* str, bool on);
int  ssd1306_draw_string_large(int x, int y, const char* str, bool on);
void ssd1306_draw_progress_bar(int x, int y, int w, int h, uint8_t percent, bool on);

// ============================================================================
// UI - INICIALIZAÇÃO E DISPATCHER
// ============================================================================

/**
 * @brief Inicializa display e exibe splash screen
 */
void display_init(void);

/**
 * @brief Renderiza a tela correta baseada no estado atual
 */
void display_draw_screen(SystemState* state);

// ============================================================================
// UI - TELAS INDIVIDUAIS
// ============================================================================

void ui_screen_main(SystemState* state);
void ui_screen_learning(SystemState* state);
void ui_screen_armed(SystemState* state);
void ui_screen_alert(SystemState* state);
void ui_screen_status(SystemState* state);

#endif // DISPLAY_H
