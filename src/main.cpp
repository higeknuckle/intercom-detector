#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "Mic_FFT.hpp"
#include "debug.hpp"

#define WIFI_SSID "<SSID>"
#define WIFI_PASS "<PASSWORD>"

#define LINE_CHANNEL_ACCESS_TOKEN "<TOKEN>"

static M5GFX *display;
static M5Canvas *canvas;

bool sendLineNotification(const char *message)
{
  WiFiClient client;
  HTTPClient http;

  // Set up the HTTP request
  http.begin(client, "https://api.line.me/v2/bot/message/broadcast");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(LINE_CHANNEL_ACCESS_TOKEN));

  // Create the JSON payload
  JsonDocument doc;
  doc["messages"][0]["type"] = "text";
  doc["messages"][0]["text"] = message;
  String payload;
  serializeJson(doc, payload);
  M5_LOGD("Payload: %s", payload.c_str());

  // Send the POST request
  // /*
  int httpResponseCode = http.POST(payload);

  // Check the response code
  if (httpResponseCode != HTTP_CODE_OK)
  {
    M5_LOGD("Error sending notification. httpResponseCode: %d", httpResponseCode);
    M5_LOGD("Response: %s", http.getString().c_str());
    return false;
  }

  M5_LOGD("Notification sent successfully.");
  // */
  return true;
}

class detector_t
{
  const uint16_t sound1_index;
  const uint16_t sound1_duration_ms;
  const uint16_t sound2_index;
  const uint16_t sound2_duration_ms;

  const unsigned long active_window_ms;

  unsigned long sound1_start_time = 0;
  unsigned long sound2_start_time = 0;
  unsigned long sound1_active_until = 0;
  unsigned long sound2_active_until = 0;
  bool sound1_detected = false;
  bool sound2_detected = false;

  const long cooldown_ms = 2000;
  unsigned long cooldown_until = 0;

public:
  detector_t(uint16_t sound1_index,
             unsigned long sound1_duration_ms,
             uint16_t sound2_index,
             unsigned long sound2_duration_ms,
             unsigned long active_window_ms)
      : sound1_index(sound1_index),
        sound1_duration_ms(sound1_duration_ms),
        sound2_index(sound2_index),
        sound2_duration_ms(sound2_duration_ms),
        active_window_ms(active_window_ms)
  {
  }

  bool update(const fft_peak_t &fft_peak)
  {
    unsigned long current_time = millis();

    // check 1st sound
    if (fft_peak.isPeak(sound1_index))
    {
      if (sound1_start_time == 0)
      {
        sound1_start_time = current_time;
      }
      else if (current_time - sound1_start_time >= sound1_duration_ms)
      {
        sound1_detected = true;
      }
      sound1_active_until = current_time + active_window_ms;
    }
    else if (current_time - sound1_start_time > active_window_ms)
    {
      sound1_start_time = 0;
      sound1_active_until = 0;
      sound1_detected = false;
    }

    // check 2nd sound
    if (fft_peak.isPeak(sound2_index))
    {
      if (sound2_start_time == 0)
      {
        sound2_start_time = current_time;
      }
      else if (current_time - sound2_start_time >= sound2_duration_ms)
      {
        sound2_detected = true;
      }
      sound2_active_until = current_time + active_window_ms;
    }
    else if (current_time - sound2_start_time > active_window_ms)
    {
      sound2_start_time = 0;
      sound2_active_until = 0;
      sound2_detected = false;
    }

    return true;
  }

  bool detected()
  {
    unsigned long current_time = millis();
    if (current_time < cooldown_until)
    {
      return false;
    }

    if (sound1_detected && sound2_detected)
    {
      cooldown_until = current_time + cooldown_ms;
      return true;
    }
    return false;
  }
};

class detect_drawer_t
{
  LGFX_Device *_gfx = nullptr;
  M5Canvas *_canvas = nullptr;
  M5Canvas *_c_indicator = nullptr;
  M5Canvas *_c_status_line = nullptr;
  M5Canvas *_c_status_wifi = nullptr;
  M5Canvas *_c_message = nullptr;

