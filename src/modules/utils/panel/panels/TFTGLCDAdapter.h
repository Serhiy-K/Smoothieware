/*
 * TFTGLCDAdapter.cpp
 *
 *  Created on: 25-06-2019
 *      Author: Serhiy-K
 */

#ifndef TFTGLCDAdapter_H_
#define TFTGLCDAdapter_H_

#include "LcdBase.h"
#include "mbed.h"
#include "libs/Pin.h"

class TFTGLCDAdapter: public LcdBase {

public:
    TFTGLCDAdapter();
    virtual ~TFTGLCDAdapter();
    void init();
    void home();
    void clear();
    void display(){}; //nothing
    void setCursor(uint8_t col, uint8_t row);
    void write(const char* line, int len);
    bool encoderReturnsDelta() { return true; };
    void set_fan_percent(uint16_t percent) { fan_percent = percent; };
    void set_has_fan(bool present){ has_fan = present; };

    void on_refresh(bool now=false);

    uint8_t readButtons();
    int readEncoderDelta();

    int getEncoderResolution() { return 2; };
    uint16_t get_screen_lines() { return text_lines; };    // return real number of panel screen lines
    bool hasGraphics() { return true; }
    bool hasFullGraphics()  { return false; };

    // blit a glyph of w pixels wide and h pixels high to x, y. offset pixel position in glyph by x_offset, y_offset.
    // span is the width in bytes of the src bitmap
    // The glyph bytes will be 8 bits of X pixels, msbit->lsbit from top left to bottom right
    void bltGlyph(int x, int y, int w, int h, const uint8_t *glyph, int span= 0, int x_offset=0, int y_offset=0);
    void setLed(int led, bool onoff);

    uint8_t getContrast() { return contrast; };
    void setContrast(uint8_t c);

    void buzz(long duration, uint16_t freq);

private:
    // this is a C++ way to do something on entry of a class and something else on exit of scope
    void detect_panel();
    void send_pic(const unsigned char* data);   //send text buffer to screen

    //buffer
    unsigned char *framebuffer;

    mbed::SPI* spi;
    Pin cs;
    Pin buzz_pin;

    uint16_t text_lines;    //minimum 10
    uint8_t chars_per_line; //minimum 20
    uint16_t fbsize;
    uint8_t tx, ty;         // text cursor position
    uint8_t picBits;
    uint8_t ledBits;
    uint8_t contrast;
    uint8_t gliph_update_cnt;
    uint8_t panel_present = 0;
    uint8_t refresh_counts = 0;
    bool has_fan = 0;
    uint16_t fan_percent = 0;
};

#endif /* TFTGLCDAdapter_H_ */
