// Headless LVGL simulator for the Claude monitor UI.
// Renders each of the 4 screens of the shared ui/ module to a PNG.
//
//   program.exe <output_dir>     (default ".")
//
// No SDL/window — pure software render into an RGB565 framebuffer, then PNG.

#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "ui.h"

static uint16_t fb[UI_W * UI_H];   // accumulated RGB565 framebuffer
static uint32_t g_tick = 0;
static uint32_t tick_cb() { return g_tick; }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  uint16_t *p = (uint16_t *)px;
  for (int32_t y = a->y1; y <= a->y2; y++)
    for (int32_t x = a->x1; x <= a->x2; x++)
      fb[y * UI_W + x] = *p++;
  lv_display_flush_ready(d);
}

static void settle(int ms) {
  lv_obj_invalidate(lv_screen_active());
  for (int i = 0; i < ms; i += 5) { g_tick += 5; lv_timer_handler(); }
}

static void dump(const char *path) {
  static uint8_t rgb[UI_W * UI_H * 3];
  for (int i = 0; i < UI_W * UI_H; i++) {
    uint16_t c = fb[i];
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    rgb[i * 3 + 0] = (uint8_t)((r * 255) / 31);
    rgb[i * 3 + 1] = (uint8_t)((g * 255) / 63);
    rgb[i * 3 + 2] = (uint8_t)((b * 255) / 31);
  }
  stbi_write_png(path, UI_W, UI_H, 3, rgb, UI_W * 3);
  printf("wrote %s\n", path);
}

static int pct_after(const char *region, const char *key) {
  if (!region) return -1;
  const char *p = strstr(region, key);
  if (!p) return -1;
  p += strlen(key);
  while (*p && (*p < '0' || *p > '9')) p++;   // skip ": and spaces
  return *p ? atoi(p) : -1;
}

// Pull live usage from the proxy (curl) if PROXY_URL is set & reachable; else false → caller mocks.
static bool fetch_live(int *fh_pct, int *wk_pct) {
  const char *url = getenv("PROXY_URL");
  const char *tok = getenv("PROXY_TOKEN");
  if (!url || !url[0]) return false;
  char cmd[600];
  if (tok && tok[0])
    snprintf(cmd, sizeof cmd, "curl -s -m 3 -H \"Authorization: Bearer %s\" \"%s\"", tok, url);
  else
    snprintf(cmd, sizeof cmd, "curl -s -m 3 \"%s\"", url);
  FILE *f = popen(cmd, "r");
  if (!f) return false;
  char buf[1024];
  size_t n = fread(buf, 1, sizeof buf - 1, f);
  buf[n] = '\0';
  pclose(f);
  if (n == 0) return false;
  int fh = pct_after(strstr(buf, "five_hour"), "used_pct");
  int wk = pct_after(strstr(buf, "weekly"), "used_pct");
  if (fh < 0) return false;
  *fh_pct = fh;
  *wk_pct = (wk < 0) ? 0 : wk;
  return true;
}

int main(int argc, char **argv) {
  const char *outdir = (argc > 1) ? argv[1] : ".";

  lv_init();
  lv_tick_set_cb(tick_cb);

  static uint8_t drawbuf[UI_W * 120 * 2];
  lv_display_t *disp = lv_display_create(UI_W, UI_H);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, drawbuf, NULL, sizeof(drawbuf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  ui_build(lv_screen_active());

  int fh = 68, wk = 41;   // mock defaults (the design samples)
  if (fetch_live(&fh, &wk)) printf("[sim] LIVE proxy data: 5h=%d%% weekly=%d%%\n", fh, wk);
  else                      printf("[sim] MOCK data (set PROXY_URL/PROXY_TOKEN, or proxy unreachable)\n");
  ui_set_session(fh, -1, "");
  ui_set_weekly(wk, -1);

  const char *names[4] = {"01_session.png", "02_weekly.png", "03_clock.png", "04_device.png"};
  for (int i = 0; i < 4; i++) {
    ui_goto(i);
    settle(450);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", outdir, names[i]);
    dump(path);
  }

  // Offline / no-data pass: validate ui_clear_usage() blanking + the Clock "CONNECTING" state.
  ui_clear_usage();
  ui_set_online(false, false);
  ui_set_clock("--:--", "CONNECTING");
  const char *off[3] = {"05_session_offline.png", "06_weekly_offline.png", "07_clock_connecting.png"};
  const int offtile[3] = {0, 1, 2};
  for (int i = 0; i < 3; i++) {
    ui_goto(offtile[i]);
    settle(450);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", outdir, off[i]);
    dump(path);
  }

  // Reset-drain pass: validate the 3s window-reset count-down on the Session ring (80% -> 0%).
  ui_set_online(true, false);
  ui_goto(0);
  ui_set_session(80, -1, "");   // pretend the 5h window was at 80%
  settle(450);
  ui_set_session(0, -1, "");    // window resets -> triggers the 80->0 drain animation
  const char *drain[4] = {"08_reset_t0.png", "09_reset_t1.png", "10_reset_t2.png", "11_reset_t3.png"};
  for (int i = 0; i < 4; i++) {
    settle(i == 0 ? 60 : 1000);   // sample ~0s, 1s, 2s, 3s into the 3s drain
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", outdir, drain[i]);
    dump(path);
  }
  printf("done\n");
  return 0;
}