  rect_t draw_rect = {0, 0, 0, 0};

  uint32_t bg_color = TFT_BLACK;
  uint32_t fg_color = TFT_WHITE;
  uint32_t ok_color = TFT_DARKGREEN;
  uint32_t error_color = TFT_MAROON;

public:
  bool setup(LGFX_Device *gfx, const rect_t &rect)
  {
    if (gfx == nullptr)
    {
      return false;
    }
    _gfx = gfx;
    draw_rect = rect;

    _canvas = new M5Canvas(gfx);
    _canvas->createSprite(draw_rect.w, draw_rect.h);
    rect_t rect_canvas = {0, 0, (int16_t)_canvas->width(), (int16_t)_canvas->height()};

    _c_indicator = new M5Canvas(_canvas);
    _c_status_line = new M5Canvas(_canvas);
    _c_status_wifi = new M5Canvas(_canvas);
    _c_message = new M5Canvas(_canvas);

    rect_t rect_indicator = {rect_canvas.x, rect_canvas.y, (int16_t)(rect_canvas.w * (2.0 / 3.0)), (int16_t)(rect_canvas.h / 2.0)};
    rect_t rect_status_line = {(int16_t)(rect_canvas.x + rect_indicator.w), rect_canvas.y, (int16_t)(rect_canvas.w * (1.0 / 3.0)), (int16_t)(rect_canvas.h / 4.0)};
    rect_t rect_status_wifi = {(int16_t)(rect_canvas.x + rect_indicator.w), (int16_t)(rect_canvas.y + rect_status_line.h), (int16_t)(rect_canvas.w * (1.0 / 3.0)), (int16_t)(rect_canvas.h / 4.0)};
    rect_t rect_message = {rect_canvas.x, (int16_t)(rect_canvas.y + rect_indicator.h), rect_canvas.w, (int16_t)(rect_canvas.h / 2.0)};

    _c_indicator->createSprite(rect_indicator.w, rect_indicator.h);
    _c_status_line->createSprite(rect_status_line.w, rect_status_line.h);
    _c_status_wifi->createSprite(rect_status_wifi.w, rect_status_wifi.h);
    _c_message->createSprite(rect_message.w, rect_message.h);

    // M5_LOGD("default canvas font: %s", _canvas->getFont());
    // _c_status_line->setFont(&fonts::lgfxJapanGothic_20);
    // _c_status_wifi->setFont(_c_status_line->getFont());
    float status_text_size = _c_status_line->getTextSizeX() * (_c_status_line->width() - 4) / (float)std::max(_c_status_line->textWidth("LINE"), _c_status_wifi->textWidth("WiFi"));
    M5_LOGD("status_text_size: %f", status_text_size);
    _c_status_line->fillRect(0, 0, rect_status_line.w, rect_status_line.h, error_color);
    _c_status_line->setTextSize(status_text_size);
    _c_status_line->setTextColor(fg_color);
    _c_status_line->setTextDatum(MC_DATUM);
    _c_status_line->drawString("LINE", rect_status_line.w / 2 + 2, rect_status_line.h / 2);
    _c_status_wifi->fillRect(0, 0, rect_status_wifi.w, rect_status_wifi.h, error_color);
    _c_status_wifi->setTextSize(status_text_size);
    _c_status_wifi->setTextColor(fg_color);
    _c_status_wifi->setTextDatum(MC_DATUM);
    _c_status_wifi->drawString("WiFi", rect_status_wifi.w / 2 + 2, rect_status_wifi.h / 2);
    M5_LOGD("status_text created");

    // update display
    _c_indicator->pushSprite(rect_indicator.x, rect_indicator.y);
    _c_status_line->pushSprite(rect_status_line.x, rect_status_line.y);
    _c_status_wifi->pushSprite(rect_status_wifi.x, rect_status_wifi.y);
    _c_message->pushSprite(rect_message.x, rect_message.y);
    M5_LOGD("sub canvas pushed");
    _canvas->pushSprite(draw_rect.x, draw_rect.y);
    M5_LOGD("canvas pushed");

    return true;
  }

