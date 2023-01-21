#pragma once
#include <memory>
#include <optional>
#include <thread>
#include <signal.h>
#include "st7920.h"
#include "temp_sensor.h"
#include "relay.h"
#include <nlohmann/json.hpp>
#include <list>
#include <civetweb.h>

class App {
public:
    static int run();
    static void init(int argc, char **argv);
    static void cleanup();

private:
    App();
    ~App();

    static void handleSignal(int signal, siginfo_t *info, void *ucontext);

    ST7920 lcd;

    bool runLoop;

    std::optional<float> coolTarget;
    std::optional<float> coolMin;
    std::optional<float> heatTarget;
    std::optional<float> heatMax;

    std::shared_ptr<TempSensor> fermenter;
    std::shared_ptr<TempSensor> ambient;

    std::shared_ptr<Relay> freezer;
    std::shared_ptr<Relay> heater;

    std::shared_ptr<std::thread> serverThread;

    struct mg_context *ctx;

    std::list<struct mg_connection *> websocketConnections;

    void updateTime();
    void updateSensors();
    void updateRelays();

    void setupWebServer();

    nlohmann::json buildStatusData();

    static int handleStatusRequest(struct mg_connection *c, void *data);
    static int handleSetRequest(struct mg_connection *c, void *data);
    static int handleClearRequest(struct mg_connection *c, void *data);

    static int handleWebsocketConnected(const struct mg_connection *c, void *data);
    static void handleWebsocketReady(struct mg_connection *c, void *data);
    static int handleWebsocketData(struct mg_connection *c, int bits, char *data, size_t len, void *cbdata);
    static void handleWebsocketClosed(const struct mg_connection *c, void *data);

    void saveConfig();
    void loadConfig();

    int _run();
};