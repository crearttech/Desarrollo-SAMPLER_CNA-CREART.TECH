/**
 * ===================================================================== 
 * ===================================================================== 
 * Simple Looper para Daisy Seed con Salida de Audio Analógica (Interna)
 * Corregido: waveform dinámica (calculo al finalizar la grabación) 
 * ===================================================================== 
 */
#include <DaisyDuino.h>
#include <new>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <math.h>
#include "sampler_engine.h"
#include "sampler_hardware.h"


using namespace daisy;

#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_BLOCK_SAMPLES 48

// --- DEFINICIONES DE PANTALLA ---
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 128
GFXcanvas16* canvas = NULL;

//====================================================================
const uint8_t Font_04_B_30__12pt7bBitmaps[] PROGMEM = {};
const GFXglyph Font_04_B_30__12pt7bGlyphs[] PROGMEM = {};
const GFXfont Font_04_B_30__12pt7b PROGMEM = {
  (uint8_t*)Font_04_B_30__12pt7bBitmaps,
  (GFXglyph*)Font_04_B_30__12pt7bGlyphs,
  0x20, 0x7E, 24
};

//====================================================================
// --- CONFIGURACIÓN DE PINES ---
//====================================================================
#define TFT_CS D7
#define TFT_DC D6
#define TFT_RST D29
#define TFT_MOSI D10
#define TFT_SCK D8
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

#define REC_BUTTON_PIN D16
#define PLAY_BUTTON_PIN D17
#define STOP_BUTTON_PIN D18
#define BACK_BUTTON_PIN D22
#define FN_BUTTON_PIN D29
#define RESET_BUTTON_PIN D23  // Botón de reseteo
#define JACK_DETECT_PIN D2   // Pin para detectar si el jack de línea está conectado

const unsigned long BUTTON_DEBOUNCE_DELAY = 50;

#define ENC1_CLK_PIN D0
#define ENC1_DT_PIN D1
#define ENC1_SW_PIN D9

#define ENC2_CLK_PIN D3
#define ENC2_DT_PIN D4
#define ENC2_SW_PIN D5

#define ENC3_CLK_PIN D13
#define ENC3_DT_PIN D14
#define ENC3_SW_PIN D25

#define ENC4_CLK_PIN D20
#define ENC4_DT_PIN D21
#define ENC4_SW_PIN D24

const int PITCH_SENSITIVITY = 4; // 4 pulsos por semitono para un control más fino

static const int record_led_pin = D15;


#define LED_G_PIN D26 // Verde
#define LED_B_PIN D27 // Azul
#define LED_R_PIN D28 // Rojo

//====================================================================
// --- BÚFERES DE AUDIO ---
//====================================================================
static const uint32_t kBufferLengthSec = 10;
static const uint32_t kSampleRate = 48000;
static const size_t kBufferLengthSamples = kBufferLengthSec * kSampleRate;

// Buffers alineados a 32 bytes para optimización de caché (Cortex-M7)
static float DSY_SDRAM_BSS buffer[kBufferLengthSamples] __attribute__((aligned(32)));
static float DSY_SDRAM_BSS waveform_source_buffer[kBufferLengthSamples] __attribute__((aligned(32)));

// Ring buffer de undo/redo - 3 niveles
static float DSY_SDRAM_BSS undo_buffer_0[kBufferLengthSamples] __attribute__((aligned(32)));
static float DSY_SDRAM_BSS undo_buffer_1[kBufferLengthSamples] __attribute__((aligned(32)));
static float DSY_SDRAM_BSS undo_buffer_2[kBufferLengthSamples] __attribute__((aligned(32)));

// Array de punteros para pasar al looper
static float* undo_buffers[3] = {undo_buffer_0, undo_buffer_1, undo_buffer_2};

//====================================================================
// --- OBJETOS DE AUDIO Y ESTADOS GLOBALES ---
//====================================================================

static crearttech::OverdubLooper looper;
static daisysp::PitchShifter pitch_shifter;
static daisysp::Svf* g_highpass_filter;
static daisysp::Svf* g_lowpass_filter;
static uint8_t DSY_SDRAM_BSS reverb_memory[sizeof(daisysp::ReverbSc)];
static daisysp::ReverbSc* reverb_effect;
static daisysp::DelayLine<float, 4800> delay_effect;

enum LooperState { STOPPED, RECORDING, PLAYING, OVERDUB, PAUSED };
LooperState looper_state = STOPPED;

enum GlobalMode { MODE_INICIO, MODE_EDICION, MODE_FX };
GlobalMode current_mode = MODE_EDICION;

enum Knob2Mode { REVERB, SIZE, DECAY };
Knob2Mode knob2_mode = REVERB;

enum Knob3Mode { TIME, DELAY, MIX };
Knob3Mode knob3_mode = TIME;

enum Enc1Mode { PITCH, HIGHPASS, LOWPASS };
Enc1Mode enc1_mode = PITCH;
static float g_current_pitch_ratio = 1.0f;
static float g_playback_speed = 1.0f;

#define REV_BUTTON_PIN D19

bool last_rec_button_state = HIGH, last_play_button_state = HIGH, last_stop_button_state = HIGH, last_back_button_state = HIGH, last_rev_button_state = HIGH;
bool last_enc1_sw_state = HIGH, last_enc2_sw_state = HIGH, last_enc3_sw_state = HIGH, last_enc4_sw_state = HIGH;
bool last_fn_button_state = HIGH;

const unsigned long DOUBLE_PRESS_TIME_MS = 500;
unsigned long lastPlayPressTime = 0;
int playPressCount = 0;
unsigned long last_rev_button_press_time = 0;
unsigned long last_stop_press_time = 0;
int stop_press_count = 0;
unsigned long last_reset_press_time = 0;
int reset_press_count = 0;
bool last_reset_button_state = HIGH;
volatile int enc1_counter = 0, enc2_counter = 0, enc3_counter = 0;
static int last_e1 = 0, last_e2 = 0, last_e3 = 0;
volatile int enc4_counter = 0;
volatile unsigned long last_isr_time_4 = 0;
static int last_e4 = 0;