  bool update(const fft_data_t &fft_data, const fft_peak_t &fft_peak)
  {
    if (_gfx == nullptr)
    {
      return false;
    }

    // update indicator

    // update status
    // if (WiFi.status() == WL_CONNECTED)
    // {
    //   _c_status_wifi->fillRect(0, 0, _c_status_wifi->width(), _c_status_wifi->height(), ok_color);
    // }
    // else
    // {
    //   _c_status_wifi->fillRect(0, 0, _c_status_wifi->width(), _c_status_wifi->height(), error_color);
    // }
    _c_status_wifi->fillRect(0, 0, _c_status_wifi->width(), _c_status_wifi->height(), WiFi.status() == WL_CONNECTED ? ok_color : error_color);

    // update message

    // update display

    return true;
  }

private:
};

static fft_function_t fft_function;
static fft_data_t fft_data;
static wav_data_t wav_data;
static wav_drawer_t wav_drawer;
static fft_drawer_t fft_drawer;
static fft_peak_t fft_peak;
static fft_history_t fft_history;
static debug_drawer_t debug_drawer;
static detector_t intercom1_detector(INDEX_INTERCOM_1_NOTE_1, 500.0f, INDEX_INTERCOM_1_NOTE_2, 700.0f, 2000.0f);
static detector_t intercom2_detector(INDEX_INTERCOM_2_NOTE_1, 500.0f, INDEX_INTERCOM_2_NOTE_2, 700.0f, 2000.0f);
static detect_drawer_t detect_drawer;

void setup()
{

  // Initialize M5
  {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
  }

  // Initialize Logger
  M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_DEBUG);
  M5.Log.setEnableColor(m5::log_target_serial, false);
  M5.Log.setSuffix(m5::log_target_serial, "\n");
  M5_LOGD("Logger initialized.");
  M5.delay(3000);

  // Initialize Display
  display = &M5.Display;
  // if (display->width() < display->height())
  if (display->width() > display->height())
  {
    display->setRotation(display->getRotation() ^ 1);
  }
  canvas = new M5Canvas(display);
  canvas->createSprite(display->width(), display->height());
  canvas->setColorDepth(1);
  canvas->setTextSize((float)canvas->width() / 160);
  canvas->setTextScroll(true);

  // Initialize Mic
  {
    auto cfg = M5.Mic.config();
    cfg.dma_buf_count = 3;
    cfg.dma_buf_len = WAVE_BLOCK_SIZE;
    cfg.over_sampling = 1;
    cfg.noise_filter_level = 0;
    cfg.sample_rate = SAMPLE_RATE;
    cfg.magnification = cfg.use_adc ? 16 : 1;
    M5.Mic.config(cfg);
  }
  if (!M5.Mic.isEnabled())
  {
    M5_LOGE("Microphone is not available.");
    canvas->println("Microphone is not available.");
    canvas->pushSprite(0, 0);
    while (true)
    {
      M5.delay(1000);
    }
  }
  M5_LOGD("Microphone initialized.");

  // Prepare for FFT
  fft_function.setup(FFT_BITS);
  fft_data.fft_size_bits = FFT_BITS;
  fft_data.sample_rate = SAMPLE_RATE;
  fft_data.wav_data = &wav_data;
  fft_data.length = (1 << (fft_data.fft_size_bits - 1)) + 1;
  fft_data.fdata = (typeof(fft_data.fdata))memory_alloc(fft_data.length * sizeof(fft_data.fdata[0]));
  wav_data.length = WAVE_TOTAL_SIZE;
  wav_data.wav = (typeof(wav_data.wav))memory_alloc(WAVE_TOTAL_SIZE * sizeof(wav_data.wav[0]));
  memset(wav_data.wav, 0, WAVE_TOTAL_SIZE * sizeof(int16_t));
  M5_LOGD("FFT initialized.");

  // WiFi Connection
  M5_LOGD("Connecting to WiFi...");
  canvas->println("Connecting to WiFi...");
  canvas->pushSprite(0, 0);
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  // /*
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Try until connected
  while (WiFi.status() != WL_CONNECTED)
  {
    canvas->print(".");
    canvas->pushSprite(0, 0);
    M5.delay(1000);
  }
  M5_LOGD("Connected to WiFi.");
  canvas->println("\r\nConnected!");
  canvas->pushSprite(0, 0);
  M5.delay(2000);
  // */

  // Refresh Display
  canvas->clear();
  canvas->setCursor(0, 0);
  canvas->pushSprite(0, 0);

  // Display setup
  M5.Display.startWrite();

  int16_t w = M5.Display.width();
  int16_t h = M5.Display.height() >> 2;
  int16_t y = 0;

  rect_t rect_fft_peak = {0, y, w, h};
  y += h;
  rect_t rect_fft_drawer = {0, y, w, h};
  y += h;
  rect_t rect_fft_history = {0, y, w, h};
  rect_t rect_debug = {0, y, w, (int16_t)(h << 1)};
  rect_t rect_detect = {0, y, w, (int16_t)(h << 1)};
  y += h;
  rect_t rect_wav_drawer = {0, y, w, h};

  fft_function.setup(FFT_BITS);
  fft_peak.setup(&M5.Display, rect_fft_peak);
  fft_drawer.setup(&M5.Display, rect_fft_drawer);
  fft_history.setup(&M5.Display, rect_fft_history);
  wav_drawer.setup(&M5.Display, rect_wav_drawer);
  debug_drawer.setup(&M5.Display, rect_debug);
  detect_drawer.setup(&M5.Display, rect_detect);
  M5_LOGD("detect_drawer initialized.");

  M5.Display.setTextSize(w / 64.0f, h / 16.0f);
  M5.Display.setFont(&fonts::AsciiFont8x16);
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
}

