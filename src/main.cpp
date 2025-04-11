#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "Mic_FFT.hpp"

#define WIFI_SSID "<SSID>"
#define WIFI_PASS "<PASSWORD>"
#define LINE_CHANNEL_ACCESS_TOKEN "<TOKEN>"

#define DETECT_THRESHOLD 5000.0f

static M5GFX *display;
static M5Canvas *canvas;

const uint16_t INDEX_INTERCOM_1_NOTE_1 = 36; // 849 Hz (G#?)
const uint16_t INDEX_INTERCOM_1_NOTE_2 = 29; // 681 Hz (F)
const uint16_t INDEX_INTERCOM_2_NOTE_1 = 28; // 656 Hz (E)
const uint16_t INDEX_INTERCOM_2_NOTE_2 = 22; // 519 Hz (C)

class notifier_t
{
  bool line_enabled = true;
  int16_t line_failure_count = 0;

public:
  bool getLineEnabled() const
  {
    return line_enabled;
  }

  void toggleLineEnabled()
  {
    line_enabled = !line_enabled;
  }

  bool sendLineNotification(const char *message)
  {
    if (!line_enabled)
    {
      M5_LOGD("LINE notification is disabled.");
      return false;
    }

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
      line_failure_count++;
      if (line_failure_count > 3)
      {
        line_enabled = false;
        M5_LOGD("LINE notification disabled due to multiple failures.");
      }
      return false;
    }

    M5_LOGD("Notification sent successfully.");
    // */
    return true;
  }
};

class detector_t
{
public:
  const uint16_t sound1_index;
  const uint16_t sound1_duration_ms;
  const uint16_t sound2_index;
  const uint16_t sound2_duration_ms;
  const float threshold;

private:
  const unsigned long active_window_ms;

  unsigned long sound1_start_time = 0;
  unsigned long sound2_start_time = 0;
  unsigned long sound1_active_until = 0;
  unsigned long sound2_active_until = 0;
  bool sound1_active = false;
  bool sound2_active = false;

  const long cooldown_ms = 2000;
  unsigned long cooldown_until = 0;

public:
  detector_t(uint16_t sound1_index,
             unsigned long sound1_duration_ms,
             uint16_t sound2_index,
             unsigned long sound2_duration_ms,
             unsigned long active_window_ms,
             float threshold)
      : sound1_index(sound1_index),
        sound1_duration_ms(sound1_duration_ms),
        sound2_index(sound2_index),
        sound2_duration_ms(sound2_duration_ms),
        active_window_ms(active_window_ms),
        threshold(threshold)
  {
  }

  bool update(const fft_data_t &fft_data, const fft_peak_t &fft_peak)
  {
    unsigned long current_time = millis();

    // detecting
    if (detecting())
    {
      if (current_time > cooldown_until)
      {
        sound1_start_time = 0;
        sound1_active_until = 0;
        sound1_active = false;
        sound2_start_time = 0;
        sound2_active_until = 0;
        sound2_active = false;
        cooldown_until = 0;
        M5_LOGD("Resetting detection state.");
        return true;
      }
      else
      {
        return false;
      }
    }

    // check 1st sound
    if (fft_peak.isPeak(sound1_index) && fft_data.fdata[sound1_index] > threshold)
    {
      if (sound1_start_time == 0)
      {
        sound1_start_time = current_time;
      }
      else if (current_time - sound1_start_time >= sound1_duration_ms)
      {
        sound1_active = true;
      }
      sound1_active_until = current_time + active_window_ms;
    }
    else if (current_time - sound1_start_time > active_window_ms)
    {
      sound1_start_time = 0;
      sound1_active_until = 0;
      sound1_active = false;
    }

    // check 2nd sound
    if (fft_peak.isPeak(sound2_index) && fft_data.fdata[sound2_index] > threshold)
    {
      if (sound2_start_time == 0)
      {
        sound2_start_time = current_time;
      }
      else if (current_time - sound2_start_time >= sound2_duration_ms)
      {
        sound2_active = true;
      }
      sound2_active_until = current_time + active_window_ms;
    }
    else if (current_time - sound2_start_time > active_window_ms)
    {
      sound2_start_time = 0;
      sound2_active_until = 0;
      sound2_active = false;
    }

    return true;
  }

  // return true only once when both sounds are detected
  bool detected()
  {
    unsigned long current_time = millis();
    if (current_time < cooldown_until)
    {
      return false;
    }

    if (sound1_active && sound2_active)
    {
      cooldown_until = current_time + cooldown_ms;
      return true;
    }
    return false;
  }

  // (DO NOT use in main loop!)
  // retun true until cooldown time is over
  bool detecting() const
  {
    return sound1_active && sound2_active;
  }

