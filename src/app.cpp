#include "app.h"
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <string>
#include <ctime>
#include <functional>
#include <cstdio>
#include <filesystem>
#include <fstream>

static App *app = nullptr;

static uint8_t logo[] = {
    0b00010100, 
    0b00111111, 
    0b00111111, 
    0b00111011, 
    0b00100011, 
    0b11100001, 
    0b10100001, 
    0b10100001, 
    0b10100001, 
    0b11100001, 
    0b00100001, 
    0b00111111, 
};

static uint8_t arrow[] = {
    0b11000000,
    0b01100000,
    0b00110000,
    0b00011000,
    0b00001100,
    0b00011000,
    0b00110000,
    0b01100000,
    0b11000000
};

#define VALUE_OR_NULL(v) v.has_value() ? nlohmann::json(v.value()) : nlohmann::json()

void App::handleSignal(int signal, siginfo_t *info, void *ucontext) {
    std::string sigName = signal==SIGTERM ? "SIGTERM" : "SIGINT";
    spdlog::warn("Caught {}, shutting down", sigName);
    app->runLoop = false;
}

int App::run() {
    app->runLoop = true;
    return app->_run();
}

void App::init(int argc, char **argv) {
    if (app!=nullptr) return;

    
    spdlog::info("===============================");
    spdlog::info("      Brewserver Startup");
    spdlog::info("-------------------------------");
    app = new App();
}

void App::cleanup() {
    if (app==nullptr) return;
    delete app;
    
    spdlog::info("-------------------------------");
    spdlog::info("    Brewserver Shutdown");
    spdlog::info("==============================="); 
    app = nullptr;
}

App::App()
:lcd(0, 0) {
    spdlog::info("Setting up lcd...");
    this->lcd.setFunctionSet(false, false);
    this->lcd.setDisplayControl(true, false, false);
    this->lcd.setFunctionSet(true, true);
    this->lcd.setFontHeight(10);

    this->lcd.drawAll(); // clear

    this->lcd.putString(2, 26, "Brewserver Loading...");
    this->lcd.drawAll();

    struct sigaction sa{};
    sa.sa_sigaction = &App::handleSignal;
    sa.sa_flags = SA_SIGINFO;

    spdlog::info("Setting signal handlers...");
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    spdlog::info("Starting up temp sensors...");

    this->fermenter.reset(new TempSensor("28-0517602ef2ff"));
    this->ambient.reset(new TempSensor("28-0517609e1fff"));

    this->heater.reset(new Relay(0,23,true));
    this->freezer.reset(new Relay(0,24,true));

    this->loadConfig();
}

void App::saveConfig() {
    nlohmann::json config = {
        { "coolTarget", VALUE_OR_NULL(this->coolTarget) },
        { "coolMin",    VALUE_OR_NULL(this->coolMin) },
        { "heatTarget", VALUE_OR_NULL(this->heatTarget) },
        { "heatMax",    VALUE_OR_NULL(this->heatMax) }
    };

    const char *home = getenv("HOME");
    std::filesystem::path configPath(home);
    configPath /= ".brewserver.json";

    spdlog::info("Writing config to {}", configPath.string());

    std::string configStr = config.dump(1);
    std::ofstream configFile(configPath, std::ios::out);
    configFile << configStr;
    configFile.close();
}

void App::loadConfig() {
    const char *home = getenv("HOME");
    std::filesystem::path configPath(home);
    configPath /= ".brewserver.json";

    if (std::filesystem::exists(configPath)) {
        spdlog::info("Loading config from {}", configPath.string());
        
        std::ifstream confFile(configPath, std::ios::in);
        nlohmann::json config = nlohmann::json::parse(confFile);
        confFile.close();

        if (config.contains("coolTarget")) {
            if (config["coolTarget"].is_number()) {
                this->coolTarget = config["coolTarget"].get<float>();
            } else {
                spdlog::warn("config value 'coolTarget' is wrong type, expected number.");
            }

            if (config["coolMin"].is_number()) {
                this->coolMin = config["coolMin"].get<float>();
            } else {
                spdlog::warn("config value 'coolMin' is wrong type, expected number.");
            }

            if (config["heatTarget"].is_number()) {
                this->heatTarget = config["heatTarget"].get<float>();
            } else {
                spdlog::warn("config value 'heatTarget' is wrong type, expected number.");
            }

            if (config["heatMax"].is_number()) {
                this->heatMax = config["heatMax"].get<float>();
            } else {
                spdlog::warn("config value 'heatMax' is wrong type, expected number.");
            }
        }
    }
}