enum Enc4Mode {
    ENC4_MODE_START_POINT,
    ENC4_MODE_END_POINT,
    ENC4_MODE_MOVE,
    ENC4_MODE_GAIN 
};
Enc4Mode enc4_mode = ENC4_MODE_GAIN;

static float g_gain = 1.0f;
volatile int knob2_reverb_val = 0, knob2_size_val = 50, knob2_decay_val = 75;
volatile int knob3_time_val = 50, knob3_feedback_val = 50, knob3_mix_val = 0;
volatile float delay_time_samples = 0;
volatile float delay_feedback = 0.0f;
volatile float delay_mix = 0.0f;

bool reverse_mode = false;
volatile size_t record_counter = 0;
volatile size_t recorded_samples = 0;
volatile size_t play_position_samples = 0;
volatile bool speaker_muted = false;
bool has_undo_state = false;
unsigned long play_button_press_time = 0;
bool play_button_long_press_actioned = false;
int original_pitch = 0;
bool waveform_ready = false;
volatile bool waveform_display_needs_update = false;
float waveform_scale = 1.0f;
volatile size_t loop_start_sample = 0;
volatile size_t loop_end_sample = 0;
bool loop_edit_mode = false;

struct WaveformPixel { float min; float max; };
const int DISPLAY_W = (SCREEN_WIDTH - 5 * 2);
WaveformPixel displayWaveform[160];

struct Star { float x, y, z; float speed; };
#define MAX_STARS 100
Star stars[MAX_STARS];

//====================================================================
// --- CONSTANTES DE DISEÑO Y PALETA DE COLORES ---
//====================================================================
#define COLOR(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
const uint16_t C_BG = COLOR(10, 15, 25);
const uint16_t C_GRID = COLOR(30, 40, 60);
const uint16_t C_TEXT_LIGHT = COLOR(200, 220, 255);
const uint16_t C_TEXT_DARK = COLOR(100, 110, 130);
const uint16_t C_ACCENT_CYAN = COLOR(0, 255, 255);
const uint16_t C_ACCENT_MAGENTA = COLOR(255, 0, 255);
const uint16_t C_ACCENT_ORANGE = COLOR(0, 165, 255);
const uint16_t C_STATE_REC = COLOR(0, 0, 255);

const int STATUS_Y = 10;
const int WAVEFORM_X = 5, WAVEFORM_Y = 25, WAVEFORM_W = 300, WAVEFORM_H = 45;
const int KNOBS_Y = 85;

// Forward Declaration needed
void updateRgbLed(LooperState state);

//====================================================================
// --- LÓGICA DE ENCODERS POR INTERRUPCIÓN (ISR) ---
//====================================================================
volatile unsigned long last_isr_time_1 = 0;
volatile unsigned long last_isr_time_2 = 0;
volatile unsigned long last_isr_time_3 = 0;

void encoder1_isr() {
  if (micros() - last_isr_time_1 < 3000) return;
  last_isr_time_1 = micros();
  if (digitalRead(ENC1_DT_PIN) == digitalRead(ENC1_CLK_PIN)) {
    enc1_counter++;
  } else {
    enc1_counter--;
  }
}

void encoder2_isr() {
  if (micros() - last_isr_time_2 < 3000) return;
  last_isr_time_2 = micros();
  if (digitalRead(ENC2_DT_PIN) == digitalRead(ENC2_CLK_PIN)) {
    enc2_counter++;
  } else {
    enc2_counter--;
  }
}

void encoder3_isr() {
  if (micros() - last_isr_time_3 < 3000) return;
  last_isr_time_3 = micros();
  if (digitalRead(ENC3_DT_PIN) == digitalRead(ENC3_CLK_PIN)) {
    enc3_counter++;
  } else {
    enc3_counter--;
  }
}

void encoder4_isr() {
  if (micros() - last_isr_time_4 < 3000) return;
  last_isr_time_4 = micros();
  if (digitalRead(ENC4_DT_PIN) == digitalRead(ENC4_CLK_PIN)) {
    enc4_counter++;
  } else {
    enc4_counter--;
  }
}


void generarOndaVisual_Dinamica(WaveformPixel* displayBuf, int displayLen, float* audioBuf, size_t audioLen) {
  if (audioLen == 0 || displayLen <= 0) return;
  int samples_per_pixel = (int)(audioLen / displayLen);
  if (samples_per_pixel < 4) samples_per_pixel = 4;

  for (int i = 0; i < displayLen; i++) {
    size_t chunk_start = (size_t)i * samples_per_pixel;
    size_t chunk_end = chunk_start + samples_per_pixel;
    if (chunk_start >= audioLen) {
      displayBuf[i].min = 0.0f;
      displayBuf[i].max = 0.0f;
      continue;
    }
    if (chunk_end > audioLen) chunk_end = audioLen;
    float sum_sq = 0.0f;
    float min_val = 1.0f;
    float max_val = -1.0f;
    for (size_t j = chunk_start; j < chunk_end; j++) {
      float s = audioBuf[j];
      sum_sq += s * s;
      if (s < min_val) min_val = s;
      if (s > max_val) max_val = s;
    }
    float rms = sqrtf(sum_sq / (float)(chunk_end - chunk_start));
    const float blend = 0.65f;
    float vis_max = (max_val * blend) + (rms * (1.0f - blend));
    float vis_min = (min_val * blend) - (rms * (1.0f - blend));
    displayBuf[i].min = vis_min;
    displayBuf[i].max = vis_max;
  }
}

