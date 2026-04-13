# BitDogLab — Monitor de Ruído de Máquina

![Platform](https://img.shields.io/badge/platform-BitDogLab%20V7%20%7C%20RP2040-blue)
![Language](https://img.shields.io/badge/language-C-lightgrey)
![SDK](https://img.shields.io/badge/Pico%20SDK-2.2.0-green)
![Architecture](https://img.shields.io/badge/architecture-Dual--Core-teal)
![License](https://img.shields.io/badge/license-MIT-orange)

Sistema embarcado de monitoramento preditivo para a placa **BitDogLab V7 (RP2040)**, que detecta anomalias acústicas em motores elétricos, bombas e máquinas rotativas, acionando alarme quando o nível de ruído excede o limiar aprendido — indicando possível falha iminente (rolamento desgastado, desalinhamento, cavitação, etc.).

---

## Objetivo

Monitorar máquinas industriais em operação contínua, detectando **aumento anormal de ruído** que precede falhas mecânicas. O sistema:

- **Aprende a assinatura acústica normal** da máquina (5 segundos de amostragem)
- **Calcula threshold adaptativo** com estatísticas robustas (μ + N·σ, com remoção de outliers de 10%)
- **Dispara alarme** em dois níveis: `PROBABLE` (falha incipiente) e `CONFIRMED` (falha severa)
- **Processa em dual-core:** Core 0 para lógica de segurança e detecção, Core 1 para interface gráfica

> **Diferencial:** utiliza apenas o **microfone analógico onboard** da BitDogLab V7 — nenhum sensor externo necessário.

---

## Hardware utilizado

Todos os componentes são integrados à **BitDogLab V7** — nenhum hardware externo adicional é necessário.

| Componente | Pino | Função |
|---|---|---|
| Microfone analógico | `GPIO28 (ADC2)` | Captura ruído da máquina |
| Sensor de temperatura interno | `ADC4` (interno) | Compensação térmica (planejado) |
| Display OLED SSD1306 | `I2C1: SDA=GP14, SCL=GP15` | Interface visual |
| Buzzer A / B | `GPIO10 / GPIO11 (PWM)` | Alerta sonoro estéreo |
| LED RGB | `GPIO13 (R), GPIO17 (G), GPIO16 (B)` | Indicação visual de estado |
| Botão A | `GPIO5` | Calibrar e armar |
| Botão B | `GPIO6` | Desarme de emergência / combinação com joystick |
| Joystick SW | `GPIO22` | Log UART sob demanda |
| Joystick X/Y | `GPIO26 (ADC0) / GPIO29 (ADC3)` | Navegação da interface |

---

## Dependências

### SDK e toolchain

| Dependência | Versão mínima | Instalação |
|---|---|---|
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | 2.2.0 | `git clone` + submodules |
| CMake | 3.13 | `sudo apt install cmake` |
| arm-none-eabi-gcc | 10.x | `sudo apt install gcc-arm-none-eabi` |
| Python 3 | 3.8+ | `sudo apt install python3` |

### Bibliotecas do Pico SDK usadas

```
pico_stdlib  pico_multicore  pico_sync
hardware_adc  hardware_gpio  hardware_i2c
hardware_pwm  hardware_timer  hardware_dma
pico_float
```

Todas inclusas no Pico SDK — sem dependências externas adicionais.

---

## Instalação

### 1. Clonar o Pico SDK

```bash
git clone https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
cd ~/pico-sdk && git submodule update --init
export PICO_SDK_PATH=~/pico-sdk
```

### 2. Clonar este repositório

```bash
git clone https://github.com/seu-usuario/bitdoglab-noise-monitor.git
cd bitdoglab-noise-monitor
```

### 3. Copiar o import do SDK

```bash
cp ~/pico-sdk/external/pico_sdk_import.cmake .
```

### 4. Estrutura de arquivos

```
bitdoglab-noise-monitor/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── include/
│   ├── config.h          # Configurações de hardware e parâmetros
│   ├── alarm.h           # Detecção de anomalia acústica
│   ├── sensors.h         # Leitura de microfone e temperatura
│   └── display.h         # Interface com SSD1306
└── src/
    ├── main.c            # Inicialização e entrada principal
    ├── core0_security.c  # Lógica de segurança (Core 0)
    ├── core1_interface.c # Interface e display (Core 1)
    ├── alarm.c           # Calibração e detecção de anomalias
    ├── sensors.c         # Leitura de sensores (mic + temp)
    └── display.c         # Drivers e UI do SSD1306
```

### 5. Compilação

```bash
mkdir build && cd build
cmake .. -DPICO_BOARD=pico_w
make -j4
```

O arquivo gerado será `build/proj.uf2`.

---

## Gravação na placa

1. Segure o botão **BOOTSEL** da BitDogLab
2. Conecte o cabo USB ao computador
3. Solte o botão BOOTSEL — a placa monta como disco `RPI-RP2`
4. Copie o firmware:

```bash
cp build/proj.uf2 /media/$USER/RPI-RP2/
```

A placa reinicia automaticamente e o sistema inicia.

### Monitor serial

```bash
# Linux
screen /dev/ttyACM0 115200
# ou
minicom -b 115200 -o -D /dev/ttyACM0
```

---

## Uso do sistema

### Fluxo de operação

```
1. Calibração (aprender ruído normal da máquina)
   └── [A] → sistema grava 5 s do ambiente
   └── Calcula mean, std_dev, threshold = mean + N·σ

2. Armamento
   └── Após calibração, sistema entra em modo ARMADO

3. Monitoramento contínuo
   └── Se ruído > threshold → ameaça detectada
   └── Nível PROBABLE: excesso < 50% do threshold
   └── Nível CONFIRMED: excesso ≥ 50% do threshold

4. Alarme
   └── LED vermelho + buzzer alternado
   └── Display mostra alerta piscante
   └── [B] silencia/desarma
```

### Botões e controles

| Botão / Controle | Modo DESARMADO | Modo ARMADO | Modo ALARME |
|---|---|---|---|
| `[A]` | Calibra e arma | Sem efeito | Sem efeito |
| `[B]` | Desarme emergência | Desarme emergência | Silencia/Desarma |
| `[SW]` (joystick) | Log UART | Log UART | Log UART |
| Joystick + `[B]` | Alterna tela / Sensibilidade | Sem efeito | Sem efeito |

### Níveis de ameaça e indicações

| Nível | Condição | LED RGB | Buzzer |
|---|---|---|---|
| `NONE` | Ruído ≤ threshold | Verde | Silêncio |
| `PROBABLE` | Ruído > threshold (excesso < 50%) | Vermelho | Beep lento (0,5 Hz) |
| `CONFIRMED` | Ruído > threshold (excesso ≥ 50%) | Vermelho piscante (5 Hz) | Sirene alternada A/B (5 Hz) |

### Telas do display

| Tela | Conteúdo |
|---|---|
| Main | Nível de ruído (barra), temperatura, sensibilidade |
| Status | Ruído atual, threshold, temperatura, média/desvio da calibração |
| Calibração | Barra de progresso (0–100%) |
| Alarme | Texto piscante "!! RUIDO ALTO !!" + nível da ameaça |

---

## Configuração

Edite `include/config.h` para ajustar parâmetros:

```c
// Aprendizado acústico
#define LEARNING_DURATION_MS    5000   // Duração da calibração (ms)
#define LEARNING_SAMPLE_RATE    100    // Amostras por segundo
#define OUTLIER_PERCENTILE      0.1f   // Remove 10% outliers (cada lado)
#define THRESHOLD_STDDEV_MULT   3.0f   // Threshold padrão: μ + 3σ

// Sensibilidade (multiplicador σ)
#define SENSITIVITY_HIGH     2.0f     // Mais sensível (detecta falha cedo)
#define SENSITIVITY_MEDIUM   3.0f     // Padrão
#define SENSITIVITY_LOW      4.0f     // Menos sensível (menos falsos positivos)

// Temporizações
#define THREAT_DEBOUNCE_MS      2000   // Debounce após detecção
#define SNOOZE_DURATION_MS      300000 // Snooze de 5 minutos
#define SENSOR_READ_INTERVAL_MS 100    // 10 Hz
#define THREAT_CHECK_INTERVAL_MS 500   // 2 Hz
```

---

## Arquitetura do software

### Dual-core

| Core | Responsabilidade | Mutex usado |
|---|---|---|
| Core 0 | Leitura de sensores, detecção de ameaças, gerenciamento de estado | `state_mutex`, `adc_mutex` |
| Core 1 | Interface com usuário (display, joystick, botões, buzzer) | `state_mutex`, `uart_mutex` |

### Principais módulos

| Módulo | Arquivo | Função |
|---|---|---|
| `AcousticSignature` | `alarm.c` | Estrutura com mean, std_dev, threshold, samples |
| `alarm_learn_environment()` | `alarm.c` | Amostragem + estatística robusta (remove outliers) |
| `alarm_detect()` | `alarm.c` | Compara ruído atual com threshold |
| `sensors_read_all()` | `sensors.c` | Leitura simultânea de mic + temperatura |
| `display_draw_screen()` | `display.c` | Renderiza tela baseada no estado atual |

### Estatística robusta (remoção de outliers)

```c
// Remove 10% menores e 10% maiores amostras
size_t start = count * OUTLIER_PERCENTILE;           // 10%
size_t end   = count * (1.0f - OUTLIER_PERCENTILE);  // 90%
mean      = média(samples[start:end])
std_dev   = desvio_padrão(samples[start:end])
threshold = mean + N * std_dev
```

Evita que ruídos transientes da máquina (batidas, partidas) contaminem a calibração.

---

## Créditos e ferramentas utilizadas

| Ferramenta / Biblioteca | Uso | Licença |
|---|---|---|
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | SDK principal, drivers de hardware | BSD 3-Clause |
| CMake | Sistema de build | BSD |
| arm-none-eabi-gcc | Compilador C para ARM Cortex-M0+ | GPL |
| Claude (Anthropic) | Assistente de IA para geração de código e documentação | — |
| BitDogLab | Plataforma de hardware (UNICAMP Escola 4.0) | — |

### Especificações de hardware referenciadas

- *RP2040 Datasheet* — Raspberry Pi Foundation, 2021
- *SSD1306 OLED Datasheet* — Solomon Systech, 2008
- *Electret Microphone + Amplifier* — BitDogLab V7 schematics

---

**Autor:** Klysmann G. F. Barbosa
**Curso:** EmbarcaTech — Sistemas Embarcados  
**Data:** Abril de 2026  
**Plataforma:** BitDogLab V7 — Raspberry Pi RP2040  
**Propósito:** Monitoramento preditivo de falhas em máquinas rotativas
**Github:** https://github.com/kgfbarbosa/EMB-embarcatech
