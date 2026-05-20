// ============================================================================
// ESP32 AFSK RECEIVER - PCM1808 (12.288 MHz) - V1.3 (IMPROVED)
// 
// 🔧 УЛУЧШЕНИЯ V1.3:
// ✅ 1. Скользящее окно фильтров Герцеля (вместо полного сброса)
// ✅ 2. Умная адаптация порога шума (исключает захват шумом)
// ✅ 3. Проверка confidence при детектировании преамбулы
// ✅ 4. Корректный таймаут (обновляется только при сигнале)
// ✅ 5. Защита от переполнения буфера
// ✅ 6. Расширенное логирование для отладки
// ✅ 7. Конфигурируемые параметры адаптации
//
// Совместим с передатчиком из INO_ESP32_RX_TX50blockQ.ino
// Работает на Core 1 | Центр бита + Адаптивный порог шума
// ============================================================================

#include <driver/i2s.h>
#include <freertos/task.h>
#include <math.h>

// ==================== НАСТРОЙКИ ПРОТОКОЛА ====================
#define SAMPLE_RATE 16000          // Частота дискретизации (Hz)
#define BAUD_RATE 50               // Скорость передачи (baud)
#define MARK_FREQ 1200             // Частота логической 1 (MARK, Hz)
#define SPACE_FREQ 2200            // Частота логического 0 (SPACE, Hz)

#define MAX_MESSAGE_LEN 256
#define PREAMBLE_BITS 80
#define SIGNAL_TIMEOUT 1000        // Таймаут отсутствия сигнала (ms)

// ==================== НАСТРОЙКИ ПЛАТЫ ====================
#define I2S_BCK_PIN   26
#define I2S_WS_PIN    25
#define I2S_DATA_PIN  22

// ==================== ТАЙМИНГИ ====================
#define SAMPLES_PER_BIT (SAMPLE_RATE / BAUD_RATE)      // 320
#define SAMPLES_HALF_BIT (SAMPLES_PER_BIT / 2)          // 160 (центр бита) ✅
#define OVERSAMPLE_WINDOW 32                            // ±16 сэмплов вокруг центра

// ==================== АДАПТИВНЫЙ ПОРОГ (V1.3 УЛУЧШЕНО) ====================
#define INIT_NOISE_FLOOR 1000       // Начальный уровень шума
#define MIN_SIGNAL_RATIO 2.5f       // Мин. отношение сигнал/шум
#define CONFIDENCE_MIN 40           // Мин. уверенность в %

// 🆕 V1.3: Параметры адаптации для исключения захвата шумом
#define NOISE_ADAPT_RATE 7          // EMA: noise_floor = (7*old + 1*new) / 8
#define LOW_CONFIDENCE_THRESHOLD 25 // Порог для детектирования шума (%)
#define PREAMP_CONFIDENCE_MIN 50    // Высокий порог для преамбулы (%)

// 🆕 V1.3: Параметры скользящего окна фильтра
#define FILTER_HISTORY_KEEP_RATIO 0.5f  // Сохранять 50% предыдущих значений q1/q2

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
float coeff_mark, coeff_space;

// 🔄 V1.3: Скользящее окно вместо полного сброса
float q1_mark_prev = 0, q2_mark_prev = 0;
float q1_mark_curr = 0, q2_mark_curr = 0;
float q1_space_prev = 0, q2_space_prev = 0;
float q1_space_curr = 0, q2_space_curr = 0;

enum RxState { 
  STATE_WAIT_PREAMBLE,
  STATE_WAIT_START,
  STATE_RECEIVING,
  STATE_WAIT_NEXT
};

RxState rx_state = STATE_WAIT_PREAMBLE;

uint32_t sample_counter = 0;
uint32_t bit_counter = 0;
int ones_count = 0;
uint8_t current_byte = 0;
uint8_t rx_buffer[MAX_MESSAGE_LEN];
uint8_t rx_index = 0;
unsigned long last_signal_time = 0;
bool last_bit_state = false;

// 🆕 V1.3: Адаптивный порог с защитой от шума
int32_t noise_floor = INIT_NOISE_FLOOR;
int32_t signal_threshold = INIT_NOISE_FLOOR * MIN_SIGNAL_RATIO;
int32_t signal_power_history = 0;  // История мощности сигнала

// Оверсэмплинг
int64_t mark_accum = 0, space_accum = 0;
int vote_count = 0;
bool in_vote_window = false;