void drawBackground() {
  uint8_t r_start = 5, g_start = 10, b_start = 25;
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    uint8_t b = b_start - (y * b_start / SCREEN_HEIGHT);
    uint8_t g = g_start - (y * g_start / SCREEN_HEIGHT);
    uint8_t r = r_start - (y * r_start / SCREEN_HEIGHT);
    canvas->drawFastHLine(0, y, SCREEN_WIDTH, COLOR(r, g, b));
  }
  float center_x = SCREEN_WIDTH / 2.0;
  float center_y = SCREEN_HEIGHT / 2.0;
  for (int i = 0; i < MAX_STARS; i++) {
    stars[i].z -= stars[i].speed * 0.2;
    if (stars[i].z <= 0.5) {
      stars[i].x = random(-SCREEN_WIDTH / 2, SCREEN_WIDTH / 2);
      stars[i].y = random(-SCREEN_HEIGHT / 2, SCREEN_HEIGHT / 2);
      stars[i].z = random(8, 15);
      stars[i].speed = (15.0 - stars[i].z) * 0.3 + 0.5;
    }
    float perspective_scale = 1.0 / stars[i].z;
    int display_x = (int)(center_x + stars[i].x * perspective_scale * 5);
    int display_y = (int)(center_y + stars[i].y * perspective_scale * 5);
    uint16_t star_color = COLOR(200, 200, 200);
    int star_size = 1;
    if (stars[i].z < 3) {
      star_color = COLOR(255, 255, 255);
      star_size = 2;
    } else if (stars[i].z < 6) {
      star_color = COLOR(220, 220, 215);
    }
    if (display_x >= 0 && display_x < SCREEN_WIDTH && display_y >= 0 && display_y < SCREEN_HEIGHT) {
      if (star_size > 1) {
        canvas->fillRect(display_x, display_y, star_size, star_size, star_color);
      } else {
        canvas->drawPixel(display_x, display_y, star_color);
      }
    }
  }
}

void drawSplashScreen(float progress) {
  drawBackground();
  const char* title = "SAMPLER";
  int16_t x1, y1;
  uint16_t w, h;
  canvas->setFont(NULL);
  canvas->setTextSize(3);
  canvas->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  int16_t cursor_x = (SCREEN_WIDTH - w) / 2;
  int bar_height = 8;
  int spacing = 12;
  int total_content_height = h + spacing + bar_height;
  int16_t cursor_y = (SCREEN_HEIGHT - total_content_height) / 2;
  canvas->setCursor(cursor_x, cursor_y);
  canvas->setTextColor(C_ACCENT_CYAN);
  canvas->print(title);
  int bar_width = 100;
  int bar_x = (SCREEN_WIDTH - bar_width) / 2;
  int bar_y = cursor_y + h + spacing;
  canvas->drawRect(bar_x, bar_y, bar_width, bar_height, C_TEXT_LIGHT);
  int progress_width = (int)(progress * (float)(bar_width - 4));
  if (progress_width > 0) {
    canvas->fillRect(bar_x + 2, bar_y + 2, progress_width, bar_height - 4, C_ACCENT_CYAN);
  }
}

void drawStatusPanel() {
  const char* state_text;
  const char* state_icon;
  uint16_t state_color;
  switch (looper_state) {
    case RECORDING: state_text = "REC"; state_icon = "●"; state_color = C_STATE_REC; break;
    case PLAYING: state_text = "PLAY"; state_icon = "►"; state_color = COLOR(0, 255, 0); break;
    case OVERDUB: state_text = "OVERDUB"; state_icon = "+"; state_color = C_STATE_REC; break;
    case PAUSED: state_text = "PAUSE"; state_icon = "||"; state_color = COLOR(255, 0, 0); break;
    default: state_text = "STOP"; state_icon = "■"; state_color = C_ACCENT_ORANGE; break;
  }
  canvas->setCursor(10, STATUS_Y);
  switch (looper_state) {
    case RECORDING: canvas->fillCircle(10 + 6, STATUS_Y + 8, 6, state_color); break;
    case PLAYING: canvas->fillTriangle(10, STATUS_Y + 2, 10, STATUS_Y + 14, 10 + 12, STATUS_Y + 8, state_color); break;
    case PAUSED: canvas->setTextSize(2); canvas->setTextColor(state_color); canvas->print(state_icon); break;
    case OVERDUB: canvas->setTextSize(2); canvas->setTextColor(state_color); canvas->print(state_icon); break;
    default: canvas->fillRect(10, STATUS_Y + 2, 12, 12, state_color); break;
  }
  canvas->setTextSize(1);
  canvas->setCursor(30, STATUS_Y + 4);
  canvas->setTextColor(state_color);
  canvas->print(state_text);

  if (reverse_mode) {
    canvas->fillTriangle(SCREEN_WIDTH - 10, STATUS_Y + 2, SCREEN_WIDTH - 10, STATUS_Y + 14, SCREEN_WIDTH - 10 - 8, STATUS_Y + 8, C_ACCENT_MAGENTA);
    canvas->fillTriangle(SCREEN_WIDTH - 10 - 6, STATUS_Y + 2, SCREEN_WIDTH - 10 - 6, STATUS_Y + 14, SCREEN_WIDTH - 10 - 6 - 8, STATUS_Y + 8, C_ACCENT_MAGENTA);
  } else {
    canvas->fillTriangle(SCREEN_WIDTH - 10 - 8, STATUS_Y + 2, SCREEN_WIDTH - 10 - 8, STATUS_Y + 14, SCREEN_WIDTH - 10, STATUS_Y + 8, C_ACCENT_CYAN);
    canvas->fillTriangle(SCREEN_WIDTH - 10 - 8 - 6, STATUS_Y + 2, SCREEN_WIDTH - 10 - 8 - 6, STATUS_Y + 14, SCREEN_WIDTH - 10 - 6, STATUS_Y + 8, C_ACCENT_CYAN);
  }

  if (speaker_muted) {
    canvas->setFont(NULL); canvas->setTextSize(1); canvas->setTextColor(COLOR(0, 255, 0)); // Verde
    canvas->setCursor(SCREEN_WIDTH - 35, SCREEN_HEIGHT - 15); canvas->print("LINE");
  } else {
    canvas->setFont(NULL); canvas->setTextSize(1); canvas->setTextColor(COLOR(255, 0, 0)); // Rojo
    canvas->setCursor(SCREEN_WIDTH - 30, SCREEN_HEIGHT - 15); canvas->print("MIC");
  }
  updateRgbLed(looper_state);
}

