#include "st7920.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <cinttypes>
#include <cstdio>
#include <cmath>
#include <string>

#define SPEED_MHZ 1.5

//#define FONT_PATH "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"
#define FONT_PATH "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
//#define FONT_PATH "/usr/share/fonts/opentype/terminus/terminus-normal.otb"

ST7920::ST7920(uint8_t bus, uint8_t dev) {
    std::string path = "/sev/spidev" + std::to_string(bus) + "." + std::to_string(dev);
    this->fd = open("/dev/spidev0.0", O_WRONLY);
    uint8_t mode = SPI_MODE_0;
    if(ioctl(this->fd, SPI_IOC_WR_MODE, &mode) ) {
        printf("couldn't set mode, ");
        printf("%d: %s\n", errno, strerror(errno));
        throw "Couldn't set spi mode: (" + std::to_string(errno) + ") " + strerror(errno);
    }
    uint32_t speed   =  SPEED_MHZ * 1000 * 1000;
    if(ioctl(this->fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed)) {
        printf("could set speed\n");
        throw "Couldn't set spi speed: (" + std::to_string(errno) + ") " + strerror(errno);
    }

    memset(&this->pixels, 0x0, sizeof(this->pixels));

    if (FT_Init_FreeType(&this->ftLib)) {
        throw "Couldn't initialize Freetype";
    }

    if (FT_New_Face(this->ftLib, FONT_PATH, 0, &this->fontFace)) {
        throw "Couldn't load font";
    }

    this->setFontHeight(8);
}

ST7920::~ST7920() {
    close(this->fd);
    FT_Done_Face(this->fontFace);
    FT_Done_FreeType(this->ftLib);
}

void ST7920::setFontHeight(uint8_t height) {
    this->fontHeight = height;
    if (FT_Set_Pixel_Sizes(this->fontFace, 0, height)) {
        throw "Couldn't set font size";
    }
}

void ST7920::setFunctionSet(bool extended, bool graphicDisplay) {
    uint8_t val = 0b00110000;
    if (extended) {
        val |= 0b00000100;
        if (graphicDisplay) {
            val |= 0b00000010;
        }
    }
    std::vector<uint8_t> data({ val });
    this->send(0, 0, data);
    usleep(75);
}

void ST7920::setDisplayControl(bool displayOn, bool cursorOn, bool charBlinkOn) {
    uint8_t val           = 0b00001000;
    if (displayOn)   val |= 0b00000100;
    if (cursorOn)    val |= 0b00000010;
    if (charBlinkOn) val |= 0b00000001;
    std::vector<uint8_t> data({ val });
    this->send(0, 0, data);
    usleep(75);
}

void ST7920::setShiftControl(bool shift, bool right) {
    uint8_t val = 0b00010000;
    if (shift) val |= 0b00001000;
    if (right) val |= 0b00000100;
    std::vector<uint8_t> data{val};
    this->send(0, 0, data);
    usleep(75);
}

void ST7920::setEntryMode(bool increase, bool shift) {
    uint8_t       val  = 0b00000100;
    if (increase) val |= 0b00000010;
    if (shift)    val |= 0b00000001;
    std::vector<uint8_t> data{val};
    this->send(0, 0, data);
    usleep(75);
}

void ST7920::setGDRAMAddress(uint8_t row, uint8_t col) {
    uint8_t rowVal = 0b10000000 | (row & 0b00111111);
    uint8_t colVal = 0b10000000 | (col & 0b00001111);

    std::vector<uint8_t> d{ rowVal, colVal };
    this->send(0, 0, d);
    usleep(75);
}

void ST7920::send(uint8_t rs, uint8_t rw, std::vector<uint8_t> data) {
    std::vector<uint8_t> sendData;
    sendData.push_back(0b11111000 | ((rw&0x01)<<2) | ((rs&0x01)<<1));
    for(uint8_t byte : data) {
        sendData.push_back(byte & 0xf0);
        sendData.push_back((byte & 0x0f) << 4);
    }

    write(this->fd, sendData.data(), sendData.size());
}

