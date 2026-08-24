# afsk_tx_rx — AFSK-трансивер на одном ESP32

Приём и передача текстовых сообщений по AFSK (MARK 1200 Гц / SPACE 2200 Гц) на
одном ESP32: **TX закреплён на ядре 0, RX на ядре 1**. Передатчик — DDS AD9851,
приёмник — I²S-АЦП PCM1808 (24 бит, 48 кГц). Сообщение режется на UTF-8-безопасные
блоки, каждый блок кадрируется (START / 8 бит LSB-first / STOP), защищается CRC-8
(poly 0x07, init 0x00) и на приёме собирается обратно в полный текст.

Демодулятор: DC-блокиратор → квадратурные корреляторы на оба тона → согласованный
фильтр (интегрирование I/Q за бит) → цифровой ФАПЧ восстановления битовой синхры.
Порог «есть сигнал» защищает от ложной преамбулы на пустой линии; в radio-сборках
он ещё и калибруется под конкретную рацию (см. `AFSK_AUTO_SQUELCH`).

## Варианты сборок

Различаются только тремя дефайнами. `AFSK_VERBOSE` и `AFSK_AUTO_SQUELCH` — в
`main/afsk_common.h`, `TX_PTT_ENABLE` — в `main/tx_ad9851.h`.

| Проект | Скорость, бод | Режим лога | Канал | `AFSK_VERBOSE` | `TX_PTT_ENABLE` | `AFSK_AUTO_SQUELCH` |
|---|---|---|---|---|---|---|
| `tx_rx_mes_50b_filter_quiet`                  | 50  | quiet   | провод | 0 | 0 | — (порог фиксированный) |
| `tx_rx_mes_50b_filter_verbose`                | 50  | verbose | провод | 1 | 0 | — |
| `tx_rx_mes_100b_filter_quiet`                 | 100 | quiet   | провод | 0 | 0 | — |
| `tx_rx_mes_100b_filter_verbose`               | 100 | verbose | провод | 1 | 0 | — |
| `tx_rx_mes_300b_filter_quiet`                 | 300 | quiet   | провод | 0 | 0 | — |
| `tx_rx_mes_300b_filter_verbose`               | 300 | verbose | провод | 1 | 0 | — |
| `tx_rx_mes_50b_filter_radio_quiet_squelch`    | 50  | quiet   | рация  | 0 | 1 | 1 |
| `tx_rx_mes_50b_filter_radio_verbose_squelch`  | 50  | verbose | рация  | 1 | 1 | 1 |
| `tx_rx_mes_100b_filter_radio_quiet_squelch`   | 100 | quiet   | рация  | 0 | 1 | 1 |
| `tx_rx_mes_100b_filter_radio_verbose_squelch` | 100 | verbose | рация  | 1 | 1 | 1 |
| `tx_rx_mes_300b_filter_radio_quiet_squelch`   | 300 | quiet   | рация  | 0 | 1 | 1 |
| `tx_rx_mes_300b_filter_radio_verbose_squelch` | 300 | verbose | рация  | 1 | 1 | 1 |

- **quiet / verbose** — `AFSK_VERBOSE`: в verbose печатаются `[PREAMBLE]`,
  `[LEVEL]`, `[SQUELCH]`, `[STAT]`; в quiet — только принятые блоки, CRC и
  собранное сообщение.
- **провод / рация** — `TX_PTT_ENABLE`: в radio-сборках на каждый блок
  поднимается PTT (GPIO17) с задержкой `PTT_LEAD_MS` до преамбулы и `PTT_TAIL_MS`
  после; в проводных PTT выключен, поведение как в проверенной по кабелю версии.
- **squelch** — `AFSK_AUTO_SQUELCH`: в radio-сборках порог калибруется по шуму
  тракта (+12 дБ над идлом, зажат между −40 и −20 dBFS), в проводных остаётся
  фиксированным −40 dBFS.

Отладку с рациями удобнее вести на `..._radio_verbose_squelch` (есть `[LEVEL]` для
настройки громкости), после подбора уровней — перейти на `..._radio_quiet_squelch`.
Начинать лучше с 300 бод; если голосовой FM-тракт их не пропускает — 100 или 50 бод.

## Распиновка (ESP32)

| Сигнал | GPIO | Куда |
|---|---|---|
| AD9851 `FQ_UD`  | GPIO18 | DDS-модуль |
| AD9851 `W_CLK`  | GPIO19 | DDS-модуль |
| AD9851 `DATA`   | GPIO21 | DDS-модуль |
| AD9851 `RESET`  | GPIO16 | DDS-модуль |
| PTT (через оптопару PC817) | GPIO17 | вместо кнопки PTT рации, `PTT_ACTIVE_LEVEL 1` |
| I²S `BCK`       | GPIO26 | PCM1808 BCK |
| I²S `WS`/LRCK   | GPIO25 | PCM1808 LRCK |
| I²S `DATA`/DIN  | GPIO22 | PCM1808 DOUT |

I²S работает в режиме slave, MCLK от ESP32 не подаётся (PCM1808 тактируется
собственным источником SCKI). Значения PTT задаются в `main/tx_ad9851.h`
(`PIN_PTT`, `PTT_ACTIVE_LEVEL`, `PTT_LEAD_MS`, `PTT_TAIL_MS`).

> Аналоговая обвязка: выход AD9851 подаётся на микрофонный вход рации через
> делитель (~100:1) и разделительный конденсатор; выход динамика рации — на вход
> PCM1808 через понижающий делитель. Прямое подключение DDS на микрофонный вход
> недопустимо. Реальный эфирный тракт (PTT через PC817 и аудиоуровни) требует
> проверки на железе — синтетические host-тесты его не заменяют.

## Сборка (ESP-IDF)

```bash
cd tx_rx_mes_300b_filter_radio_verbose_squelch
idf.py set-target esp32
idf.py build flash monitor
```

## Host-тесты DSP (`hosttest/`)

Переносимая часть (сериализация TX → синтез тонов → декодер) проверяется на
хосте без ESP-IDF. Пути в примере — под 300-бодовый проект, для остальных
подставьте нужную папку.

```bash
cd afsk_tx_rx
# основной прогон: nominal, дрейф, шум, много-блочные UTF-8/emoji кейсы
gcc -O2 -std=gnu11 -I hosttest/stubs -I tx_rx_mes_300b_filter_verbose/main \
    hosttest/host_test.c \
    tx_rx_mes_300b_filter_verbose/main/afsk_protocol.c \
    tx_rx_mes_300b_filter_verbose/main/afsk_decoder.c -lm -o ht && ./ht

# калибровка порога (шум, затем посылка на его фоне) — для radio-сборок
gcc -O2 -std=gnu11 -I hosttest/stubs -I tx_rx_mes_300b_filter_radio_verbose_squelch/main \
    hosttest/squelch.c \
    tx_rx_mes_300b_filter_radio_verbose_squelch/main/afsk_protocol.c \
    tx_rx_mes_300b_filter_radio_verbose_squelch/main/afsk_decoder.c -lm -o sq && ./sq

# ложная преамбула на пустой линии / паразитный тон
gcc -O2 -std=gnu11 -I hosttest/stubs -I tx_rx_mes_300b_filter_verbose/main \
    hosttest/idle.c \
    tx_rx_mes_300b_filter_verbose/main/afsk_protocol.c \
    tx_rx_mes_300b_filter_verbose/main/afsk_decoder.c -lm -o idle && ./idle 0.02 15
```

У каждого проекта есть собственный `README.md` с подробностями DSP, порога и PTT.
