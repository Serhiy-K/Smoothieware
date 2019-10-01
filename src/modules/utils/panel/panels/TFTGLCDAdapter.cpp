/*
* TFTGLCDAdapter.cpp
*
* TFTGLCDAdapter is external adapter based on microcontroller.
* TFTGLCDAdapter may use color TFT LCD with different chips and different resolutions.
* Courently it built on STM32F103C8T6 "Blue Pill" board and may use color TFT LCDs based on
* ILI9325 and ILI9341 with resolution 320x240 and ILI9327 with resolution 400x240.
* If adapter use font dimension 16x24 and LCD has resolution 320x240 then screen has text
* resolution 20x10 and with LCD resolution 400x240 text resolution will be 25x10.
* TFTGLCDAdapter uses text screen buffer insted off graphical buffer for other panels.
* TFTGLCDAdapter has own encoder and may have up to 6 buttons (include encoder button).
*
* For use TFTGLCDAdapter you need set "panel.enable" parameter in config file to "true",
* change "panel.lcd" parameter to "tft_glcd_adapter" and set proper parameters for SPI bus.
*
* Hardware and firmware sources for TFTGLCDAdapter: https://github.com/Serhiy-K/TFTGLCDAdapter.git
*
*  Created on: 25-06-2019
*      Author: Serhiy-K
*/

#include "TFTGLCDAdapter.h"

#include "Kernel.h"
#include "platform_memory.h"
#include "Config.h"
#include "checksumm.h"
#include "StreamOutputPool.h"
#include "ConfigValue.h"
#include "utils.h"

#define panel_checksum             CHECKSUM("panel")
#define spi_channel_checksum       CHECKSUM("spi_channel")
#define spi_cs_pin_checksum        CHECKSUM("spi_cs_pin")
#define spi_frequency_checksum     CHECKSUM("spi_frequency")
#define buzz_pin_checksum          CHECKSUM("buzz_pin")
#define contrast_checksum          CHECKSUM("contrast")

enum Commands {
    GET_SPI_DATA = 0,
    READ_BUTTONS,       // read buttons
    READ_ENCODER,       // read encoder
    LCD_WRITE,          // write to LCD
    BUZZER,             // beep buzzer
    CONTRAST,           // set contrast
    // Other commands... 0xE0 thru 0xFF
    GET_LCD_ROW = 0xE0, // read LCD rows number from adapter
    GET_LCD_COL,        // read LCD columns number from adapter
    CLEAR_BUFFER,       // for Marlin
    REDRAW,             // for Marlin
    INIT_ADAPTER = 0xFE,// Initialize
};

#define LED_MASK    0x0f
#define PIC_MASK    0x3f

TFTGLCDAdapter::TFTGLCDAdapter() {
    // select which SPI channel to use
    int spi_channel = THEKERNEL->config->value(panel_checksum, spi_channel_checksum)->by_default(0)->as_number();
    PinName mosi, miso, sclk;
    if      (spi_channel == 0) { mosi = P0_18; miso = P0_17; sclk = P0_15;}
    else if (spi_channel == 1) { mosi = P0_9;  miso = P0_8;  sclk = P0_7;}
    else                       { mosi = P0_18; miso = P0_17; sclk = P0_15;}

    this->spi = new mbed::SPI(mosi, miso, sclk);
    this->spi->frequency(THEKERNEL->config->value( panel_checksum, spi_frequency_checksum)->by_default(1000000)->as_number()); //1Mhz freq
    this->cs.from_string(THEKERNEL->config->value( panel_checksum, spi_cs_pin_checksum)->by_default("nc")->as_string())->as_output();

    cs.set(1);

    this->buzz_pin.from_string(THEKERNEL->config->value( panel_checksum, buzz_pin_checksum)->by_default("nc")->as_string())->as_output();

    // contrast override
    contrast = THEKERNEL->config->value(panel_checksum, contrast_checksum)->by_default(180)->as_number();

    detect_panel();

    if (panel_present)
    {
        framebuffer = (uint8_t *)AHB0.alloc(fbsize); // grab some memory from USB_RAM
        if (framebuffer == NULL)
            THEKERNEL->streams->printf("Not enough memory available for frame buffer");
    }
    else
        THEKERNEL->streams->printf("TFT GLCD Adapter not connected");
}