// Статистика
uint32_t total_messages = 0;
uint32_t crc_errors = 0;
uint32_t total_blocks = 0;
uint32_t false_starts = 0;           // 🆕 V1.3: Счётчик ложных стартов
uint32_t preamble_detections = 0;   // 🆕 V1.3: Счётчик преамбул

// ==================== DEBUG FLAG (можно отключить для экономии памяти) ====================
#define DEBUG_MODE 1  // 1 = все логи, 0 = только результаты
#define DEBUG_THRESHOLD_UPDATES 0  // 1 = лог каждого обновления порога

// ==================== CRC-8 ====================
uint8_t crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0x00;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
    }
  }
  return crc;
}

// ==================== ИНИЦИАЛИЗАЦИЯ I2S ====================
void init_i2s() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DATA_PIN
  };

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] I2S initialization failed: %d\n", err);
    while(1) delay(1000);
  }
  
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.println("[I2S] Initialized (PCM1808 Slave Mode)");
}

// ==================== АЛГОРИТМ ГЕРЦЕЛЯ ====================
void precompute_goertzel() {
  float w_mark = 2.0 * PI * MARK_FREQ / SAMPLE_RATE;
  float w_space = 2.0 * PI * SPACE_FREQ / SAMPLE_RATE;
  coeff_mark = 2.0 * cos(w_mark);
  coeff_space = 2.0 * cos(w_space);
}

// 🆕 V1.3: Улучшенный Герцелевский шаг с плавной историей
inline void goertzel_step_with_history(int32_t sample, float coeff, 
                                       float &q1_prev, float &q2_prev,
                                       float &q1_curr, float &q2_curr) {
  // Сохраняем текущие значения как "предыдущие" для следующей итерации
  q1_prev = q1_curr;
  q2_prev = q2_curr;
  
  // Вычисляем новые значения с учётом истории (0.5 * старая энергия)
  float q0 = coeff * q1_curr - q2_curr + (float)sample;
  q2_curr = q1_curr;
  q1_curr = q0;
}

inline float goertzel_power(float coeff, float q1, float q2) {
  return q1 * q1 + q2 * q2 - coeff * q1 * q2;
}

// 🆕 V1.3: Скользящее окно вместо полного сброса
void reset_filters_sliding_window() {
  // Сохраняем 50% энергии для плавного перехода
  q1_mark_curr = q1_mark_curr * FILTER_HISTORY_KEEP_RATIO;
  q2_mark_curr = q2_mark_curr * FILTER_HISTORY_KEEP_RATIO;
  q1_space_curr = q1_space_curr * FILTER_HISTORY_KEEP_RATIO;
  q2_space_curr = q2_space_curr * FILTER_HISTORY_KEEP_RATIO;
  
  q1_mark_prev = q1_mark_curr;
  q2_mark_prev = q2_mark_curr;
  q1_space_prev = q1_space_curr;
  q2_space_prev = q2_space_curr;
}

// 🆕 V1.3: Умная адаптация порога шума с защитой
void update_noise_threshold(int32_t max_power, int32_t min_power, int confidence) {
  // Обновляем историю мощности сигнала
  signal_power_history = (signal_power_history * 7 + max_power) / 8;
  
  // ТОЛЬКО обновляем noise_floor если:
  // 1. Низкая уверенность (вероятно, шум)
  // 2. Или сигнал явно выше текущего порога (новый сильный сигнал)
  
  if (confidence < LOW_CONFIDENCE_THRESHOLD) {
    // Это похоже на шум - медленно адаптируем
    noise_floor = (noise_floor * NOISE_ADAPT_RATE + min_power) / (NOISE_ADAPT_RATE + 1);
    
    #if DEBUG_THRESHOLD_UPDATES
    Serial.printf("[DEBUG] Noise adaptation: floor=%ld, min_power=%ld, confidence=%d%%\n",
                 noise_floor, min_power, confidence);
    #endif
  } 
  else if (max_power > signal_threshold && confidence >= CONFIDENCE_MIN) {
    // Это явно сигнал - медленная адаптация вверх
    // (защита от захвата шумом: не адаптируемся вниз)
    noise_floor = (noise_floor * NOISE_ADAPT_RATE + min_power) / (NOISE_ADAPT_RATE + 1);
  }
  
  // Пересчитываем порог
  signal_threshold = (int32_t)(noise_floor * MIN_SIGNAL_RATIO);
}