void drawWaveform() {
  if (looper_state == STOPPED && !waveform_ready) return;
  int displayLen = DISPLAY_W;
  int draw_limit_x = displayLen;
  if (looper_state == RECORDING) {
    noInterrupts(); size_t local_count = record_counter; interrupts();
    draw_limit_x = (int)((float)local_count / (float)kBufferLengthSamples * displayLen);
    draw_limit_x = constrain(draw_limit_x, 0, displayLen);
  }
  if (waveform_ready) {
    int midY = WAVEFORM_Y + (WAVEFORM_H / 2);
    for (int x = 0; x < draw_limit_x; x++) {
      float min_val = displayWaveform[x].min * g_gain;
      float max_val = displayWaveform[x].max * g_gain;
      int y_top = midY - (int)(max_val * waveform_scale);
      int y_bottom = midY - (int)(min_val * waveform_scale);
      if (y_top > y_bottom) { int tmp = y_top; y_top = y_bottom; y_bottom = tmp; }
      y_top = constrain(y_top, WAVEFORM_Y, WAVEFORM_Y + WAVEFORM_H - 1);
      y_bottom = constrain(y_bottom, WAVEFORM_Y, WAVEFORM_Y + WAVEFORM_H - 1);
      int height = y_bottom - y_top;
      if (height < 1) height = 1;
      uint16_t waveform_color = C_ACCENT_CYAN;
      if (recorded_samples > 0) {
        int loop_start_screen_x = WAVEFORM_X + (int)((float)loop_start_sample / recorded_samples * displayLen);
        int loop_end_screen_x = WAVEFORM_X + (int)((float)loop_end_sample / recorded_samples * displayLen);
        loop_start_screen_x -= WAVEFORM_X; loop_end_screen_x -= WAVEFORM_X;
        if (x < loop_start_screen_x || x > loop_end_screen_x) waveform_color = C_TEXT_DARK;
      }
      canvas->drawFastVLine(WAVEFORM_X + x, y_top, height, waveform_color);
    }
    bool should_draw_playhead = (looper_state == PLAYING || looper_state == OVERDUB || looper_state == PAUSED);
    if (should_draw_playhead && recorded_samples > 0) {
      size_t absolute_playhead_pos;
      noInterrupts(); size_t relative_playhead = looper.GetLoopPlayheadPosition(); interrupts();
      absolute_playhead_pos = loop_start_sample + relative_playhead;
      if (absolute_playhead_pos >= recorded_samples) absolute_playhead_pos = recorded_samples - 1;
      float progress = (float)absolute_playhead_pos / (float)recorded_samples;
      int play_x = WAVEFORM_X + (int)(progress * displayLen);
      play_x = constrain(play_x, WAVEFORM_X, WAVEFORM_X + displayLen - 1);
      canvas->drawFastVLine(play_x, WAVEFORM_Y, WAVEFORM_H, C_ACCENT_MAGENTA);
    }
    if (recorded_samples > 0) {
      int start_x = WAVEFORM_X + (int)((float)loop_start_sample / recorded_samples * displayLen);
      int end_x = WAVEFORM_X + (int)((float)loop_end_sample / recorded_samples * displayLen);
      start_x = constrain(start_x, WAVEFORM_X, WAVEFORM_X + displayLen - 1);
      end_x = constrain(end_x, WAVEFORM_X, WAVEFORM_X + displayLen - 1);
      canvas->drawFastVLine(start_x, WAVEFORM_Y, WAVEFORM_H, C_TEXT_LIGHT);
      canvas->drawFastVLine(end_x, WAVEFORM_Y, WAVEFORM_H, C_TEXT_LIGHT);
    }
  }
}

void drawArc(Adafruit_GFX& gfx, int16_t cx, int16_t cy, int16_t radius, uint8_t thickness, int16_t start_angle, int16_t end_angle, uint16_t color) {
  if (end_angle < start_angle) { end_angle += 360; }
  for (int r = radius; r < radius + thickness; r++) {
    for (int i = start_angle; i <= end_angle; i++) {
        gfx.drawPixel(cx + r * cos(i * M_PI / 180.0), cy + r * sin(i * M_PI / 180.0), color);
    }
  }
}

void drawCircularKnob(Adafruit_GFX& gfx, int16_t cx, int16_t cy, const char* label, int16_t arc_start_angle, int16_t arc_end_angle, uint16_t fgColor, uint16_t textColor) {
  const int16_t radius = 18; const int16_t outline_radius = 12;
  gfx.drawCircle(cx, cy, outline_radius, COLOR(255, 255, 255));
  drawArc(gfx, cx, cy, radius, 3, arc_start_angle, arc_end_angle, fgColor);
  gfx.setFont(NULL); gfx.setTextSize(1); gfx.setTextColor(textColor);
  int16_t x1, y1; uint16_t w, h;
  gfx.setTextSize(1); gfx.setTextColor(C_TEXT_LIGHT);
  gfx.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(cx - w / 2, cy + radius + 5); gfx.print(label);
}

