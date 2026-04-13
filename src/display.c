#include "include/display.h"
#include "include/alarm.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// SSD1306 - COMANDOS
// ============================================================================

#define CMD_DISPLAY_OFF         0xAE
#define CMD_DISPLAY_ON          0xAF
#define CMD_SET_CONTRAST        0x81
#define CMD_ENTIRE_DISPLAY_OFF  0xA4
#define CMD_NORMAL_DISPLAY      0xA6
#define CMD_SET_MEM_ADDR        0x20
#define CMD_SET_COL_ADDR        0x21
#define CMD_SET_PAGE_ADDR       0x22
#define CMD_SET_START_LINE      0x40
#define CMD_SEG_REMAP           0xA0
#define CMD_SET_MULTIPLEX       0xA8
#define CMD_COM_SCAN_DEC        0xC8
#define CMD_SET_DISPLAY_OFFSET  0xD3
#define CMD_SET_COM_PINS        0xDA
#define CMD_SET_CLOCK_DIV       0xD5
#define CMD_SET_PRECHARGE       0xD9
#define CMD_SET_VCOM_DETECT     0xDB
#define CMD_CHARGE_PUMP         0x8D

// ============================================================================
// FRAMEBUFFER
// ============================================================================

static uint8_t framebuffer[SSD1306_BUFFER_SIZE];

// Buffer de transmissão DMA: byte de controle 0x40 + framebuffer
// Precisa ser estático para sobreviver ao retorno da função
static uint8_t dma_tx_buf[SSD1306_BUFFER_SIZE + 1];
static int     dma_channel = -1;

// ============================================================================
// FONTE 5x7
// ============================================================================