App::~App() {
    // clear lcd
    this->lcd.setRegion(0, 0, 127, 63, false);
    this->lcd.drawAll();   
}

int App::_run() {

    this->setupWebServer();

    // clear lcd
    this->lcd.setRegion(0, 0, 127, 63, false);
    this->lcd.drawAll(); 

    this->lcd.putBitmap(0, 0, 8, logo, 12);
    time_t lastWSSent = 0;
    while(this->runLoop) {
        
        std::optional<float> ferm = this->fermenter->getTempF();
        std::optional<float> amb = this->ambient->getTempF();
        if (this->coolTarget.has_value() && ferm.has_value() && amb.has_value()) {
            bool coolTurnOn = false;

            if (ferm > this->coolTarget.value()) {
                coolTurnOn = true;
                if (ferm.value() < this->coolTarget.value() + 1 && !this->freezer->isOn()) coolTurnOn = false;
            }

            if (this->coolMin.has_value()) {
                if (amb.value() <= this->coolMin.value()) {
                    coolTurnOn = false;
                }
            }

            if (!this->freezer->isOn() && coolTurnOn) {
                spdlog::info("Turning freezer on");
                this->freezer->turnOn();
            } else if (this->freezer->isOn() && !coolTurnOn) {
                spdlog::info("Turning freezer off");
                this->freezer->turnOff();
            }
        }   

        if (this->heatTarget.has_value() && ferm.has_value() && amb.has_value()) {
            bool heatTurnOn = false;

            if (ferm.value() < this->heatTarget.value()) {
                heatTurnOn = true;
                if (ferm > this->heatTarget.value() - 1 && !this->heater->isOn()) heatTurnOn = false;
            }

            if (this->heatMax.has_value()) {
                if (amb.value() >= this->heatMax.value()) {
                    heatTurnOn = false;
                }
            }

            if (!this->heater->isOn() && heatTurnOn) {
                spdlog::info("Turning heater on");
                this->heater->turnOn();
            } else if (this->heater->isOn() && !heatTurnOn) {
                spdlog::info("Turning heater off");
                this->heater->turnOff();
            }
        }

        this->updateTime();
        this->updateSensors();
        this->updateRelays();
        this->lcd.drawAll();

        /* send websocket data each second*/
        time_t now = time(NULL);
        bool didSend = false;
        if (now-lastWSSent>=1) {
            didSend = false;
            nlohmann::json status;
            status["status"] = this->buildStatusData();
            std::string statusStr = status.dump();

            for (struct mg_connection *c : this->websocketConnections) {
                mg_websocket_write(c, MG_WEBSOCKET_OPCODE_TEXT, statusStr.c_str(), statusStr.size());
                didSend = true;
            }
            if (didSend) lastWSSent = now;
        }

        usleep(100 * 1000);
    }

    spdlog::info("Stopping webserver");
    mg_stop(ctx);
    mg_exit_library();

    return 0;
}

void App::updateSensors() {
    std::optional<float> f = this->fermenter->getTempF();
    char tempStr[128];

    if (!f.has_value()) {
        sprintf(tempStr, "Fermenter:{err}");
    } else {
        sprintf(tempStr, "Fermenter:%3.1f\xb0", f.value());
    }
    this->lcd.setRegion(2,12, 128, 24, false);
    this->lcd.putString(2,12, tempStr);

    f = this->ambient->getTempF();
    if (!f.has_value()) {
        sprintf(tempStr, "Ambient  :{err}");
    } else {
        sprintf(tempStr, "Ambient  :%3.1f\xb0", f.value());
    }
    this->lcd.setRegion(2,24, 128, 36, false);
    this->lcd.putString(2,24, tempStr);
}