void drawKnobsPanel() {
  const char* knob1_label; int16_t knob1_arc_start, knob1_arc_end;
  if (enc1_mode == PITCH) {
    knob1_label = "PITCH"; int16_t center_angle = 270; int16_t max_sweep = 135;
    float pitch_value = (float)(enc1_counter / PITCH_SENSITIVITY);
    if (pitch_value >= 0) { knob1_arc_start = center_angle; knob1_arc_end = center_angle + (int16_t)((pitch_value / 6.0f) * max_sweep); }
    else { knob1_arc_start = center_angle - (int16_t)((-pitch_value / 6.0f) * max_sweep); knob1_arc_end = center_angle; }
  } else if (enc1_mode == HIGHPASS) {
    knob1_label = "HPASS"; knob1_arc_start = 135; knob1_arc_end = 135 + (int16_t)((float)enc1_counter / 100.0f * 270);
  } else {
    knob1_label = "LPASS"; knob1_arc_start = 135; knob1_arc_end = 135 + (int16_t)((float)enc1_counter / 100.0f * 270);
  }
  drawCircularKnob(*canvas, 30, KNOBS_Y + 10, knob1_label, knob1_arc_start, knob1_arc_end, C_ACCENT_MAGENTA, C_TEXT_LIGHT);

  const char* knob3_label; int knob3_val_to_display;
  switch (knob3_mode) {
    case DELAY: knob3_label = "FBACK"; knob3_val_to_display = knob3_feedback_val; break;
    case MIX: knob3_label = "MIX"; knob3_val_to_display = knob3_mix_val; break;
    default: knob3_label = "DELAY"; knob3_val_to_display = knob3_time_val; break;
  }
  drawCircularKnob(*canvas, 80, KNOBS_Y + 10, knob3_label, 135, 135 + (int16_t)((float)knob3_val_to_display / 100.0f * 270), C_ACCENT_ORANGE, C_TEXT_LIGHT);

  const char* knob2_label;
  if (knob2_mode == SIZE) knob2_label = "SIZE"; else if (knob2_mode == DECAY) knob2_label = "DECAY"; else knob2_label = "REVERB";
  drawCircularKnob(*canvas, 130, KNOBS_Y + 10, knob2_label, 135, 135 + (int16_t)((float)enc2_counter / 100.0f * 270), C_ACCENT_CYAN, C_TEXT_LIGHT);
}

void drawScreen() {
  drawBackground();
  drawStatusPanel();
  drawWaveform();
  drawKnobsPanel();
  int current_y = STATUS_Y + 4; int text_x = SCREEN_WIDTH - 50;
  canvas->setFont(NULL); canvas->setTextSize(1); canvas->setTextWrap(false);
  const char* enc4_mode_text; uint16_t enc4_mode_color = C_ACCENT_MAGENTA;
  switch (enc4_mode) {
    case ENC4_MODE_START_POINT: enc4_mode_text = "S.PT"; break;
    case ENC4_MODE_END_POINT: enc4_mode_text = "E.PT"; break;
    case ENC4_MODE_MOVE: enc4_mode_text = "MOVE"; break;
    case ENC4_MODE_GAIN: enc4_mode_text = "GAIN"; break;
    default: enc4_mode_text = ""; break;
  }
  int16_t x1, y1; uint16_t w, h;
  canvas->getTextBounds(enc4_mode_text, 0, 0, &x1, &y1, &w, &h);
  canvas->setCursor(text_x - w, current_y); canvas->setTextColor(enc4_mode_color); canvas->print(enc4_mode_text);
}

void updateRgbLed(LooperState state) {
  // Configurar LED de Grabación (D15)
  if (state == RECORDING || state == OVERDUB) {
    digitalWrite(record_led_pin, HIGH); // Encender LED de Grabación
  } else {
    digitalWrite(record_led_pin, LOW);  // Apagar LED de Grabación
  }

  // Apagar todos los LEDs RGB primero (HIGH = OFF para ánodo común)
  digitalWrite(LED_R_PIN, HIGH); 
  digitalWrite(LED_G_PIN, HIGH); 
  digitalWrite(LED_B_PIN, HIGH);
  
  switch (state) {
      case RECORDING: 
        // ROJO PURO (solo rojo encendido)
        digitalWrite(LED_R_PIN, LOW); 
        digitalWrite(LED_G_PIN, HIGH); 
        digitalWrite(LED_B_PIN, HIGH); 
        break;
      case PLAYING: 
        // VERDE PURO (solo verde encendido)
        digitalWrite(LED_R_PIN, HIGH); 
        digitalWrite(LED_G_PIN, LOW); 
        digitalWrite(LED_B_PIN, HIGH); 
        break;
      case STOPPED: 
        // AMARILLO (rojo + verde)
        digitalWrite(LED_R_PIN, LOW); 
        digitalWrite(LED_G_PIN, LOW); 
        digitalWrite(LED_B_PIN, HIGH); 
        break;
      case OVERDUB: 
        // ROJO PURO (como RECORDING)
        digitalWrite(LED_R_PIN, LOW); 
        digitalWrite(LED_G_PIN, HIGH); 
        digitalWrite(LED_B_PIN, HIGH); 
        break;
      case PAUSED: 
        // AZUL PURO (solo azul encendido)
        digitalWrite(LED_R_PIN, HIGH); 
        digitalWrite(LED_G_PIN, HIGH); 
        digitalWrite(LED_B_PIN, LOW); 
        break;
    }
}