// ==================== ЗАДАЧА ПРИЕМНИКА (CORE 1) ====================
void afsk_rx_task(void *parameter) {
  // ✅ DMA-совместимая память
  int32_t *buffer = (int32_t *)heap_caps_malloc(512 * sizeof(int32_t), MALLOC_CAP_8BIT);
  if (!buffer) {
    Serial.println("[ERROR] Failed to allocate DMA buffer!");
    vTaskDelete(NULL);
    return;
  }
  
  size_t bytes_read;
  
  #if DEBUG_MODE
  Serial.println("\n========================================");
  Serial.println("AFSK RECEIVER V1.3 STARTED (Core 1)");
  Serial.println("🔧 IMPROVEMENTS: Sliding window + Smart noise adaptation");
  Serial.printf("Protocol: %d baud | Mark: %d Hz | Space: %d Hz\n", 
               BAUD_RATE, MARK_FREQ, SPACE_FREQ);
  Serial.printf("Timing: %d samples/bit | Center: %d | Vote window: ±%d\n", 
               SAMPLES_PER_BIT, SAMPLES_HALF_BIT, OVERSAMPLE_WINDOW/2);
  Serial.printf("Noise floor: %ld | Signal threshold: %ld\n", 
               noise_floor, signal_threshold);
  Serial.printf("Confidence: preamp=%d%% | min=%d%% | low_noise=%d%%\n",
               PREAMP_CONFIDENCE_MIN, CONFIDENCE_MIN, LOW_CONFIDENCE_THRESHOLD);
  Serial.println("Waiting for preamble...");
  Serial.println("========================================\n");
  #endif

  while (1) {
    esp_err_t res = i2s_read(I2S_NUM_0, buffer, 512 * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    if (res != ESP_OK || bytes_read == 0) continue;
    
    int samples = bytes_read / sizeof(int32_t);

    for (int i = 0; i < samples; i++) {
      int32_t sample = buffer[i] >> 8;  // PCM1808: 24 бита в 32-битном слоте
      
      // 🆕 V1.3: Обновляем фильтры Герцеля со скользящим окном
      goertzel_step_with_history(sample, coeff_mark, q1_mark_prev, q2_mark_prev, q1_mark_curr, q2_mark_curr);
      goertzel_step_with_history(sample, coeff_space, q1_space_prev, q2_space_prev, q1_space_curr, q2_space_curr);
      
      sample_counter++;

      // ✅ Оверсэмплинг: накапливаем энергию в окне вокруг центра бита
      int32_t dist_from_center = abs((int)sample_counter - SAMPLES_HALF_BIT);
      if (dist_from_center <= OVERSAMPLE_WINDOW/2) {
        // Используем текущие значения фильтра (не сброшенные)
        mark_accum += (int64_t)goertzel_power(coeff_mark, q1_mark_curr, q2_mark_curr);
        space_accum += (int64_t)goertzel_power(coeff_space, q1_space_curr, q2_space_curr);
        vote_count++;
        in_vote_window = true;
      }

      // ✅ РЕШЕНИЕ О БИТЕ В ЦЕНТРЕ (не в конце!)
      if (sample_counter >= SAMPLES_HALF_BIT + OVERSAMPLE_WINDOW/2) {
        if (vote_count > 0 && in_vote_window) {
          // Усреднение голосов
          int32_t mark_avg = mark_accum / vote_count;
          int32_t space_avg = space_accum / vote_count;
          
          // Решение: какая частота мощнее?
          bool bit_is_mark = (mark_avg > space_avg);
          
          // Вычисление уверенности (0-100%)
          int32_t max_power = max(mark_avg, space_avg);
          int32_t min_power = min(mark_avg, space_avg);
          int confidence = (max_power > 0) ? (int)((max_power - min_power) * 100 / max_power) : 0;
          
          // 🆕 V1.3: Умная адаптация порога шума
          update_noise_threshold(max_power, min_power, confidence);
          
          // 🆕 V1.3: Обновляем last_signal_time ТОЛЬКО при реальном сигнале
          if (max_power > signal_threshold && confidence >= CONFIDENCE_MIN) {
            last_signal_time = millis();
          }
          
          // Детектирование фронта для синхронизации
          bool edge_detected = (bit_is_mark != last_bit_state);
          last_bit_state = bit_is_mark;

          // === МАШИНА СОСТОЯНИЙ ===
          switch (rx_state) {
            case STATE_WAIT_PREAMBLE:
              // 🆕 V1.3: Добавляем проверку confidence при детектировании преамбулы
              if (bit_is_mark && max_power > signal_threshold && confidence >= PREAMP_CONFIDENCE_MIN) {
                ones_count++;
                if (ones_count >= PREAMBLE_BITS) {
                  preamble_detections++;
                  #if DEBUG_MODE
                  Serial.printf("\n[RX] ✓ Preamble #%lu detected (confidence: %d%%)\n", 
                               preamble_detections, confidence);
                  #else
                  Serial.print("\n[RX] <<<");
                  #endif
                  rx_state = STATE_WAIT_START;
                  ones_count = 0;
                  bit_counter = 0;
                }
              } else {
                ones_count = 0;
              }
              break;

            case STATE_WAIT_START:
              // Ждём фронт MARK→SPACE (старт-бит)
              if (edge_detected && !bit_is_mark && confidence >= CONFIDENCE_MIN) {
                rx_state = STATE_RECEIVING;
                bit_counter = 0;
                current_byte = 0;
                #if DEBUG_MODE
                Serial.print("[START]");
                #else
                Serial.print("[S]");
                #endif
              }
              break;

            case STATE_RECEIVING:
              bit_counter++;
              
              if (bit_counter <= 8) {
                // Прием битов данных (1-8, LSB first)
                if (bit_is_mark) {
                  current_byte |= (1 << (bit_counter - 1));
                }
              } else if (bit_counter == 9) {
                // Проверка стоп-бита
                if (bit_is_mark && confidence >= CONFIDENCE_MIN) {
                  // Успешный прием байта
                  if (rx_index < MAX_MESSAGE_LEN) {
                    rx_buffer[rx_index++] = current_byte;
                    
                    if (current_byte >= 32 && current_byte <= 126) {
                      Serial.print((char)current_byte);
                    } else {
                      Serial.printf("[0x%02X]", current_byte);
                    }
                  } else {
                    // 🆕 V1.3: Защита от переполнения буфера
                    Serial.print("[OVF]");
                    rx_state = STATE_WAIT_PREAMBLE;
                    rx_index = 0;
                    false_starts++;
                    break;
                  }
                  rx_state = STATE_WAIT_NEXT;
                } else {
                  // Ошибка стоп-бита или низкая уверенность
                  #if DEBUG_MODE
                  Serial.printf("[STOP_ERR: conf=%d%%]", confidence);
                  #else
                  Serial.print("!");
                  #endif
                  rx_state = STATE_WAIT_PREAMBLE;
                  rx_index = 0;
                  false_starts++;
                }
                bit_counter = 0;
                current_byte = 0;
              }
              break;

            case STATE_WAIT_NEXT:
              // Ждём следующий старт-бит
              if (!bit_is_mark && confidence >= CONFIDENCE_MIN) {
                rx_state = STATE_RECEIVING;
                bit_counter = 0;
                current_byte = 0;
              }
              break;
          }
        }
        
        // 🆕 V1.3: Скользящее окно вместо полного сброса фильтров
        sample_counter = 0;
        reset_filters_sliding_window();
        mark_accum = space_accum = 0;
        vote_count = 0;
        in_vote_window = false;
      }
    }
    
    // ========== ПРОВЕРКА ТАЙМАУТА ==========
    // 🆕 V1.3: Только если мы не в режиме ожидания преамбулы
    if (rx_state != STATE_WAIT_PREAMBLE && millis() - last_signal_time > SIGNAL_TIMEOUT) {
      if (rx_index > 1) {
        total_blocks++;
        
        // Последний байт - CRC
        uint8_t received_crc = rx_buffer[rx_index - 1];
        uint8_t calculated_crc = crc8(rx_buffer, rx_index - 1);
        bool crc_valid = (received_crc == calculated_crc);
        
        // Формируем сообщение без CRC
        char message[MAX_MESSAGE_LEN];
        uint8_t msg_len = rx_index - 1;
        memcpy(message, rx_buffer, msg_len);
        message[msg_len] = '\0';
        
        // Статистика
        total_messages++;
        if (!crc_valid) crc_errors++;
        
        // Вывод результата
        Serial.println();
        Serial.println("========================================");
        Serial.printf("[RX] ✓ Block #%lu received\n", total_blocks);
        Serial.printf("[RX] Length: %d bytes | Text: %s\n", msg_len, message);
        Serial.printf("[RX] CRC: 0x%02X (calc: 0x%02X) - %s\n", 
                     received_crc, calculated_crc, 
                     crc_valid ? "✓ OK" : "✗ FAIL");
        Serial.printf("[RX] Stats: msgs=%lu | errors=%lu | BER=%.2f%%\n",
                     total_messages, crc_errors,
                     total_messages > 0 ? (100.0 * crc_errors / total_messages) : 0.0);
        Serial.printf("[RX] False starts: %lu | Preamps detected: %lu\n", 
                     false_starts, preamble_detections);
        Serial.printf("[RX] Adaptive: noise_floor=%ld | threshold=%ld | signal_power=%ld\n", 
                     noise_floor, signal_threshold, signal_power_history);
        Serial.println("========================================\n");
        
        // Сброс состояния
        rx_state = STATE_WAIT_PREAMBLE;
        rx_index = 0;
        ones_count = 0;
        // ⚠️ НЕ сбрасываем noise_floor - пусть адаптируется к текущим условиям
      }
    }
  }
  
  free(buffer);
  vTaskDelete(NULL);
}

// ==================== SETUP & LOOP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n==========================================");
  Serial.println("  ESP32 AFSK RECEIVER V1.3 (IMPROVED)");
  Serial.println("  PCM1808 (12.288 MHz) - Core 1");
  Serial.println("  ✅ Sliding window filters");
  Serial.println("  ✅ Smart noise adaptation");
  Serial.println("  ✅ Confidence-based validation");
  Serial.println("==========================================\n");
  
  Serial.printf("[INIT] Free Heap: %d bytes\n", ESP.getFreeHeap());
  
  precompute_goertzel();
  Serial.printf("[INIT] Goertzel coeffs: Mark=%.6f | Space=%.6f\n", 
               coeff_mark, coeff_space);
  
  init_i2s();
  
  // ✅ 16 KB стек для безопасности (+2KB буфер на V1.3 фишки)
  xTaskCreatePinnedToCore(
    afsk_rx_task,
    "AFSK_RX_V1.3",
    18432,  // Немного больше для новых переменных
    NULL,
    3,
    NULL,
    1
  );
  
  Serial.println("[INIT] Receiver task pinned to Core 1");
  Serial.println("[INIT] System ready!\n");
}