void loop()
{
  static int step = -1;

  if (!M5.Mic.isEnabled())
  {
    M5_LOGE("Microphone is not available.");
    canvas->println("Microphone is not available.");
    canvas->pushSprite(0, 0);
    M5.delay(1000);
    return;
  }

  M5.update();
  if (M5.BtnA.wasPressed())
  {
    debug_drawer.resetCounter();
  }

  int wav_idx = wav_data.latest_index;
  while (M5.Mic.isRecording() < 2)
  {
    M5.Mic.record(&(wav_data.wav[wav_idx]), WAVE_BLOCK_SIZE, SAMPLE_RATE, false);
    wav_idx += WAVE_BLOCK_SIZE;
    if (wav_idx >= WAVE_TOTAL_SIZE)
    {
      wav_idx = 0;
    }
    wav_data.latest_index = wav_idx;
  };

  {
    switch (++step)
    {
    default:
      step = 0;
      M5.Display.display();
      fft_function.update(&fft_data);
      break;
    case 1:
      // wav_drawer.update(wav_data);
      break;
    case 2:
      fft_drawer.update(fft_data);
      break;
    case 3:
      // fft_history.update(fft_data);
      break;
    case 4:
      fft_peak.update(fft_data);
      // debug_drawer.update(fft_data);
      // debug_drawer.update(fft_data, INDEX_INTERCOM_2_NOTE_1);
      // debug_drawer.update(fft_peak);
      intercom1_detector.update(fft_peak);
      intercom2_detector.update(fft_peak);
      if (intercom1_detector.detected())
      {
        M5_LOGD("intercom 1 Detected!!!");
        // sendLineNotification("外のインターホンが鳴ったよ！");
      }
      if (intercom2_detector.detected())
      {
        M5_LOGD("intercom 2 Detected!!!");
        // sendLineNotification("玄関のインターホンが鳴ったよ！");
      }
      detect_drawer.update(fft_data, fft_peak);
      break;
    }
  }
}
