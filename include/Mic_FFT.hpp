#pragma once

// External displays can be enabled if necessary
// #include <M5ModuleDisplay.h>
// #include <M5AtomDisplay.h>
// #include <M5UnitGLASS2.h>
// #include <M5UnitOLED.h>
// #include <M5UnitLCD.h>

#include <M5Unified.h>
#include <set>
#include <map>

// The higher the sample rate, the higher the frequency results obtained by FFT.
// If limited to the audible range, 24kHz to 48kHz is sufficient.
static constexpr const size_t SAMPLE_RATE = 24000;
// static constexpr const size_t SAMPLE_RATE = 96000;

// The larger the FFT_BITS, the higher the accuracy of the FFT, but the processing load also increases
static constexpr const size_t FFT_BITS = 10;

// WAVE_BLOCK_SIZE is the size of one processing when capturing data from I2S.
// If it is too large, the loop cycle will be slow and the frequency of drawing updates will decrease.
// If it is too small, recoding interruptions will occur.
// For example, if the sample rate is 96kHz and the block size is 384, data will be captured every 4ms.
// Therefore, the drawing process, FFT process, and other loop iterations must be completed within 4ms.
static constexpr const size_t WAVE_BLOCK_SIZE = 256;

static constexpr const size_t FFT_SIZE = 1u << FFT_BITS;
static constexpr const size_t WAVE_BLOCK_COUNT = 3 + FFT_SIZE / WAVE_BLOCK_SIZE;
static constexpr const size_t WAVE_TOTAL_SIZE = WAVE_BLOCK_SIZE * WAVE_BLOCK_COUNT;

void *memory_alloc(size_t size);
void memory_free(void *ptr);

struct wav_data_t
{
    int16_t *wav = nullptr;
    size_t length = 0;
    size_t latest_index = 0;

    size_t searchEdge(size_t offset, size_t search_length) const;
};

struct fft_data_t
{
    float *fdata = nullptr;
    size_t length = 0;
    size_t sample_rate;

    wav_data_t *wav_data = nullptr;
    uint8_t fft_size_bits = 0;

    float getDataByPixel(uint16_t x, uint16_t width) const;
};

struct rect_t
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

class fft_function_t
{
public:
    bool setup(uint8_t max_fft_size_bits);
    void close(void);
    bool update(fft_data_t *fft_data);

private:
    bool _init(uint8_t size_bits);
    float *fi;
    float *fr;
    void *_work_area = nullptr;
    uint8_t _max_fft_size_bits = 0;
    uint8_t _initialized_fft_size_bits = 0;
};

class wav_drawer_t
{
public:
    bool setup(LGFX_Device *gfx, const rect_t &rect);
    bool update(const wav_data_t &wav_data);

private:
    LGFX_Device *_gfx = nullptr;
    int16_t *prev_y = nullptr;
    int16_t *prev_h = nullptr;
    rect_t draw_rect = {0, 0, 0, 0};
    uint32_t bg_color = 0x000000u;
    uint32_t fg_color = 0xFFFFFFu;
    uint32_t line_color = 0x303030u;
};

class fft_drawer_t
{
public:
    bool setup(LGFX_Device *gfx, const rect_t &rect);
    bool update(const fft_data_t &fft_data);

private:
    LGFX_Device *_gfx = nullptr;
    uint16_t *prev_y = nullptr;
    uint16_t *prev_h = nullptr;
    rect_t draw_rect = {0, 0, 0, 0};
    uint32_t bg_color = 0x000066u;
    uint32_t fg_color = 0x00FF00u;
};

class fft_peak_t
{
public:
    bool setup(LGFX_Device *gfx, const rect_t &rect);
    bool update(const fft_data_t &fft_data);
    bool isPeak(const uint16_t &index) const;

private:
    LGFX_Device *_gfx = nullptr;
    rect_t draw_rect = {0, 0, 0, 0};
    uint32_t bg_color = 0x000000u;
    uint32_t fg_color = 0x00FFFFu;
    std::set<uint16_t> peak_index_set;
    uint16_t prev_peak_index = UINT16_MAX;
    char text_buf[10] = {
        0,
    };
    char prev_text[10] = {
        0,
    };
    uint8_t step = 0;
};

class fft_history_t
{
public:
    bool setup(LGFX_Device *gfx, const rect_t &rect);
    bool update(const fft_data_t &fft_data);

private:
    LGFX_Device *_gfx = nullptr;
    uint8_t *color_map = nullptr;
    rect_t draw_rect = {0, 0, 0, 0};
    uint32_t bg_color = 0x000033u;
    uint32_t fg_color = 0xFFFF00u;
    int step = 0;
};