void App::updateTime() {
    time_t now;
    now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timeStr[26];

    strftime(timeStr, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    this->lcd.setRegion(10, 0, 128, 12, false);
    this->lcd.putString(10, 0, timeStr);
}

void App::updateRelays() {
    bool on = this->freezer->isOn();

    char relayStr[128];

    if (this->coolTarget.has_value()) {
        sprintf(relayStr, "Cooling:%s [%3.1f\xb0]", on ? "ON " : "OFF", this->coolTarget.value());
    } else {
        sprintf(relayStr, "Cooling:%s [NONE ]", on ? "ON " : "OFF");
    }
    this->lcd.setRegion(2,36, 128, 48, false);
    this->lcd.putString(2,36, relayStr);

    on = this->heater->isOn();
    if (this->heatTarget.has_value()) {
        sprintf(relayStr, "Heating:%s [%3.1f\xb0]", on ? "ON " : "OFF", this->heatTarget.value());
    } else {
        sprintf(relayStr, "Heating:%s [NONE ]", on ? "ON " : "OFF");
    }
    this->lcd.setRegion(2,48, 128, 60, false);
    this->lcd.putString(2,48, relayStr);
}

nlohmann::json App::buildStatusData() {
    return {
        { "temperature", {
            {"fermenter", VALUE_OR_NULL(this->fermenter->getTempF())},
            {"ambient", VALUE_OR_NULL(this->ambient->getTempF())}
        }},
        { "thermostat", {
            {"coolTargetTemp", VALUE_OR_NULL(this->coolTarget)},
            {"coolMinTemp", VALUE_OR_NULL(this->coolMin)},
            {"heatTargetTemp", VALUE_OR_NULL(this->heatTarget)},
            {"heatMaxTemp", VALUE_OR_NULL(this->heatMax)}
        }},
        { "relay", {
            {"cooling", this->freezer->isOn()},
            {"heating", this->heater->isOn()}
        }}
    };
}

static std::string remoteAddressStr(const struct mg_connection *c) {
    const char *remoteAddr = mg_get_header(c, "X-Real-IP");
    const struct mg_request_info *req = mg_get_request_info(c);

    if (remoteAddr==NULL) remoteAddr = req->remote_addr;

    return std::string(remoteAddr);
}

static void endRequest(const struct mg_connection *c, int replyStatus) {
    if (replyStatus<0) return;
    const struct mg_request_info *req = mg_get_request_info(c);

    if (replyStatus>=400 && replyStatus <700) {
        spdlog::warn("{} {} {} -> {}", remoteAddressStr(c), req->request_method, req->local_uri, replyStatus);
    } else {
        spdlog::info("{} {} {} -> {}", remoteAddressStr(c), req->request_method, req->local_uri, replyStatus);
    }
}

int App::handleClearRequest(struct mg_connection *c, void *data) {
    App *app = (App*)data;

    const struct mg_request_info *req = mg_get_request_info(c);
    if (std::string(req->request_method)!="POST") {
        mg_printf(c, "HTTP/1.1 405 Method Not Allowed\r\n");
        mg_printf(c, "Content-Type: text/plain\r\n");
        mg_printf(c, "Connection: close\r\n");
        mg_printf(c, "\r\n");
        mg_printf(c, "405: Method Not Allowed");

        return 405;
    }

    struct mg_match_context mcx;

    if (mg_match("/clear/?*", req->local_uri, &mcx)==-1 || mcx.num_matches!=1) {
        mg_printf(c, "HTTP/1.1 400 Bad Request\r\n");
        mg_printf(c, "Content-Type: text/plain\r\n");
        mg_printf(c, "Connection: close\r\n");
        mg_printf(c, "\r\n");
        mg_printf(c, "400: Bad Request");

        return 400;
    }

    std::string var(mcx.match[0].str, mcx.match[0].len);

    if (var=="coolTarget") {
        spdlog::info("Clearing coolTarget (from {})", remoteAddressStr(c));
        app->coolTarget.reset();
    } else if (var=="coolMin") {
        spdlog::info("Clearing coolMin (from {})", remoteAddressStr(c));
        app->coolMin.reset();
    } else if (var=="heatTarget") {
        spdlog::info("Clearing heatTarget (from {})", remoteAddressStr(c));
        app->heatTarget.reset();
    } else if (var=="heatMax") {
        spdlog::info("Clearing heatMax (from {})", remoteAddressStr(c));
        app->heatMax.reset();
    } else {
        mg_printf(c, "HTTP/1.1 400 Bad Request\r\n");
        mg_printf(c, "Content-Type: text/plain\r\n");
        mg_printf(c, "Connection: close\r\n");
        mg_printf(c, "\r\n");
        mg_printf(c, "400: Bad Request");
        return 400;
    }

    mg_printf(c, "HTTP/1.1 204 No Content\r\n");
    mg_printf(c, "Connection: close\r\n");
    mg_printf(c, "\r\n");
    app->saveConfig();
    return 204;
}

int App::handleSetRequest(struct mg_connection *c, void *data) {
    App *app = (App*)data;

    const struct mg_request_info *req = mg_get_request_info(c);
    if (std::string(req->request_method)!="POST") {
        mg_printf(c, "HTTP/1.1 405 Method Not Allowed\r\n");
        mg_printf(c, "Content-Type: text/plain\r\n");
        mg_printf(c, "Connection: close\r\n");
        mg_printf(c, "\r\n");
        mg_printf(c, "405: Method Not Allowed");

        return 405;
    }
    struct mg_match_context mcx;

    if (mg_match("/set/?*/?*", req->local_uri, &mcx)==-1 || mcx.num_matches!=2) {
        mg_printf(c, "HTTP/1.1 400 Bad Request\r\n");
        mg_printf(c, "Content-Type: text/plain\r\n");
        mg_printf(c, "Connection: close\r\n");
        mg_printf(c, "\r\n");
        mg_printf(c, "400: Bad Request");

        return 400;
    }

    std::string var(mcx.match[0].str, mcx.match[0].len);
    std::string valStr(mcx.match[1].str, mcx.match[1].len);
    float val = std::atof(valStr.c_str());

    if (var=="coolTarget") {
        spdlog::info("Setting coolTarget to {} (from {})", val, remoteAddressStr(c));
        app->coolTarget = val;
    } else if (var=="coolMin") {
        spdlog::info("Setting coolMin to {} (from {})", val, remoteAddressStr(c));
        app->coolMin = val;
    } else if (var=="heatTarget") {
        spdlog::info("Setting heatTarget to {} (from {})", val, remoteAddressStr(c));
        app->heatTarget = val;
    } else if (var=="heatMax") {
        spdlog::info("Setting heatMax to {} (from {})", val, remoteAddressStr(c));
        app->heatMax = val;
    } else {
        mg_printf(c, "HTTP/1.1 400 Bad Request\r\n");
        mg_printf(c, "Content-Type: text/plain\r\n");
        mg_printf(c, "Connection: close\r\n");
        mg_printf(c, "\r\n");
        mg_printf(c, "400: Bad Request");
        return 400;
    }

    mg_printf(c, "HTTP/1.1 204 No Content\r\n");
    mg_printf(c, "Connection: close\r\n");
    mg_printf(c, "\r\n");
    app->saveConfig();
    return 204;
}

int App::handleStatusRequest(struct mg_connection *c, void *data) {
    App *app = (App*)data;
    nlohmann::json status = app->buildStatusData();
    std::string statusStr = status.dump();

    mg_printf(c, "HTTP/1.1 200 OK\r\n");
    mg_printf(c, "Content-Type: application/json\r\n");
    mg_printf(c, "Connection: close\r\n");
    mg_printf(c, "\r\n");
    mg_printf(c, "%s", statusStr.c_str());

    return 200;
}

void App::setupWebServer() {
    spdlog::info("Starting web server");
    mg_init_library(MG_FEATURES_WEBSOCKET);

    const char *opts[] = {
        "listening_ports", "127.0.0.1:8000",
        "num_threads", "10",
        "enable_websocket_ping_pong", "yes",
        NULL, NULL
    };

    struct mg_callbacks cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.end_request = endRequest;


    this->ctx = mg_start(&cbs, (void*)this, opts);

    mg_set_websocket_handler(this->ctx, "/websocket", &App::handleWebsocketConnected, &App::handleWebsocketReady, &App::handleWebsocketData, &App::handleWebsocketClosed, (void*)this);
    mg_set_request_handler(this->ctx, "/status$", &App::handleStatusRequest, (void*)this);
    mg_set_request_handler(this->ctx, "/set/*/*$", &App::handleSetRequest, (void*)this);
    mg_set_request_handler(this->ctx, "/clear/*$", &App::handleClearRequest, (void*)this);    
}

int App::handleWebsocketConnected(const struct mg_connection *c, void *data) {
    return 0;
}

void App::handleWebsocketReady(struct mg_connection *c, void *data) {
    App *app = (App*)data;
    app->websocketConnections.push_back(c);
    spdlog::info("{} connected to websocket", remoteAddressStr(c));
}

int App::handleWebsocketData(struct mg_connection *c, int bits, char *data, size_t len, void *cbdata) {
    return 1;
}

void App::handleWebsocketClosed(const struct mg_connection *c, void *data) {
    App *app = (App*)data;
    app->websocketConnections.remove((struct mg_connection *)c);
    spdlog::info("{} disconnected from websocket", remoteAddressStr(c));
}