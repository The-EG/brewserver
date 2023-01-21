#include "temp_sensor.h"
#include <fcntl.h>
#include <unistd.h>
#include <functional>
#include <cstdlib>
#include <spdlog/spdlog.h>

TempSensor::TempSensor(std::string id) {
    this->id = id;
    spdlog::info("New temp sensor for {}", id);
    std::string path = "/sys/bus/w1/devices/"+id+"/temperature";
    this->fd = open(path.c_str(), O_RDONLY);

    this->runPoll = true;

    this->pollThread.reset(new std::thread(std::bind(&TempSensor::runPolling, this)));
}

TempSensor::~TempSensor() {
    this->runPoll = false;
    this->pollThread->join();
    close(this->fd);
}

std::optional<float> TempSensor::getTempC() {
    return this->lastTemp;
}

std::optional<float> TempSensor::getTempF() {
    if (this->lastTemp.has_value()) {
        return std::optional<float>(this->lastTemp.value() * 1.8f + 32.f);
    } else {
        return std::optional<float>();
    }
}

time_t TempSensor::getTempTime() {
    time_t v;
    v = this->lastTempTime;
    return v;
}

void TempSensor::runPolling() {
    char tempBuf[8];

    spdlog::info("Starting polling for temp sensor {}", this->id);
    while(this->runPoll) {
        lseek(this->fd, 0, SEEK_SET);
        size_t l = read(this->fd, tempBuf, 8);

        this->lastTemp = std::strtof(tempBuf, 0) / 1000.f;
        this->lastTempTime = time(NULL);
        
        usleep(500 * 1000);
    }
    spdlog::info("Ending polling for temp sensor {}", this->id);
}