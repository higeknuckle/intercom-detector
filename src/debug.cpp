#include "debug.hpp"

// debug_drawer_t class definitions
bool debug_drawer_t::setup(LGFX_Device *gfx, const rect_t &rect)
{
  if (gfx == nullptr)
  {
    return false;
  }
  _gfx = gfx;
  draw_rect = rect;

  _canvas = new M5Canvas(gfx);
  _canvas->createSprite(rect.w, rect.h);
  _canvas->fillRect(0, 0, rect.w, rect.h, bg_color);
  _canvas->setTextColor(fg_color);
  _canvas->setTextSize(2.0f);
  _canvas->setTextWrap(false);
  _canvas->pushSprite(rect.x, rect.y);
  return true;
}

bool debug_drawer_t::update(const fft_data_t &fft_data)
{
  if (_gfx == nullptr)
  {
    return false;
  }
  if (++step >= 16)
  {
    step = 0;
    _canvas->clear();
    _canvas->setCursor(0, 0);
    _canvas->printf("849: %.2f\n", fft_data.fdata[INDEX_INTERCOM_1_NOTE_1]);
    _canvas->printf("681: %.2f\n", fft_data.fdata[INDEX_INTERCOM_1_NOTE_2]);
    _canvas->printf("656: %.2f\n", fft_data.fdata[INDEX_INTERCOM_2_NOTE_1]);
    _canvas->printf("519: %.2f\n", fft_data.fdata[INDEX_INTERCOM_2_NOTE_2]);
    _canvas->pushSprite(draw_rect.x, draw_rect.y);
  }
  return true;
}

bool debug_drawer_t::update(const fft_data_t &fft_data, const int16_t target_index)
{
  if (_gfx == nullptr)
  {
    return false;
  }
  if (++step >= 16)
  {
    step = 0;
    _canvas->clear();
    _canvas->setCursor(0, 0);
    for (int i = -2; i <= 2; ++i)
    {
      _canvas->printf("%2d:%8.2f\n", i, fft_data.fdata[target_index + i]);
    }
    _canvas->pushSprite(draw_rect.x, draw_rect.y);
  }
  return true;
}

bool debug_drawer_t::update(fft_peak_t &fft_peak)
{
  if (_gfx == nullptr)
  {
    return false;
  }

  _canvas->clear();
  _canvas->setCursor(0, 0);

  if (fft_peak.isPeak(INDEX_INTERCOM_1_NOTE_1))
  {
    count_intercom1_note1++;
  }
  else if (fft_peak.isPeak(INDEX_INTERCOM_1_NOTE_2))
  {
    count_intercom1_note2++;
  }
  else if (fft_peak.isPeak(INDEX_INTERCOM_2_NOTE_1))
  {
    count_intercom2_note1++;
  }
  else if (fft_peak.isPeak(INDEX_INTERCOM_2_NOTE_2))
  {
    count_intercom2_note2++;
  }

  _canvas->printf("%3d: %d\n", INDEX_INTERCOM_1_NOTE_1, count_intercom1_note1);
  _canvas->printf("%3d: %d\n", INDEX_INTERCOM_1_NOTE_2, count_intercom1_note2);
  _canvas->printf("%3d: %d\n", INDEX_INTERCOM_2_NOTE_1, count_intercom2_note1);
  _canvas->printf("%3d: %d\n", INDEX_INTERCOM_2_NOTE_2, count_intercom2_note2);
  _canvas->pushSprite(draw_rect.x, draw_rect.y);
  return true;
}

bool debug_drawer_t::resetCounter(void)
{
  count_intercom1_note1 = 0;
  count_intercom1_note2 = 0;
  count_intercom2_note1 = 0;
  count_intercom2_note2 = 0;

  return true;
}