  bool isSound1Active() const
  {
    return sound1_active;
  }
  bool isSound2Active() const
  {
    return sound2_active;
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
  rect_t rect_canvas = {0, 0, 0, 0};
  rect_t rect_indicator = {0, 0, 0, 0};
  rect_t rect_status_line = {0, 0, 0, 0};
  rect_t rect_status_wifi = {0, 0, 0, 0};
  rect_t rect_message = {0, 0, 0, 0};

  bool initialized = false;
  bool line_ok = false;
  bool wifi_ok = false;

  uint16_t bg_color = TFT_BLACK;
  uint16_t gh_color = TFT_NAVY;
  uint16_t gh_color2 = TFT_DARKCYAN;
  uint16_t fg_color = TFT_WHITE;
  uint16_t ok_color = TFT_DARKGREEN;
  uint16_t error_color = TFT_MAROON;
  uint16_t detected1_color = 0xc0b0u;
  uint16_t detected2_color = 0x8a22u;

  uint16_t inidicator_label_w = 0;

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
    rect_canvas = {0, 0, (int16_t)_canvas->width(), (int16_t)_canvas->height()};

    _c_indicator = new M5Canvas(_canvas);
    _c_status_line = new M5Canvas(_canvas);
    _c_status_wifi = new M5Canvas(_canvas);
    _c_message = new M5Canvas(_canvas);

    rect_indicator = {rect_canvas.x, rect_canvas.y, (int16_t)(rect_canvas.w * (2.0 / 3.0)), (int16_t)(rect_canvas.h / 2.0)};
    rect_status_line = {(int16_t)(rect_canvas.x + rect_indicator.w), rect_canvas.y, (int16_t)(rect_canvas.w * (1.0 / 3.0)), (int16_t)(rect_canvas.h / 4.0)};
    rect_status_wifi = {(int16_t)(rect_canvas.x + rect_indicator.w), (int16_t)(rect_canvas.y + rect_status_line.h), (int16_t)(rect_canvas.w * (1.0 / 3.0)), (int16_t)(rect_canvas.h / 4.0)};
    rect_message = {rect_canvas.x, (int16_t)(rect_canvas.y + rect_indicator.h), rect_canvas.w, (int16_t)(rect_canvas.h / 2.0)};

    _c_indicator->createSprite(rect_indicator.w, rect_indicator.h);
    _c_status_line->createSprite(rect_status_line.w, rect_status_line.h);
    _c_status_wifi->createSprite(rect_status_wifi.w, rect_status_wifi.h);
    _c_message->createSprite(rect_message.w, rect_message.h);

    // init indicator
    _c_indicator->fillRect(0, 0, rect_indicator.w, rect_indicator.h, gh_color);
    const float indicator_text_size = 1.4f;
    M5_LOGD("indicator_text_size: %f", indicator_text_size);
    _c_indicator->setTextSize(indicator_text_size, indicator_text_size);
    _c_indicator->setTextColor(fg_color);
    _c_indicator->setTextDatum(ML_DATUM);
    auto x = rect_indicator.x + 2;
    auto y = rect_indicator.y + rect_indicator.h / 4.0f / 2.0f;
    _c_indicator->drawString("A", x, y);
    _c_indicator->drawString("F", x, y += rect_indicator.h / 4.0f);
    _c_indicator->drawString("E", x, y += rect_indicator.h / 4.0f);
    _c_indicator->drawString("C", x, y += rect_indicator.h / 4.0f);
    inidicator_label_w = x + _c_indicator->textWidth("A") + 4;

    // init status
    const float status_text_size = _c_status_line->getTextSizeX() * (rect_status_line.w - 8) / (float)std::max(_c_status_line->textWidth("LINE"), _c_status_wifi->textWidth("WiFi"));
    M5_LOGD("status_text_size: %f", status_text_size);
    _c_status_line->setTextSize(status_text_size);
    _c_status_wifi->setTextSize(status_text_size);
    updateLineStatus(true);
    updateWifiStatus();

    // init message
    _c_message->fillRect(0, 0, rect_message.w, rect_message.h, bg_color);
    const float message_text_size = _c_message->getTextSizeX() * (rect_message.w - 16) / (float)_c_message->textWidth("Listening...");
    M5_LOGD("message_text_size: %f", message_text_size);
    _c_message->setTextSize(message_text_size, message_text_size * 1.4f);

    // push to display
    _c_indicator->pushSprite(rect_indicator.x, rect_indicator.y);
    _c_status_line->pushSprite(rect_status_line.x, rect_status_line.y);
    _c_status_wifi->pushSprite(rect_status_wifi.x, rect_status_wifi.y);
    _c_message->pushSprite(rect_message.x, rect_message.y);
    _canvas->pushSprite(draw_rect.x, draw_rect.y);

    initialized = true;
    return true;
  }