//====================================================================
// --- AUDIO CALLBACK ---
//====================================================================
void AudioCallback(float** in, float** out, size_t size) {

  delay_effect.SetDelay(delay_time_samples);

  for (size_t i = 0; i < size; i++) {
    float input_signal = in[0][i];


    if (!speaker_muted) {
      input_signal = 0.0f;
    }


    if (looper_state == RECORDING || looper_state == OVERDUB) {
      looper.Process(in[0][i]);

      if (looper_state == RECORDING) {
        size_t pos = record_counter;
        if (pos < kBufferLengthSamples) {
          waveform_source_buffer[pos] = in[0][i];
          record_counter++;
          if (record_counter > kBufferLengthSamples) record_counter = kBufferLengthSamples;
          waveform_display_needs_update = true;
        }
      }
    }


    float looper_output = 0.0f;
    if (looper_state == PLAYING) {
      looper_output = looper.Process(0.0f);
    }


    float signal_to_process = input_signal + looper_output;




    if (enc1_mode == HIGHPASS) {
      g_highpass_filter->Process(signal_to_process);
      signal_to_process = g_highpass_filter->High();
    } else if (enc1_mode == LOWPASS) {
      g_lowpass_filter->Process(signal_to_process);
      signal_to_process = g_lowpass_filter->Low();
    }


    float delayed = delay_effect.Read();
    delay_effect.Write(signal_to_process + (delayed * delay_feedback));
    float post_delay = (signal_to_process * (1.0f - delay_mix)) + (delayed * delay_mix);


    float reverb_out_l = 0.0f, reverb_out_r = 0.0f;
    float reverb_mix = (float)knob2_reverb_val / 100.0f;
    float mono_reverb = 0.0f;

    if (reverb_mix > 0.0f) {
      reverb_effect->Process(post_delay, post_delay, &reverb_out_l, &reverb_out_r);
      mono_reverb = (reverb_out_l + reverb_out_r) * 0.5f;
    }

    float wet_signal = (post_delay * (1.0f - reverb_mix)) + (mono_reverb * reverb_mix);


    float final_signal = wet_signal * g_gain;
    final_signal = tanhf(final_signal) ;


    out[0][i] = out[1][i] = final_signal;
  }
}

void resetSystem() {
  pitch_shifter.Init(DAISY.AudioSampleRate());
  delay_effect.Reset();
  g_current_pitch_ratio = 1.0f;
  g_highpass_filter->SetFreq(10.0f);
  g_lowpass_filter->SetFreq(20000.0f);
  knob2_reverb_val = 0; knob2_size_val = 0; knob2_decay_val = 0;
  reverb_effect->SetFeedback(0.0f); reverb_effect->SetLpFreq(20000.0f);
  knob3_time_val = 0; knob3_feedback_val = 0; knob3_mix_val = 0;
  delay_time_samples = 0; delay_feedback = 0.0f; delay_mix = 0.0f;
  noInterrupts(); enc1_counter = 0; enc2_counter = 0; enc3_counter = 0; last_e1 = 0; interrupts();
  enc1_mode = PITCH; knob2_mode = REVERB; knob3_mode = TIME;
  waveform_display_needs_update = true;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  DAISY.init(DAISY_SEED, AUDIO_SR_48K);
  float sample_rate = DAISY.get_samplerate();


  canvas = new GFXcanvas16(SCREEN_WIDTH, SCREEN_HEIGHT);
  
  looper.Init(buffer, kBufferLengthSamples, undo_buffers, 3);  // 3 niveles de undo/redo
  pitch_shifter.Init(DAISY.AudioSampleRate());
  pitch_shifter.SetFun(1.0f);
  g_highpass_filter = new daisysp::Svf();
  g_highpass_filter->Init(DAISY.AudioSampleRate());
  g_highpass_filter->SetRes(0.7f); g_highpass_filter->SetDrive(0.7f); g_highpass_filter->SetFreq(10.0f);
  g_lowpass_filter = new daisysp::Svf();
  g_lowpass_filter->Init(DAISY.AudioSampleRate());
  g_lowpass_filter->SetRes(0.7f); g_lowpass_filter->SetDrive(0.7f); g_lowpass_filter->SetFreq(20000.0f);
  delay_effect.Init();
  delay_effect.SetDelay(2400.0f);
  reverb_effect = new (reverb_memory) daisysp::ReverbSc();
  reverb_effect->Init(DAISY.AudioSampleRate());

  for (int i = 0; i < MAX_STARS; i++) {
    stars[i].x = random(-SCREEN_WIDTH / 2, SCREEN_WIDTH / 2);
    stars[i].y = random(-SCREEN_HEIGHT / 2, SCREEN_HEIGHT / 2);
    stars[i].z = random(8, 15);
    stars[i].speed = (15.0 - stars[i].z) * 0.3 + 0.5;
  }

  pinMode(REC_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PLAY_BUTTON_PIN, INPUT_PULLUP);
  pinMode(STOP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BACK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(FN_BUTTON_PIN, INPUT_PULLUP);
  pinMode(REV_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(JACK_DETECT_PIN, INPUT_PULLUP);

  pinMode(ENC1_CLK_PIN, INPUT_PULLUP); pinMode(ENC1_DT_PIN, INPUT_PULLUP); pinMode(ENC1_SW_PIN, INPUT_PULLUP);
  pinMode(ENC2_CLK_PIN, INPUT_PULLUP); pinMode(ENC2_DT_PIN, INPUT_PULLUP); pinMode(ENC2_SW_PIN, INPUT_PULLUP);
  pinMode(ENC3_CLK_PIN, INPUT_PULLUP); pinMode(ENC3_DT_PIN, INPUT_PULLUP); pinMode(ENC3_SW_PIN, INPUT_PULLUP);
  pinMode(ENC4_CLK_PIN, INPUT_PULLUP); pinMode(ENC4_DT_PIN, INPUT_PULLUP); pinMode(ENC4_SW_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENC1_CLK_PIN), encoder1_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC2_CLK_PIN), encoder2_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC3_CLK_PIN), encoder3_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC4_CLK_PIN), encoder4_isr, CHANGE);

  pinMode(record_led_pin, OUTPUT); digitalWrite(record_led_pin, LOW);

  pinMode(LED_R_PIN, OUTPUT); pinMode(LED_G_PIN, OUTPUT); pinMode(LED_B_PIN, OUTPUT);

  digitalWrite(LED_R_PIN, HIGH); digitalWrite(LED_G_PIN, HIGH); digitalWrite(LED_B_PIN, HIGH);

  tft.initR(INITR_GREENTAB);
  tft.fillScreen(ST77XX_BLACK);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  unsigned long splash_start_time = millis();
  while (millis() - splash_start_time < 2000) {
    float progress = (float)(millis() - splash_start_time) / 2000.0f;
    drawSplashScreen(progress);
    tft.drawRGBBitmap(0, 0, canvas->getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
    delay(30);
  }
  DAISY.StartAudio(AudioCallback);
}

