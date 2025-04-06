#include "Mic_FFT.hpp"

const uint16_t INDEX_INTERCOM_1_NOTE_1 = 36; // 849 Hz (G#?)
const uint16_t INDEX_INTERCOM_1_NOTE_2 = 29; // 681 Hz (F)
const uint16_t INDEX_INTERCOM_2_NOTE_1 = 28; // 656 Hz (E)
const uint16_t INDEX_INTERCOM_2_NOTE_2 = 22; // 519 Hz (C)

class debug_drawer_t
{
public:
    bool setup(LGFX_Device *gfx, const rect_t &rect);
    bool update(const fft_data_t &fft_data);
    bool update(const fft_data_t &fft_data, const int16_t target_index);
    bool update(fft_peak_t &fft_peak);
    bool resetCounter(void);
    bool isPeak(const uint16_t &index);
private:
    LGFX_Device *_gfx = nullptr;
    M5Canvas *_canvas = nullptr;
    rect_t draw_rect = {0, 0, 0, 0};
    uint32_t bg_color = 0x000000u;
    uint32_t fg_color = 0xFFFFFFu;
    uint16_t step = 0;

    uint16_t count_intercom1_note1 = 0;
    uint16_t count_intercom1_note2 = 0;
    uint16_t count_intercom2_note1 = 0;
    uint16_t count_intercom2_note2 = 0;
};
