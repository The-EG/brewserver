#pragma once

#include <memory>
#include <thread>
#include <ctime>
#include <string>
#include <optional>

class TempSensor {
public:
    TempSensor(std::string id);
    ~TempSensor();

    std::optional<float> getTempC();
    std::optional<float> getTempF();

    time_t getTempTime();

private:
    //std::string tempPath;
    int fd;
    std::string id;

    std::optional<float> lastTemp;
    time_t lastTempTime;

    bool runPoll;

    std::shared_ptr<std::thread> pollThread;

    void runPolling();
};