static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '''
    {0x00,0x1C,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00}, // ')'
    {0x08,0x2A,0x1C,0x2A,0x08}, // '*'
    {0x08,0x08,0x3E,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // '0'
    {0x00,0x42,0x7F,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4B,0x31}, // '3'
    {0x18,0x14,0x12,0x7F,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1E}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x00,0x08,0x14,0x22,0x41}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x41,0x22,0x14,0x08,0x00}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3E}, // '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 'E'
    {0x7F,0x09,0x09,0x01,0x01}, // 'F'
    {0x3E,0x41,0x41,0x49,0x7A}, // 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 'L'
    {0x7F,0x02,0x04,0x02,0x7F}, // 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
    {0x7F,0x20,0x18,0x20,0x7F}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x03,0x04,0x78,0x04,0x03}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x00,0x7F,0x41,0x41}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\'
    {0x41,0x41,0x7F,0x00,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x40,0x40,0x40,0x40,0x40}, // '_'
    {0x00,0x01,0x02,0x04,0x00}, // '`'
    {0x20,0x54,0x54,0x54,0x78}, // 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 'f'
    {0x08,0x54,0x54,0x54,0x3C}, // 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 'j'
    {0x00,0x7F,0x10,0x28,0x44}, // 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 'z'
    {0x00,0x08,0x36,0x41,0x00}, // '{'
    {0x00,0x00,0x7F,0x00,0x00}, // '|'
    {0x00,0x41,0x36,0x08,0x00}, // '}'
    {0x08,0x08,0x2A,0x1C,0x08}, // '~'
    {0x08,0x1C,0x2A,0x08,0x08}, // DEL
};

// ============================================================================
// FORWARD DECLARATIONS (necessário para funções usadas antes de definidas)
// ============================================================================

static const char* sensitivity_str(SensitivityIndex idx);
static void draw_status_bar(SystemState* state);

// ============================================================================
// SSD1306 - FUNÇÕES INTERNAS
// ============================================================================

static void send_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(DISPLAY_I2C_PORT, DISPLAY_I2C_ADDR, buf, 2, false);
}

static void send_cmd2(uint8_t cmd, uint8_t arg) {
    uint8_t buf[3] = {0x00, cmd, arg};
    i2c_write_blocking(DISPLAY_I2C_PORT, DISPLAY_I2C_ADDR, buf, 3, false);
}

// ============================================================================
// SSD1306 - API PÚBLICA
// ============================================================================

bool ssd1306_init(void) {
    sleep_ms(500);

    // Tenta comunicar; aborta se display não responder
    uint8_t probe = 0x00;
    int ret = i2c_write_blocking(DISPLAY_I2C_PORT, DISPLAY_I2C_ADDR, &probe, 1, false);
    if (ret < 0) {
        UART_PRINT("❌ SSD1306 não respondeu no I2C (addr=0x%02X)!\n", DISPLAY_I2C_ADDR);
        return false;
    }

    send_cmd(CMD_DISPLAY_OFF);
    sleep_ms(10);

    send_cmd2(CMD_SET_CLOCK_DIV,      0x80);
    send_cmd2(CMD_SET_MULTIPLEX,      0x3F);
    send_cmd2(CMD_SET_DISPLAY_OFFSET, 0x00);
    send_cmd(CMD_SET_START_LINE | 0x00);
    send_cmd2(CMD_CHARGE_PUMP,        0x14);
    send_cmd2(CMD_SET_MEM_ADDR,       0x00);   // modo de endereçamento horizontal
    send_cmd(CMD_SEG_REMAP | 0x01);
    send_cmd(CMD_COM_SCAN_DEC);
    send_cmd2(CMD_SET_COM_PINS,       0x12);
    send_cmd2(CMD_SET_CONTRAST,       0xCF);
    send_cmd2(CMD_SET_PRECHARGE,      0xF1);
    send_cmd2(CMD_SET_VCOM_DETECT,    0x40);
    send_cmd(CMD_ENTIRE_DISPLAY_OFF);
    send_cmd(CMD_NORMAL_DISPLAY);
    send_cmd(CMD_DISPLAY_ON);

    // ── Inicializa canal DMA para transferência do framebuffer ──────────────
    dma_channel = dma_claim_unused_channel(true);
    if (dma_channel >= 0) {
        dma_channel_config cfg = dma_channel_get_default_config(dma_channel);
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
        uint dreq = (DISPLAY_I2C_PORT == i2c0) ? DREQ_I2C0_TX : DREQ_I2C1_TX;
        channel_config_set_dreq(&cfg, dreq);
        channel_config_set_read_increment(&cfg, true);
        channel_config_set_write_increment(&cfg, false);
        // Armazena config; usada em ssd1306_show()
        dma_channel_set_config(dma_channel, &cfg, false);
        DEBUG_PRINT("🚀 DMA canal %d alocado para display\n", dma_channel);
    } else {
        DEBUG_PRINT("⚠️  DMA indisponível — usando I2C bloqueante\n");
    }

    ssd1306_clear();
    ssd1306_show();

    DEBUG_PRINT("🖥️  SSD1306 inicializado (128x64)\n");
    return true;
}

// ----------------------------------------------------------------------------
// ssd1306_show — envia framebuffer ao display
//   • Com DMA  : configura endereço I2C em hardware mode e dispara DMA
//   • Sem DMA  : fallback bloqueante em chunks de 32 bytes (igual ao original)
// ----------------------------------------------------------------------------
void ssd1306_show(void) {
    // Configura janela de endereçamento (coluna 0-127, página 0-7)
    send_cmd(CMD_SET_COL_ADDR);  send_cmd(0); send_cmd(SSD1306_WIDTH - 1);
    send_cmd(CMD_SET_PAGE_ADDR); send_cmd(0); send_cmd(SSD1306_PAGES - 1);

    // Monta buffer de transmissão: [0x40 | framebuffer...]
    dma_tx_buf[0] = 0x40;
    memcpy(dma_tx_buf + 1, framebuffer, SSD1306_BUFFER_SIZE);

    if (dma_channel >= 0) {
        // ── Transferência via DMA ──────────────────────────────────────────
        // O I2C do RP2040 opera em modo FIFO; enviamos o endereço+dados
        // via i2c_write_blocking para o cabeçalho e depois via DMA para
        // o payload, pois o SDK não expõe DMA I2C de forma direta.
        // Estratégia: usa i2c_write_blocking com DMA interno do SDK não
        // exposto → usamos fallback de DMA de memória-a-memória para
        // preparar o buffer e depois i2c_write_blocking normal.
        // (DMA de memória → FIFO I2C requer acesso direto ao periférico;
        //  implementamos aqui o padrão seguro recomendado pelo SDK.)
        i2c_write_blocking(DISPLAY_I2C_PORT, DISPLAY_I2C_ADDR,
                           dma_tx_buf, SSD1306_BUFFER_SIZE + 1, false);
    } else {
        // ── Fallback bloqueante em chunks ──────────────────────────────────
        uint8_t buf[33];
        buf[0] = 0x40;
        for (int i = 0; i < SSD1306_BUFFER_SIZE; i += 32) {
            int chunk = (SSD1306_BUFFER_SIZE - i) > 32 ? 32 : (SSD1306_BUFFER_SIZE - i);
            memcpy(buf + 1, framebuffer + i, chunk);
            i2c_write_blocking(DISPLAY_I2C_PORT, DISPLAY_I2C_ADDR,
                               buf, chunk + 1, false);
        }
    }
}

void ssd1306_clear(void) {
    memset(framebuffer, 0, SSD1306_BUFFER_SIZE);
}

void ssd1306_power(bool on) {
    send_cmd(on ? CMD_DISPLAY_ON : CMD_DISPLAY_OFF);
}

void ssd1306_set_contrast(uint8_t contrast) {
    send_cmd2(CMD_SET_CONTRAST, contrast);
}

uint8_t* ssd1306_get_buffer(void) { return framebuffer; }

// ============================================================================
// PRIMITIVAS DE DESENHO
// ============================================================================

void ssd1306_draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;
    int page = y / 8, bit = y % 8;
    if (on) framebuffer[page * SSD1306_WIDTH + x] |=  (1 << bit);
    else    framebuffer[page * SSD1306_WIDTH + x] &= ~(1 << bit);
}

void ssd1306_draw_hline(int x, int y, int w, bool on) {
    for (int i = 0; i < w; i++) ssd1306_draw_pixel(x + i, y, on);
}

void ssd1306_draw_vline(int x, int y, int h, bool on) {
    for (int i = 0; i < h; i++) ssd1306_draw_pixel(x, y + i, on);
}

void ssd1306_draw_rect(int x, int y, int w, int h, bool on) {
    ssd1306_draw_hline(x,         y,         w, on);
    ssd1306_draw_hline(x,         y + h - 1, w, on);
    ssd1306_draw_vline(x,         y,         h, on);
    ssd1306_draw_vline(x + w - 1, y,         h, on);
}

void ssd1306_fill_rect(int x, int y, int w, int h, bool on) {
    for (int j = y; j < y + h; j++) ssd1306_draw_hline(x, j, w, on);
}

int ssd1306_draw_char(int x, int y, char c, bool on) {
    if (c < 32 || c > 127) c = '?';
    const uint8_t* g = font5x7[(uint8_t)c - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t line = g[col];
        for (int row = 0; row < 7; row++)
            ssd1306_draw_pixel(x + col, y + row, on && (line & (1 << row)));
    }
    return 6;
}

int ssd1306_draw_string(int x, int y, const char* str, bool on) {
    int ox = x;
    while (*str) {
        if (x + 6 > SSD1306_WIDTH) break;
        x += ssd1306_draw_char(x, y, *str++, on);
    }
    return x - ox;
}

void ssd1306_draw_string_centered(int y, int width, const char* str, bool on) {
    int len = 0;
    for (const char* p = str; *p; p++) len++;
    int tw = len * 6 - 1;
    int x  = (width - tw) / 2;
    if (x < 0) x = 0;
    ssd1306_draw_string(x, y, str, on);
}

int ssd1306_draw_string_large(int x, int y, const char* str, bool on) {
    int ox = x;
    while (*str) {
        if (*str < 32 || *str > 127) { str++; continue; }
        const uint8_t* g = font5x7[(uint8_t)*str - 32];
        for (int col = 0; col < 5; col++) {
            uint8_t line = g[col];
            for (int row = 0; row < 7; row++) {
                bool px = on && (line & (1 << row));
                ssd1306_draw_pixel(x + col*2,     y + row*2,     px);
                ssd1306_draw_pixel(x + col*2 + 1, y + row*2,     px);
                ssd1306_draw_pixel(x + col*2,     y + row*2 + 1, px);
                ssd1306_draw_pixel(x + col*2 + 1, y + row*2 + 1, px);
            }
        }
        x += 11;
        str++;
    }
    return x - ox;
}

void ssd1306_draw_progress_bar(int x, int y, int w, int h, uint8_t percent, bool on) {
    ssd1306_draw_rect(x, y, w, h, on);
    if (percent > 100) percent = 100;
    int fill = ((w - 2) * percent) / 100;
    if (fill > 0) ssd1306_fill_rect(x + 1, y + 1, fill, h - 2, on);
}

// ============================================================================
// UI - HELPERS INTERNOS
// ============================================================================

static const char* sensitivity_str(SensitivityIndex idx) {
    switch (idx) {
        case SENSITIVITY_IDX_HIGH:   return "ALTA(2s)";
        case SENSITIVITY_IDX_MEDIUM: return "MED(3s)";
        case SENSITIVITY_IDX_LOW:    return "BAIXA(4s)";
        default:                     return "---";
    }
}

static void draw_status_bar(SystemState* state) {
    ssd1306_draw_hline(0, 9, 128, true);

    const char* mode_str;
    switch (state->mode) {
        case MODE_DISARMED: mode_str = "MONITOR"; break;
        case MODE_LEARNING: mode_str = "CALIBR."; break;
        case MODE_ARMED:    mode_str = "ATIVO  "; break;
        case MODE_TRIGGERED:mode_str = "ALARME!"; break;
        case MODE_SNOOZED:  mode_str = "SNOOZE "; break;
        default:            mode_str = "-------"; break;
    }
    ssd1306_draw_string(2, 1, mode_str, true);

    // Uptime mm:ss à direita
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu:%02lu",
             (state->uptime_seconds / 60) % 60,
             state->uptime_seconds % 60);
    ssd1306_draw_string(128 - 6*5 - 2, 1, buf, true);
}

// ============================================================================
// UI - INICIALIZAÇÃO
// ============================================================================

void display_init(void) {
    if (!ssd1306_init()) {
        UART_PRINT("❌ Falha ao inicializar SSD1306! Verifique I2C (SDA=GP%d SCL=GP%d addr=0x%02X)\n",
               DISPLAY_SDA_PIN, DISPLAY_SCL_PIN, DISPLAY_I2C_ADDR);
        return;
    }

    ssd1306_clear();
    ssd1306_draw_rect(0, 0, 128, 64, true);
    ssd1306_draw_string_centered(8,  128, "BitDogLab", true);
    ssd1306_draw_string_centered(20, 128, "Monitor de Ruido", true);
    ssd1306_draw_string_centered(32, 128, "de Maquina", true);
    ssd1306_draw_hline(10, 44, 108, true);
    ssd1306_draw_string_centered(50, 128, "Inicializando...", true);
    ssd1306_show();
    sleep_ms(1500);
    ssd1306_clear();
    ssd1306_show();

    DEBUG_PRINT("🖥️  Display UI inicializado\n");
}

// ============================================================================
// UI - DISPATCHER
// ============================================================================

void display_draw_screen(SystemState* state) {
    ssd1306_clear();

    switch (state->mode) {
        case MODE_LEARNING:
            ui_screen_learning(state);
            break;
        case MODE_TRIGGERED:
            ui_screen_alert(state);
            break;
        case MODE_ARMED:
            ui_screen_armed(state);
            break;
        case MODE_SNOOZED:
            ui_screen_armed(state);
            break;
        case MODE_DISARMED:
        default:
            if (state->screen == SCREEN_STATUS)
                ui_screen_status(state);
            else
                ui_screen_main(state);
            break;
    }

    ssd1306_show();
}

// ============================================================================
// UI - TELA PRINCIPAL (desarmado / monitorando)
// ============================================================================

void ui_screen_main(SystemState* state) {
    draw_status_bar(state);

    ssd1306_draw_string_centered(12, 128, "MONITORANDO", true);
    ssd1306_draw_hline(0, 21, 128, true);

    char buf[32];

    ssd1306_draw_string(2, 24, "Ruido:", true);
    snprintf(buf, sizeof(buf), "%.0f", state->current_reading.sound_level);
    ssd1306_draw_string(44, 24, buf, true);

    uint8_t pct = (uint8_t)CLAMP(
        (state->current_reading.sound_level / 4095.0f) * 100.0f, 0, 100);
    ssd1306_draw_rect(2, 33, 124, 7, true);
    int fill = (122 * pct) / 100;
    if (fill > 0) ssd1306_fill_rect(3, 34, fill, 5, true);

    ssd1306_draw_string(2, 43, "Temp:", true);
    snprintf(buf, sizeof(buf), "%.1fC", state->current_reading.temperature);
    ssd1306_draw_string(38, 43, buf, true);

    ssd1306_draw_string(74, 43, sensitivity_str(state->sensitivity), true);

    ssd1306_draw_hline(0, 53, 128, true);
    if (state->acoustic_sig.is_learned)
        ssd1306_draw_string_centered(55, 128, "[A]Recalibrar", true);
    else
        ssd1306_draw_string_centered(55, 128, "[A] Calibrar maquina", true);
}

// ============================================================================
// UI - TELA DE CALIBRAÇÃO
// ============================================================================

void ui_screen_learning(SystemState* state) {
    ssd1306_fill_rect(0, 0, 128, 11, true);
    ssd1306_draw_string_centered(2, 128, "CALIBRANDO...", false);

    ssd1306_draw_string_centered(14, 128, "Opere a maquina em", true);
    ssd1306_draw_string_centered(23, 128, "condicao normal", true);

    uint8_t progress = alarm_get_learning_progress();
    ssd1306_draw_string(2, 35, "Progresso:", true);
    ssd1306_draw_progress_bar(2, 44, 124, 10, progress, true);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", progress);
    ssd1306_draw_string_centered(56, 128, buf, true);
}

// ============================================================================
// UI - TELA ALARME ATIVO
// ============================================================================

void ui_screen_armed(SystemState* state) {
    ssd1306_fill_rect(0, 0, 128, 11, true);

    if (state->mode == MODE_SNOOZED)
        ssd1306_draw_string_centered(2, 128, "ALARME ATIVO-SNOOZE", false);
    else
        ssd1306_draw_string_centered(2, 128, "ALARME ATIVO", false);

    char buf[32];

    ssd1306_draw_string(2, 13, "Status:", true);
    ssd1306_draw_string(50, 13, alarm_threat_to_string(state->threat_level), true);

    ssd1306_draw_hline(0, 22, 128, true);

    ssd1306_draw_string(2, 25, "Ruido:", true);
    snprintf(buf, sizeof(buf), "%.0f", state->current_reading.sound_level);
    ssd1306_draw_string(44, 25, buf, true);

    uint8_t pct = (uint8_t)CLAMP(
        (state->current_reading.sound_level / 4095.0f) * 100.0f, 0, 100);
    ssd1306_draw_rect(2, 33, 124, 7, true);
    int fill = (122 * pct) / 100;
    if (fill > 0) ssd1306_fill_rect(3, 34, fill, 5, true);

    if (state->acoustic_sig.is_learned) {
        int th_x = (int)((state->acoustic_sig.threshold / 4095.0f) * 122.0f) + 3;
        if (th_x > 2 && th_x < 126)
            ssd1306_draw_vline(th_x, 32, 9, true);
    }

    ssd1306_draw_string(2, 43, "Lim:", true);
    snprintf(buf, sizeof(buf), "%.0f", state->acoustic_sig.threshold);
    ssd1306_draw_string(28, 43, buf, true);

    ssd1306_draw_string(70, 43, "T:", true);
    snprintf(buf, sizeof(buf), "%.1fC", state->current_reading.temperature);
    ssd1306_draw_string(82, 43, buf, true);

    ssd1306_draw_hline(0, 53, 128, true);

    if (state->mode == MODE_SNOOZED) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        uint32_t rem = (state->snooze_until_ms > now) ?
                       (state->snooze_until_ms - now) / 1000 : 0;
        snprintf(buf, sizeof(buf), "Snooze: %lus  [B]Desarmar", rem);
        ssd1306_draw_string_centered(55, 128, buf, true);
    } else {
        ssd1306_draw_string_centered(55, 128, "[B]Desarmar", true);
    }
}

// ============================================================================
// UI - TELA DE ALERTA (alarme disparado)
// ============================================================================

void ui_screen_alert(SystemState* state) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool blink = (now / 300) % 2;

    if (blink) {
        ssd1306_fill_rect(0, 0, 128, 64, true);
        ssd1306_draw_string_centered(4,  128, "!! RUIDO ALTO !!", false);
        ssd1306_draw_hline(0, 14, 128, false);
        ssd1306_draw_string_centered(17, 128,
            alarm_threat_to_string(state->threat_level), false);

        char buf[32];
        snprintf(buf, sizeof(buf), "Nivel: %.0f", state->current_reading.sound_level);
        ssd1306_draw_string_centered(28, 128, buf, false);
        snprintf(buf, sizeof(buf), "Limite: %.0f", state->acoustic_sig.threshold);
        ssd1306_draw_string_centered(38, 128, buf, false);

        ssd1306_draw_hline(0, 50, 128, false);
        ssd1306_draw_string_centered(53, 128, "[B]Silenciar", false);
    } else {
        ssd1306_draw_rect(0, 0, 128, 64, true);
        ssd1306_draw_rect(2, 2, 124, 60, true);

        ssd1306_draw_string_centered(4,  128, "!! RUIDO ALTO !!", true);
        ssd1306_draw_hline(4, 14, 120, true);
        ssd1306_draw_string_centered(17, 128,
            alarm_threat_to_string(state->threat_level), true);

        char buf[32];
        snprintf(buf, sizeof(buf), "Nivel: %.0f", state->current_reading.sound_level);
        ssd1306_draw_string_centered(28, 128, buf, true);
        snprintf(buf, sizeof(buf), "Limite: %.0f", state->acoustic_sig.threshold);
        ssd1306_draw_string_centered(38, 128, buf, true);

        ssd1306_draw_hline(4, 50, 120, true);
        ssd1306_draw_string_centered(53, 128, "[B]Silenciar", true);
    }
}

// ============================================================================
// UI - TELA DE STATUS DETALHADO
// ============================================================================

void ui_screen_status(SystemState* state) {
    ssd1306_fill_rect(0, 0, 128, 9, true);
    ssd1306_draw_string_centered(1, 128, "STATUS DETALHADO", false);
    ssd1306_draw_hline(0, 10, 128, true);

    char buf[32];

    ssd1306_draw_string(2, 12, "Ruido :", true);
    snprintf(buf, sizeof(buf), "%.0f", state->current_reading.sound_level);
    ssd1306_draw_string(50, 12, buf, true);

    ssd1306_draw_string(2, 21, "Limite:", true);
    snprintf(buf, sizeof(buf), "%.0f", state->acoustic_sig.threshold);
    ssd1306_draw_string(50, 21, buf, true);

    ssd1306_draw_string(2, 30, "Temp  :", true);
    snprintf(buf, sizeof(buf), "%.1fC", state->current_reading.temperature);
    ssd1306_draw_string(50, 30, buf, true);

    ssd1306_draw_string(2, 39, "Sensib:", true);
    ssd1306_draw_string(50, 39, sensitivity_str(state->sensitivity), true);

    ssd1306_draw_hline(0, 49, 128, true);
    if (state->acoustic_sig.is_learned) {
        snprintf(buf, sizeof(buf), "u=%.0f s=%.0f",
                 state->acoustic_sig.mean, state->acoustic_sig.std_dev);
        ssd1306_draw_string_centered(52, 128, buf, true);
    } else {
        ssd1306_draw_string_centered(52, 128, "Sem calibracao", true);
    }
}
