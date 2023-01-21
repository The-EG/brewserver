#include "relay.h"
#include <spdlog/spdlog.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string>

Relay::Relay(uint8_t gpioChip, uint8_t gpioPin, bool activeLow)
:activeLow(activeLow), gpioChip(gpioChip), gpioPin(gpioPin) {
    spdlog::info("Setting up relay on GPIO {}.{}", this->gpioChip, this->gpioPin);

    std::string gpioChipPath = "/dev/gpiochip" + std::to_string(this->gpioChip);

    int fd = open(gpioChipPath.c_str(), O_RDONLY);

    if (fd==-1) {
        spdlog::error("Couldn't open gpio chip {}: ({}) {}", this->gpioChip, errno, strerror(errno));
        throw "Couldn't open GPIO chip";
    }

    this->gpioReq.offsets[0] = this->gpioPin;
    this->gpioReq.num_lines = 1;
    this->gpioReq.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    if (this->activeLow) this->gpioReq.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;

    int r = ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &this->gpioReq);
    if (r==-1) {
        spdlog::error("Couldn't request gpio pin {}: ({}) {}", this->gpioPin, errno, strerror(errno));
        throw "Couldn't request GPIO pin";
    }

    close(fd);
}   

Relay::~Relay() {
    spdlog::info("Releasing GPIO {}.{}", this->gpioChip, this->gpioPin);
    close(this->gpioReq.fd);
}

void Relay::turnOn() {
    this->set(true);
}

void Relay::turnOff() {
    this->set(false);
}

bool Relay::isOn() {
    struct gpio_v2_line_values val{};

    val.mask = 1;

    int r = ioctl(this->gpioReq.fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &val);

    if (r==-1) {
        spdlog::error("Couldn't get value for GPIO {}.{}", this->gpioChip, this->gpioPin);
        throw "Coudn't get value for GPIO";
    }

    return val.bits == 1;
}

void Relay::set(bool on) {
    struct gpio_v2_line_values val;
    val.mask = 1;
    val.bits = on ? 1 : 0;

    int r = ioctl(this->gpioReq.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &val);

    if (r==-1) {
        spdlog::error("Couldn't set value for GPIO {}.{}", this->gpioChip, this->gpioPin);
        throw "Coudn't set value for GPIO";
    }
}