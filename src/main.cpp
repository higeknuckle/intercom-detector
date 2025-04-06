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
  y += h;
  rect_t rect_wav_drawer = {0, y, w, h};

  fft_function.setup(FFT_BITS);
  fft_peak.setup(&M5.Display, rect_fft_peak);
  fft_drawer.setup(&M5.Display, rect_fft_drawer);
  fft_history.setup(&M5.Display, rect_fft_history);
  wav_drawer.setup(&M5.Display, rect_wav_drawer);
  debug_drawer.setup(&M5.Display, rect_debug);

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
      // debug_drawer.update(fft_data, INDEX_INTERCOM_1_NOTE_1);
      debug_drawer.update(fft_peak);
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
      break;
    }
  }
}
