
#include "web_ui.h"

#include "debug.h"
#include "helpers.h"
#include "ota_html.h"

#ifndef GIT_TAG
#define GIT_TAG "unknown"
#endif

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
    json += "\",\"sleepInterval\":";
    json += config->getSleepIntervalS();
    json += ",\"alertV\":";
    json += config->getAlertThresholdV();
    json += ",\"recoverV\":";
    json += config->getRecoverThresholdV();
    json += ",\"version\":\"";
    json += GIT_TAG;
    json += "\"}";
    server->send(200, "application/json", json);
  });

  server->on("/device", HTTP_POST, [this]() {
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

  server->on("/settings", HTTP_POST, [this]() {
    String sleepArg = server->arg("sleepInterval");
    if (sleepArg.length() > 0) {
      config->setSleepIntervalS(sleepArg.toInt());
    }

    String alertArg = server->arg("alertV");
    String recoverArg = server->arg("recoverV");
    if (alertArg.length() > 0 && recoverArg.length() > 0) {
      int32_t alertV = alertArg.toInt();
      int32_t recoverV = recoverArg.toInt();
      if (alertV < 0 || alertV > 65535 || recoverV < 0 || recoverV > 65535) {
        server->send(400, "text/plain", "Threshold must be 0-65535");
        return;
      }
      config->setAlertThresholds(alertV, recoverV);
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
