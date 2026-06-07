#pragma once
#include <Arduino.h>

// Audio notifications (F5) — ES8311 codec over I2S, soft notification chimes.
//
// Tones are synthesised in RAM with an attack/decay envelope (no clicks) at low
// digital amplitude, and played on a dedicated FreeRTOS task via a queue so the
// LVGL loop never blocks. The codec is muted while idle to kill amplifier hiss.
//
// NOTE: assumes Wire.begin(SDA=8, SCL=7) has already been called (main.cpp does
// this for the shared I2C bus). audio_begin() does NOT touch Wire config.

void audio_begin();        // init I2S + ES8311 at low volume; start the player task
void audio_chime_warn();   // soft 2-note rising chime — usage crossed >=70%
void audio_chime_reset();  // gentle single low note — 100% / window reset
void audio_chime_alert();  // bright, LOUDER rising triad — Claude Code "needs input" (issue #2)
void audio_chime_bot();    // playful robotic "beep-bop-bop-beep" — shake-to-summon easter egg (#31)
bool audio_ready();        // true once the codec + task initialised ok
