#pragma once
#include <cinttypes>
#include <linux/gpio.h>

class Relay {
public:
    Relay(uint8_t gpioChip, uint8_t gpioPin, bool activeLow);
    ~Relay();

    void turnOn();
    void turnOff();
    bool isOn();
    void set(bool on);

private:
    struct gpio_v2_line_request gpioReq;

    uint8_t gpioChip;
    uint8_t gpioPin;
    bool activeLow;

};