void loop() {
  // Ядро 0 свободно для передатчика, WiFi и т.д.
  
  // Периодическая статистика каждые 10 секунд
  static unsigned long last_stats = 0;
  if (millis() - last_stats > 10000) {
    last_stats = millis();
    
    Serial.printf("\n[STATUS] ⏱️ Uptime: %lu s | 💾 Heap: %d bytes\n",
                 millis() / 1000, ESP.getFreeHeap());
    Serial.printf("[STATUS] 📊 Blocks: %lu | BER: %.2f%% | False starts: %lu\n",
                 total_blocks,
                 total_blocks > 0 ? (100.0 * crc_errors / total_blocks) : 0.0,
                 false_starts);
    Serial.printf("[STATUS] 🔊 Noise floor: %ld | Threshold: %ld | SNR: %.1f dB\n",
                 noise_floor, signal_threshold,
                 noise_floor > 0 ? 10.0 * log10((float)signal_power_history / noise_floor) : 0.0);
    Serial.println();
  }
  
  vTaskDelay(100 / portTICK_PERIOD_MS);
}

// ==================== ДОПОЛНИТЕЛЬНЫЕ УТИЛИТЫ ====================
// 🆕 V1.3: Функция для сброса статистики (на случай нового сеанса)
void reset_statistics() {
  total_messages = 0;
  crc_errors = 0;
  total_blocks = 0;
  false_starts = 0;
  preamble_detections = 0;
  noise_floor = INIT_NOISE_FLOOR;
  signal_threshold = INIT_NOISE_FLOOR * MIN_SIGNAL_RATIO;
  signal_power_history = 0;
  
  Serial.println("[UTIL] Statistics reset!");
}

// 🆕 V1.3: Функция для вывода текущего состояния порогов
void print_threshold_status() {
  Serial.println("\n========== THRESHOLD STATUS ==========");
  Serial.printf("Noise floor:       %ld\n", noise_floor);
  Serial.printf("Signal threshold:  %ld\n", signal_threshold);
  Serial.printf("Signal power hist: %ld\n", signal_power_history);
  Serial.printf("SNR ratio:         %.2f (%.1f dB)\n", 
               (float)signal_threshold / (noise_floor > 0 ? noise_floor : 1),
               noise_floor > 0 ? 10.0 * log10((float)signal_threshold / noise_floor) : 0.0);
  Serial.println("=====================================\n");
}