  bool update(const fft_data_t &fft_data, const fft_peak_t &fft_peak, const detector_t &detector1, const detector_t &detector2, const notifier_t &notifier)
  {
    if (_gfx == nullptr)
    {
      return false;
    }

    // update indicator
    updateIndicator(fft_data, fft_peak, detector1, detector2);
    // update status
    updateLineStatus(notifier);
    updateWifiStatus();
    // update message
    updateMessage(detector1, detector2);
    // push to display
    _canvas->pushSprite(draw_rect.x, draw_rect.y);

    return true;
  }

private:
  bool updateIndicator(const fft_data_t &fft_data, const fft_peak_t &fft_peak, const detector_t &detector1, const detector_t &detector2)
  {
    _c_indicator->fillRect(inidicator_label_w, 0, rect_indicator.w - inidicator_label_w, rect_indicator.h, gh_color);
    const int32_t height_per_sound = rect_indicator.h / 4.0f;
    if (detector1.isSound1Active()) { _c_indicator->fillRect(inidicator_label_w, height_per_sound * 0, rect_indicator.w - inidicator_label_w, height_per_sound, gh_color2);}
    if (detector1.isSound2Active()) { _c_indicator->fillRect(inidicator_label_w, height_per_sound * 1, rect_indicator.w - inidicator_label_w, height_per_sound, gh_color2);}
    if (detector2.isSound1Active()) { _c_indicator->fillRect(inidicator_label_w, height_per_sound * 2, rect_indicator.w - inidicator_label_w, height_per_sound, gh_color2);}
    if (detector2.isSound2Active()) { _c_indicator->fillRect(inidicator_label_w, height_per_sound * 3, rect_indicator.w - inidicator_label_w, height_per_sound, gh_color2);}

    const int16_t indicator_max_width = rect_indicator.w - inidicator_label_w - 4;
    if (fft_peak.isPeak(detector1.sound1_index)) { M5_LOGD("level of detector1 sound1: %f", fft_data.fdata[detector1.sound1_index]); }
    if (fft_peak.isPeak(detector1.sound2_index)) { M5_LOGD("level of detector1 sound2: %f", fft_data.fdata[detector1.sound2_index]); }
    if (fft_peak.isPeak(detector2.sound1_index)) { M5_LOGD("level of detector2 sound1: %f", fft_data.fdata[detector2.sound1_index]); }
    if (fft_peak.isPeak(detector2.sound2_index)) { M5_LOGD("level of detector2 sound2: %f", fft_data.fdata[detector2.sound2_index]); }
    
    _c_indicator->drawFastVLine(inidicator_label_w + detector1.threshold / 65536.0f * indicator_max_width, 0, rect_indicator.h / 2.0f, TFT_RED);
    _c_indicator->drawFastVLine(inidicator_label_w + detector2.threshold / 65536.0f * indicator_max_width, rect_indicator.h / 2.0f, rect_indicator.h / 2.0f, TFT_RED);
    for (int i = -1; i <= 1; ++i)
    {
      auto y = rect_indicator.y + rect_indicator.h / 4.0f / 2.0f + i;
      _c_indicator->drawFastHLine(inidicator_label_w, y, (int32_t)(fft_data.fdata[detector1.sound1_index] / 65536.0f * indicator_max_width), TFT_GREEN);
      _c_indicator->drawFastHLine(inidicator_label_w, y += rect_indicator.h / 4.0f, (int32_t)(fft_data.fdata[detector1.sound2_index] / 65536.0f * indicator_max_width), TFT_GREEN);
      _c_indicator->drawFastHLine(inidicator_label_w, y += rect_indicator.h / 4.0f, (int32_t)(fft_data.fdata[detector2.sound1_index] / 65536.0f * indicator_max_width), TFT_GREEN);
      _c_indicator->drawFastHLine(inidicator_label_w, y += rect_indicator.h / 4.0f, (int32_t)(fft_data.fdata[detector2.sound2_index] / 65536.0f * indicator_max_width), TFT_GREEN);
    }
    _c_indicator->pushSprite(rect_indicator.x, rect_indicator.y);

    return true;
  }

  bool updateLineStatus(bool status)
  {
    // check current status
    if (initialized && status == line_ok)
    {
      return false;
    }

    // status changed or not initialized
    line_ok = status;
    if (line_ok)
    {
      _c_status_line->fillRect(0, 0, _c_status_line->width(), _c_status_line->height(), ok_color);
    }
    else
    {
      _c_status_line->fillRect(0, 0, _c_status_line->width(), _c_status_line->height(), error_color);
    }
    _c_status_line->setTextColor(fg_color);
    _c_status_line->setTextDatum(MC_DATUM);
    _c_status_line->drawString("LINE", rect_status_line.w / 2 + 2, rect_status_line.h / 2);
    _c_status_line->pushSprite(rect_status_line.x, rect_status_line.y);

    return true;
  }

