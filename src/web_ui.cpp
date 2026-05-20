
#include "web_ui.h"

#include "debug.h"
#include "helpers.h"
#include "ota_html.h"

WebUi::WebUi(Config* config) : config(config), running(false) {}

void WebUi::start() {
  if (running) {
    LOGLN("WebUi already active");
    return;
  }
  running = true;
  char ssid[32] = "FenceRanger-";
  bytesToHex(config->getDevEUI(), DEVEUI_LEN, ssid + 12);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, "wildinternational");

  server = new WebServer(80);

  server->on("/", HTTP_GET, [this]() { server->send(200, "text/html", OTA_HTML); });

  server->on("/api/config", HTTP_GET, [this]() {
    char eui[DEVEUI_LEN * 2 + 1];
    bytesToHex(config->getDevEUI(), DEVEUI_LEN, eui);

    char appkey[APPKEY_LEN * 2 + 1] = "";
    if (config->hasAppKey()) {
      bytesToHex(config->getAppKey(), APPKEY_LEN, appkey);
    }

    String json = "{\"eui\":\"";
    json += eui;
    json += "\",\"name\":\"";
    json += config->getDevName();
    json += "\",\"appkey\":\"";
    json += appkey;
    json += "\"}";
    server->send(200, "application/json", json);
  });

  server->on("/config", HTTP_POST, [this]() {
    config->setDevName(server->arg("name").c_str());

    String hexKey = server->arg("appkey");
    if (hexKey.length() == APPKEY_LEN * 2) {
      uint8_t key[APPKEY_LEN];
      if (!hexToBytes(hexKey.c_str(), key, APPKEY_LEN)) {
        server->send(400, "text/plain", "Invalid app key");
        return;
      }
      config->setAppKey(key);
    } else if (hexKey.length() == 0) {
      config->clearAppKey();
    } else {
      server->send(400, "text/plain", "App key must be 32 hex chars");
      return;
    }

    server->send(200, "text/plain", "Saved");
  });

  server->on(
      "/update", HTTP_POST,
      [this]() {
        server->send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
        delay(500);
        ESP.restart();
      },
      [this]() {
        HTTPUpload& upload = server->upload();
        if (upload.status == UPLOAD_FILE_START) {
          Update.begin(UPDATE_SIZE_UNKNOWN);
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          Update.write(upload.buf, upload.currentSize);
        } else if (upload.status == UPLOAD_FILE_END) {
          Update.end(true);
        }
      });

  server->begin();
}

void WebUi::stop() {
  if (!running) {
    LOGLN("WebUi not running");
    return;
  }
  running = false;
  server->stop();
  delete server;
  server = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void WebUi::update() {
  if (!running) {
    LOGLN("WebUi not running");
    return;
  }
  server->handleClient();
}