void loop() {

  static unsigned long last_jack_check = 0;
  if (millis() - last_jack_check > 200) {
    last_jack_check = millis();

    if (digitalRead(JACK_DETECT_PIN) != LOW) {
      speaker_muted = true;
    } else {
      speaker_muted = false;
    }
  }
  noInterrupts();
  int e1 = enc1_counter; int e2 = enc2_counter; int e3 = enc3_counter; int e4 = enc4_counter;
  interrupts();
  int e1_delta = e1 - last_e1; last_e1 = e1;
  int e4_delta = e4 - last_e4; last_e4 = e4;

  bool enc4_sw = digitalRead(ENC4_SW_PIN);
  if (last_enc4_sw_state == HIGH && enc4_sw == LOW) {
    if (enc4_mode == ENC4_MODE_GAIN) enc4_mode = ENC4_MODE_START_POINT;
    else if (enc4_mode == ENC4_MODE_START_POINT) enc4_mode = ENC4_MODE_END_POINT;
    else if (enc4_mode == ENC4_MODE_END_POINT) enc4_mode = ENC4_MODE_MOVE;
    else enc4_mode = ENC4_MODE_GAIN;
    noInterrupts(); enc4_counter = 0; last_e4 = 0; interrupts();
  }
  last_enc4_sw_state = enc4_sw;

  if (e4_delta != 0 && recorded_samples > 0) {
    int sensitivity = max(1, (int)(recorded_samples / 500));
    long delta = (long)e4_delta * sensitivity;
    switch (enc4_mode) {
      case ENC4_MODE_START_POINT: {
        long new_start = (long)loop_start_sample + delta;
        if (new_start < 0) new_start = 0;
        if (new_start >= loop_end_sample) new_start = (loop_end_sample > 0) ? (loop_end_sample - 1) : 0;
        loop_start_sample = (size_t)new_start; break;
      }
      case ENC4_MODE_END_POINT: {
        long new_end = (long)loop_end_sample + delta;
        if (new_end <= loop_start_sample) new_end = loop_start_sample + 1;
        if (new_end >= recorded_samples) new_end = recorded_samples - 1;
        if (new_end < 0) new_end = 0;
        loop_end_sample = (size_t)new_end; break;
      }
      case ENC4_MODE_MOVE: {
        long new_start = (long)loop_start_sample + delta;
        long new_end = (long)loop_end_sample + delta;
        if (new_start < 0) { new_start = 0; new_end = (loop_end_sample - loop_start_sample); }
        if (new_end >= recorded_samples) { new_end = recorded_samples - 1; new_start = new_end - (loop_end_sample - loop_start_sample); }
        if (new_start < 0) new_start = 0;
        if (new_start >= new_end) { new_start = new_end - 1; if (new_start < 0) new_start = 0; }
        loop_start_sample = (size_t)new_start; loop_end_sample = (size_t)new_end; break;
      }
      case ENC4_MODE_GAIN: {
        g_gain += (float)e4_delta * 0.01f; g_gain = constrain(g_gain, 0.0f, 2.0f); break;
      }
    }
    looper.SetLoopRegion(loop_start_sample, loop_end_sample);
  }

  // ENC1
  switch (enc1_mode) {
    case PITCH: {
        int pitch_semitones = e1 / PITCH_SENSITIVITY;
        pitch_semitones = constrain(pitch_semitones, -6, 6);
        g_current_pitch_ratio = powf(2.0f, (float)pitch_semitones / 12.0f);
        looper.SetPlaybackSpeed(g_current_pitch_ratio);
      } break;
    case HIGHPASS: {
        e1 = constrain(e1, 0, 100); noInterrupts(); enc1_counter = e1; interrupts();
        g_highpass_filter->SetFreq(20.0f * powf(500.0f, (float)e1 / 100.0f));
      } break;
    case LOWPASS: {
        e1 = constrain(e1, 0, 100); noInterrupts(); enc1_counter = e1; interrupts();
        g_lowpass_filter->SetFreq(200.0f * powf(100.0f, (float)e1 / 100.0f));
      } break;
  }

  // ENC2, ENC3
  e2 = constrain(e2, 0, 100); e3 = constrain(e3, 0, 100);
  noInterrupts(); enc2_counter = e2; enc3_counter = e3; interrupts();

  bool enc2_sw = digitalRead(ENC2_SW_PIN);
  if (last_enc2_sw_state == HIGH && enc2_sw == LOW) {
    if (knob2_mode == REVERB) { knob2_mode = SIZE; enc2_counter = knob2_size_val; }
    else if (knob2_mode == SIZE) { knob2_mode = DECAY; enc2_counter = knob2_decay_val; }
    else { knob2_mode = REVERB; enc2_counter = knob2_reverb_val; }
  }
  last_enc2_sw_state = enc2_sw;
  reverb_effect->SetFeedback(((float)knob2_decay_val / 100.0f) * 0.70f);
  reverb_effect->SetLpFreq(500.0f + ((float)knob2_size_val / 100.0f * 15000.0f));

  bool fn_button = digitalRead(FN_BUTTON_PIN);
  if (last_fn_button_state == HIGH && fn_button == LOW) loop_edit_mode = !loop_edit_mode;
  last_fn_button_state = fn_button;

  switch (knob2_mode) {
    case REVERB: knob2_reverb_val = e2; break;
    case SIZE: knob2_size_val = e2; break;
    case DECAY: knob2_decay_val = e2; break;
  }
  bool enc3_sw = digitalRead(ENC3_SW_PIN);
  if (last_enc3_sw_state == HIGH && enc3_sw == LOW) {
    if (knob3_mode == TIME) { knob3_mode = DELAY; enc3_counter = knob3_feedback_val; }
    else if (knob3_mode == DELAY) { knob3_mode = MIX; enc3_counter = knob3_mix_val; }
    else { knob3_mode = TIME; enc3_counter = knob3_time_val; }
  }
  last_enc3_sw_state = enc3_sw;
  switch (knob3_mode) {
    case TIME: { knob3_time_val = e3; float delay_ms = (float)knob3_time_val / 100.0f * 100.0f; if (delay_ms < 1.0f) delay_ms = 1.0f; delay_time_samples = DAISY.AudioSampleRate() / 1000.0f * delay_ms; } break;
    case DELAY: delay_feedback = (float)e3 / 100.0f * 0.70f; knob3_feedback_val = e3; break;
    case MIX: delay_mix = (float)e3 / 100.0f; knob3_mix_val = e3; break;
  }

  noInterrupts(); size_t current_recorded_samples = record_counter; interrupts();
  if (current_recorded_samples > 0) {
    float max_abs_val = 1e-6f;
    for (size_t i = 0; i < current_recorded_samples; i++) {
        float a = fabsf(waveform_source_buffer[i]); if (a > max_abs_val) max_abs_val = a;
    }
    if (max_abs_val < 1e-6f) max_abs_val = 1e-6f;
    waveform_scale = ((WAVEFORM_H / 2.0f) / max_abs_val) * 0.7f;
    generarOndaVisual_Dinamica(displayWaveform, DISPLAY_W, waveform_source_buffer, current_recorded_samples);
    waveform_ready = true;
  } else waveform_ready = false;
  waveform_display_needs_update = false;

  bool rec_button = digitalRead(REC_BUTTON_PIN);
  bool play_button = digitalRead(PLAY_BUTTON_PIN);
  bool stop_button = digitalRead(STOP_BUTTON_PIN);
  bool reset_button = digitalRead(RESET_BUTTON_PIN);

  if (last_reset_button_state == HIGH && reset_button == LOW) {
    unsigned long currentTime = millis();
    if (currentTime - last_reset_press_time < DOUBLE_PRESS_TIME_MS) reset_press_count++; else reset_press_count = 1;
    last_reset_press_time = currentTime;
    if (reset_press_count == 2) {
      if (recorded_samples > 0) {
        loop_start_sample = 0; loop_end_sample = recorded_samples - 1;
        looper.SetLoopRegion(loop_start_sample, loop_end_sample);
      }
      reset_press_count = 0;
    }
  }
  if (reset_press_count == 1 && (millis() - last_reset_press_time > DOUBLE_PRESS_TIME_MS)) {
    resetSystem(); reset_press_count = 0;
  }
  last_reset_button_state = reset_button;

  if (last_enc1_sw_state == HIGH && digitalRead(ENC1_SW_PIN) == LOW) {
    if (enc1_mode == PITCH) enc1_mode = HIGHPASS; else if (enc1_mode == HIGHPASS) enc1_mode = LOWPASS; else enc1_mode = PITCH;
  }
  last_enc1_sw_state = digitalRead(ENC1_SW_PIN);

  bool rec_button_is_pressed = (rec_button == LOW);
  bool rec_button_was_pressed = (last_rec_button_state == LOW);
  if (rec_button_is_pressed && !rec_button_was_pressed) {
    if (looper_state == STOPPED) {
      memset(buffer, 0, sizeof(float) * kBufferLengthSamples);
      looper.StartRecording(); looper_state = RECORDING;
      recorded_samples = 0; record_counter = 0; has_undo_state = false; waveform_ready = false;
    } else if (looper_state == PLAYING) {
      looper.StartOverdub(); looper_state = OVERDUB;
    }
  }
  if (!rec_button_is_pressed && rec_button_was_pressed) {
    if (looper_state == RECORDING) {
      looper.StopRecording(); recorded_samples = record_counter;
      loop_start_sample = 0; loop_end_sample = recorded_samples > 0 ? recorded_samples - 1 : 0;
      looper.SetLoopRegion(loop_start_sample, loop_end_sample);
      looper_state = PLAYING;
    } else if (looper_state == OVERDUB) {
      looper.StopOverdub(); looper_state = PLAYING;
    }
  }
  last_rec_button_state = rec_button;

  if (last_play_button_state == HIGH && play_button == LOW) {
    play_button_press_time = millis(); play_button_long_press_actioned = false;
    unsigned long currentTime = millis();
    if (currentTime - lastPlayPressTime < DOUBLE_PRESS_TIME_MS) playPressCount++; else playPressCount = 1;
    lastPlayPressTime = currentTime;
    if (playPressCount == 2) {
      looper.Restart(); if (looper_state == RECORDING) looper.StopRecording();
      looper_state = STOPPED; recorded_samples = 0;
      noInterrupts(); record_counter = 0; interrupts();
      has_undo_state = false; waveform_ready = false; playPressCount = 0;
    }
  }
  if (play_button == LOW && !play_button_long_press_actioned) {
    if (millis() - play_button_press_time > 500) { play_button_long_press_actioned = true; }
  }
  if (playPressCount == 1 && (millis() - lastPlayPressTime > DOUBLE_PRESS_TIME_MS)) {
    if (!play_button_long_press_actioned) {
      if (looper_state == PAUSED) looper_state = PLAYING;
      else if (looper_state == PLAYING) looper_state = PAUSED;
    }
    playPressCount = 0;
  }
  last_play_button_state = play_button;

  if (stop_button == LOW && last_stop_button_state == HIGH) {
    reverse_mode = !reverse_mode; looper.SetReverse(reverse_mode);
  }
  last_stop_button_state = stop_button;

  bool current_rev_button_state = digitalRead(REV_BUTTON_PIN); last_rev_button_state = current_rev_button_state;
  bool current_back_button_state = digitalRead(BACK_BUTTON_PIN); last_back_button_state = current_back_button_state;

  static unsigned long last_draw = 0;
  if (millis() - last_draw > 30) {
    drawScreen(); tft.drawRGBBitmap(0, 0, canvas->getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
    last_draw = millis();
  }
}