  bool updateLineStatus(const notifier_t &notifier)
  {
    return updateLineStatus(notifier.getLineEnabled());
  }

  bool updateWifiStatus()
  {
    // check current status
    bool wifi_now = (WiFi.status() == WL_CONNECTED);
    if (initialized && wifi_now == wifi_ok)
    {
      return false;
    }

    // status changed or not initialized
    wifi_ok = wifi_now;
    if (wifi_ok)
    {
      _c_status_wifi->fillRect(0, 0, _c_status_wifi->width(), _c_status_wifi->height(), ok_color);
    }
    else
    {
      _c_status_wifi->fillRect(0, 0, _c_status_wifi->width(), _c_status_wifi->height(), error_color);
    }
    _c_status_wifi->setTextColor(fg_color);
    _c_status_wifi->setTextDatum(MC_DATUM);
    _c_status_wifi->drawString("WiFi", rect_status_wifi.w / 2 + 2, rect_status_wifi.h / 2);
    _c_status_wifi->pushSprite(rect_status_wifi.x, rect_status_wifi.y);
    _canvas->pushSprite(draw_rect.x, draw_rect.y);

    return true;
  }

  bool updateMessage(const detector_t detector1, const detector_t detector2)
  {
    _c_message->clear();
    if (!M5.Mic.isEnabled())
    {
      _c_message->fillRect(0, 0, rect_message.w, rect_message.h, error_color);
      _c_message->setTextColor(fg_color);
      _c_message->setTextDatum(MC_DATUM);
      _c_message->drawString("Mic error!", rect_message.w / 2 + 2, rect_message.h / 2);
    }
    else if (detector1.detecting())
    {
      _c_message->fillRect(0, 0, rect_message.w, rect_message.h, detected1_color);
      _c_message->setTextColor(fg_color);
      _c_message->setTextDatum(MC_DATUM);
      _c_message->drawString("#1 Detected!", rect_message.w / 2 + 2, rect_message.h / 2);
    }
    else if (detector2.detecting())
    {
      _c_message->fillRect(0, 0, rect_message.w, rect_message.h, detected2_color);
      _c_message->setTextColor(fg_color);
      _c_message->setTextDatum(MC_DATUM);
      _c_message->drawString("#2 Detected!", rect_message.w / 2 + 2, rect_message.h / 2);
    }
    else
    {
      _c_message->fillRect(0, 0, rect_message.w, rect_message.h, bg_color);
      _c_message->setTextColor(fg_color);
      _c_message->setTextDatum(MC_DATUM);
      _c_message->drawString("Listening...", rect_message.w / 2 + 2, rect_message.h / 2);
    }
    _c_message->pushSprite(rect_message.x, rect_message.y);

    return true;
  }
};

static fft_function_t fft_function;
static fft_data_t fft_data;
static wav_data_t wav_data;
static wav_drawer_t wav_drawer;
static fft_drawer_t fft_drawer;
static fft_peak_t fft_peak;
static fft_history_t fft_history;
static notifier_t notifier;
static detector_t intercom1_detector(INDEX_INTERCOM_1_NOTE_1, 500.0f, INDEX_INTERCOM_1_NOTE_2, 700.0f, 2000.0f, DETECT_THRESHOLD);
static detector_t intercom2_detector(INDEX_INTERCOM_2_NOTE_1, 500.0f, INDEX_INTERCOM_2_NOTE_2, 700.0f, 2000.0f, DETECT_THRESHOLD);
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
  if (M5.BtnB.wasPressed())
  {
    notifier.toggleLineEnabled();
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
      fft_drawer.update(fft_data);
      break;
    case 2:
      fft_peak.update(fft_data);
      break;
    case 3:
      // fft_history.update(fft_data);
      intercom1_detector.update(fft_data, fft_peak);
      intercom2_detector.update(fft_data, fft_peak);
      break;
    case 4:
      detect_drawer.update(fft_data, fft_peak, intercom1_detector, intercom2_detector, notifier);
      if (intercom1_detector.detected())
      {
        M5_LOGD("intercom 1 Detected!!!");
        notifier.sendLineNotification("外のインターホンが鳴ったよ！");
      }
      if (intercom2_detector.detected())
      {
        M5_LOGD("intercom 2 Detected!!!");
        notifier.sendLineNotification("玄関のインターホンが鳴ったよ！");
      }
      break;
    }
  }
}