void ST7920::setPixel(uint8_t x, uint8_t y, bool on) {
    if (x>=128 || y>=64) return;

    uint8_t r = y % 32;
    uint8_t c = x / 16 + (y >= 32 ? 8 : 0);
    uint8_t b = x % 16;

    if (on) {
        this->pixels[r][c] |= 0x0001 << (15-b);
    } else {
        this->pixels[r][c] &= ~(0x0001 << (15-b));
    }
}

void ST7920::setRegion(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, bool on) {
    for (uint8_t y=y1;y<=y2;y++) {
        for (uint8_t x=x1;x<=x2;x++) {
            this->setPixel(x,y,on);
        }
    }
}

void ST7920::drawSectionsForBB(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
    for (int r=y1;r<=y2;r++) {
        for (int c=x1 / 8;c<=x2 / 8;c++) {
            this->drawSection(r, c);
        }
    }
}

void ST7920::drawSection(uint8_t r, uint8_t c) {
    uint8_t actualR = r % 32;
    uint8_t actualC = c + (r >= 32 ? 8 : 0);

    this->setGDRAMAddress(actualR, actualC);
    uint16_t sec = this->pixels[actualR][actualC];
    uint8_t secH = (sec & 0xFF00) >> 8;
    uint8_t secL = sec & 0x00ff;
    std::vector<uint8_t> data{secH, secL};
    this->send(1, 0, data);
}

void ST7920::drawRow(uint8_t r) {
    uint8_t actualR = r % 32;
    uint8_t c = r >= 32 ? 8 : 0;
    std::vector<uint8_t> data;
    for(int i=0;i<8;i++) {
        uint16_t sec = this->pixels[actualR][c+i];
        uint8_t secH = (sec & 0xFF00) >> 8;
        uint8_t secL = sec & 0x00ff;
        data.push_back(secH);
        data.push_back(secL);
    }
    this->setGDRAMAddress(actualR, c);
    this->send(1, 0, data);
}

void ST7920::drawAll() {
    for (int i=0;i<64;i++) this->drawRow(i);
}

void ST7920::putChar(uint8_t x, uint8_t y, unsigned long c) {
    if (FT_Load_Char(this->fontFace, c, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO | FT_LOAD_MONOCHROME)) {
        throw "couldn't load glyph";
    }

    FT_Bitmap bitmap = this->fontFace->glyph->bitmap;
    int rows = bitmap.rows;
    int width = bitmap.width;
    int pitch = bitmap.pitch;

    int baselineY = (this->fontFace->size->metrics.ascender / 64) + y;
    int xOffset = this->fontFace->glyph->bitmap_left;
    int topY = baselineY - this->fontFace->glyph->bitmap_top;    

    for (int r=0 ; r < rows ; r++) {
        for (int xa=0 ; xa < width; xa++) {
            int xb = xa / 8;
            uint8_t *b = bitmap.buffer + (r * pitch) + xb;
            uint8_t v = (*b >> (7 - (xa % 8))) & 0x01;
            if(v) this->setPixel(x + xa + xOffset, topY + r, v );
        }
    }
}

void ST7920::putString(uint8_t x, uint8_t y, std::string str) {
    for (char c : str) {
        this->putChar(x, y, c);
        x += std::ceil(this->fontFace->glyph->advance.x / 64);
        if (x>=128) return;
    }
}

void ST7920::putBitmap(uint8_t x, uint8_t y, uint8_t width, uint8_t *data, size_t len) {
    uint8_t cx = 0;
    uint8_t cy = 0;

    for (size_t i=0;i<len;i++) {
        uint8_t b = data[i];
        for (int8_t bi=0;bi<8;bi++) {
            uint8_t v = (b >> (7-bi)) & 0x01;
            this->setPixel(x + cx, y + cy, v);
            cx++;
            if (cx >= width) {
                cy++;
                cx = 0;
            }
        }
    }   
}