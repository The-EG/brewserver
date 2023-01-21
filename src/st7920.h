#pragma once
#include <cstdint>
#include <vector>
#include <string>

#include <ft2build.h>
#include FT_FREETYPE_H

class ST7920 {
public:
    ST7920(uint8_t bus, uint8_t dev);
    ~ST7920();

    void setFunctionSet(bool extended, bool graphicDisplay);
    void setDisplayControl(bool displayOn, bool cursorOn, bool charBlinkOn);
    void setShiftControl(bool shift, bool right);
    void setEntryMode(bool increase, bool shift);
    void setGDRAMAddress(uint8_t row, uint8_t col);

    void send(uint8_t rs, uint8_t rw, std::vector<uint8_t> data);

    void setPixel(uint8_t x, uint8_t y, bool on);
    void setRegion(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, bool on);

    void putChar(uint8_t x, uint8_t y, unsigned long c);
    void putString(uint8_t x, uint8_t y, std::string str);

    void putBitmap(uint8_t x, uint8_t y, uint8_t width, uint8_t *data, size_t len);

    void drawSectionsForBB(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
    void drawSection(uint8_t r, uint8_t c);
    void drawRow(uint8_t r);
    void drawAll();

    void setFontHeight(uint8_t height);

private:
    int fd;

    FT_Library ftLib;
    FT_Face fontFace;

    uint8_t fontHeight;

    uint16_t pixels[32][16];
};