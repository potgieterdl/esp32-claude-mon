#include "app_audio.h"

#include "ESP_I2S.h"
#include "Wire.h"
#include "es8311.h"

#include <math.h>

// ---- Pins / codec config (from docs/research-notes.md + vendored examples) ----
#define I2S_MCK_PIN  19
#define I2S_BCK_PIN  20
#define I2S_LRCK_PIN 22
#define I2S_DOUT_PIN 23
#define I2S_DIN_PIN  21

#define SAMPLE_RATE   16000                 // 16k -> MCLK 4.096MHz is in es8311 coeff table
#define MCLK_MULTIPLE 256
#define MCLK_FREQ_HZ  (SAMPLE_RATE * MCLK_MULTIPLE)

#define VOICE_VOLUME  35                    // codec output volume (0..100) — "soft"
#define TONE_AMPLITUDE 7000                 // digital peak (of 32767) — keep low for soft

// ---- Module state ----
static I2SClass       s_i2s;
static es8311_handle_t s_es = nullptr;
static QueueHandle_t  s_queue = nullptr;
static volatile bool  s_ready = false;

enum chime_id_t { CHIME_WARN = 1, CHIME_RESET = 2 };

// Largest note we ever synthesise (ms) -> size the scratch buffer for it.
#define MAX_NOTE_MS   320
#define MAX_NOTE_SAMP ((SAMPLE_RATE * MAX_NOTE_MS) / 1000)

static int16_t s_tone[MAX_NOTE_SAMP];       // ~10KB scratch, reused per note

// Synthesise one sine note into s_tone with a short attack + decay ramp so the
// waveform starts/ends at zero amplitude (avoids the click of a hard edge).
// Returns the number of samples written.
static size_t synth_note(float freq, uint16_t ms) {
  size_t n = (size_t)SAMPLE_RATE * ms / 1000;
  if (n > MAX_NOTE_SAMP) n = MAX_NOTE_SAMP;

  const size_t ramp = SAMPLE_RATE * 8 / 1000;   // 8ms attack/decay
  const float w = 2.0f * (float)M_PI * freq / SAMPLE_RATE;

  for (size_t i = 0; i < n; i++) {
    float env = 1.0f;
    if (i < ramp)            env = (float)i / ramp;             // attack
    else if (i > n - ramp)   env = (float)(n - i) / ramp;      // decay
    s_tone[i] = (int16_t)(sinf(w * i) * TONE_AMPLITUDE * env);
  }
  return n;
}

static void play_note(float freq, uint16_t ms) {
  size_t n = synth_note(freq, ms);
  s_i2s.write((uint8_t *)s_tone, n * sizeof(int16_t));   // blocking — but on our task
}

// brief silence between notes (also flushed through I2S so timing is exact)
static void play_silence(uint16_t ms) {
  size_t n = (size_t)SAMPLE_RATE * ms / 1000;
  if (n > MAX_NOTE_SAMP) n = MAX_NOTE_SAMP;
  memset(s_tone, 0, n * sizeof(int16_t));
  s_i2s.write((uint8_t *)s_tone, n * sizeof(int16_t));
}

static void play_chime(chime_id_t id) {
  // Unmute only while playing, then mute again to silence amplifier hiss.
  es8311_voice_mute(s_es, false);

  if (id == CHIME_WARN) {
    // soft 2-note rising chime (E5 -> A5)
    play_note(659.25f, 130);
    play_silence(40);
    play_note(880.00f, 170);
  } else { // CHIME_RESET
    // gentle single low note (E4)
    play_note(329.63f, 300);
  }

  play_silence(20);          // let the tail flush before muting
  es8311_voice_mute(s_es, true);
}

static void audio_task(void *arg) {
  uint8_t id;
  for (;;) {
    if (xQueueReceive(s_queue, &id, portMAX_DELAY) == pdTRUE) {
      play_chime((chime_id_t)id);
    }
  }
}

static esp_err_t codec_init() {
  s_es = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  if (!s_es) return ESP_FAIL;

  const es8311_clock_config_t clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = MCLK_FREQ_HZ,
    .sample_frequency = SAMPLE_RATE,
  };
  esp_err_t err = es8311_init(s_es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
  if (err != ESP_OK) return err;

  es8311_voice_volume_set(s_es, VOICE_VOLUME, NULL);
  es8311_microphone_config(s_es, false);
  es8311_voice_mute(s_es, true);     // start muted (idle) — no hiss
  return ESP_OK;
}

void audio_begin() {
  // I2S bus (shared-mode standard, mono, 16-bit). Wire/I2C already up in main.cpp.
  s_i2s.setPins(I2S_BCK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCK_PIN);
  if (!s_i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("[audio] I2S init FAILED");
    return;
  }

  if (codec_init() != ESP_OK) {
    Serial.println("[audio] ES8311 init FAILED");
    return;
  }

  s_queue = xQueueCreate(4, sizeof(uint8_t));
  if (!s_queue) { Serial.println("[audio] queue alloc FAILED"); return; }

  // Dedicated player task (low priority — must never starve LVGL). 4KB stack.
  if (xTaskCreate(audio_task, "audio", 4096, nullptr, 2, nullptr) != pdPASS) {
    Serial.println("[audio] task create FAILED");
    return;
  }

  s_ready = true;
  Serial.println("[audio] ready (ES8311 @ vol 35, muted/idle)");
}

static void enqueue(chime_id_t id) {
  if (!s_ready) return;
  uint8_t v = (uint8_t)id;
  xQueueSend(s_queue, &v, 0);   // non-blocking: drop if queue full
}

void audio_chime_warn()  { enqueue(CHIME_WARN); }
void audio_chime_reset() { enqueue(CHIME_RESET); }
bool audio_ready()       { return s_ready; }