TFTGLCDAdapter::~TFTGLCDAdapter() {
    this->cs.set(1);
    delete this->spi;
    if (framebuffer)
        AHB0.dealloc(framebuffer);
}
//get screen resolution from adapter and calculate framebuffer size
void TFTGLCDAdapter::detect_panel() {
    this->cs.set(0);
    this->spi->write(GET_LCD_ROW);
    text_lines = this->spi->write(GET_SPI_DATA);
    this->cs.set(1);
    if ((text_lines < 10) || (text_lines > 20)) { //not real number
        text_lines = 0;
        return;
    }
    this->cs.set(0);
    this->spi->write(GET_LCD_COL);
    chars_per_line = (uint16_t)this->spi->write(GET_SPI_DATA);
    this->cs.set(1);
    fbsize = chars_per_line * text_lines + 2;
    panel_present = 1;  //screen resolution >= 20x10
}
//clearing screen
void TFTGLCDAdapter::clear() {
    if (!panel_present) return;
    memset(framebuffer, ' ', fbsize - 2);
    framebuffer[fbsize - 2] = framebuffer[fbsize - 1] = 0;
    tx = ty = picBits = gliph_update_cnt = 0;
}
//set new text cursor position
void TFTGLCDAdapter::setCursor(uint8_t col, uint8_t row) {
    tx = col;
    ty = row;
}
// set text cursor to uper left corner
void TFTGLCDAdapter::home() {
    tx = ty = 0;
}
//Init adapter
void TFTGLCDAdapter::init() {
    if (!panel_present) return;
    this->cs.set(0);
    this->spi->write(INIT_ADAPTER);
    this->spi->write(0);    //protocol = Smoothie
    wait_us(10);
    this->cs.set(1);
    // give adapter time to init
    safe_delay_ms(100);
}
//send text line to buffer
void TFTGLCDAdapter::write(const char *line, int len) {
    uint8_t pos;
    if (!panel_present) return;
    pos = tx + ty * chars_per_line;
    for (int i = 0; i < len; ++i) framebuffer[pos++] = line[i];
}
//send all screen and flags for icons and leds
void TFTGLCDAdapter::send_pic(const unsigned char *fbstart) {
    framebuffer[fbsize - 2] = picBits & PIC_MASK;
    framebuffer[fbsize - 1] = ledBits & LED_MASK;
    if (gliph_update_cnt) gliph_update_cnt--;
    else                  picBits = 0;
    if ((framebuffer[20] == 'X') && (this->has_fan == true))  //main screen, fan present
        framebuffer[chars_per_line * 4] = (uint8_t)fan_percent;
    //send framebuffer to adapter
    this->cs.set(0);
    this->spi->write(LCD_WRITE);
    for (int x = 0; x < fbsize; x++) {
        this->spi->write(*(fbstart++));
    }
    wait_us(10);
    this->cs.set(1);
}
//refreshing screen with 10Hz refresh rate
void TFTGLCDAdapter::on_refresh(bool now) {
    if (!panel_present) return;
    refresh_counts++;
    if (now || (refresh_counts == 2)) {
        send_pic(framebuffer);
        refresh_counts = 0;
    }
}
//set flags for icons
void TFTGLCDAdapter::bltGlyph(int x, int y, int w, int h, const uint8_t *glyph, int span, int x_offset, int y_offset) {
    if (w == 80)
        picBits = 0x01;    //draw logo
    else {
        // Update Only every 20 refreshes
        gliph_update_cnt = 20;
        switch (x) {
            case 0:   picBits |= 0x02; break; //draw hotend_on1
            case 27:  picBits |= 0x04; break; //draw hotend_on2
            case 55:  picBits |= 0x08; break; //draw hotend_on3
            case 83:  picBits |= 0x10; break; //draw bed_on
            case 111: picBits |= 0x20; break; //draw fan_state
        }
    }
}
// Sets flags for leds
void TFTGLCDAdapter::setLed(int led, bool onoff) {
    if(onoff) {
        switch(led) {
            case LED_HOTEND_ON: ledBits |= 1; break; // on
            case LED_BED_ON:    ledBits |= 2; break; // on
            case LED_FAN_ON:    ledBits |= 4; break; // on
            case LED_HOT:       ledBits |= 8; break; // on
        }
    } else {
        switch(led) {
            case LED_HOTEND_ON: ledBits &= ~1; break; // off
            case LED_BED_ON:    ledBits &= ~2; break; // off
            case LED_FAN_ON:    ledBits &= ~4; break; // off
            case LED_HOT:       ledBits &= ~8; break; // off
        }
    }
}
// cycle the buzzer pin at a certain frequency (hz) for a certain duration (ms)
void TFTGLCDAdapter::buzz(long duration, uint16_t freq) {
    if (this->buzz_pin.connected()) { //buzzer on Smoothie main board
        duration *= 1000;             //convert from ms to us
        long period = 1000000 / freq; // period in us
        long elapsed_time = 0;
        while (elapsed_time < duration) {
            this->buzz_pin.set(1);
            wait_us(period / 2);
            this->buzz_pin.set(0);
            wait_us(period / 2);
            elapsed_time += (period);
        }
    } else if (panel_present) {    //buzzer on GLCD controller board
        this->cs.set(0);
        this->spi->write(BUZZER);
        this->spi->write((uint16_t)duration >> 8);
        this->spi->write(duration);
        this->spi->write(freq >> 8);
        this->spi->write(freq);
        safe_delay_us(10);
        this->cs.set(1);
    }
}
//reading button state
uint8_t TFTGLCDAdapter::readButtons(void) {
    if (!panel_present) return 0;
    this->cs.set(0);
    this->spi->write(READ_BUTTONS);
    safe_delay_us(10);
    uint8_t b = this->spi->write(GET_SPI_DATA);
    safe_delay_us(10);
    this->cs.set(1);
    return b;
}

int TFTGLCDAdapter::readEncoderDelta() {
    if (!panel_present) return 0;
    this->cs.set(0);
    this->spi->write(READ_ENCODER);
    safe_delay_us(10);
    int8_t e = this->spi->write(GET_SPI_DATA);
    safe_delay_us(10);
    this->cs.set(1);
    int d = (int16_t)e;
    return d;
}

void TFTGLCDAdapter::setContrast(uint8_t c) {
    contrast = c;
    if (!panel_present) return;
    this->cs.set(0);
    this->spi->write(CONTRAST);
    safe_delay_us(10);
    this->spi->write(c);
    safe_delay_us(10);
    this->cs.set(1);
}

