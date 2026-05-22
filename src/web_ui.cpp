
#include "web_ui.h"

#include "logging.h"
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
    json += ",\"burstSends\":";
    json += config->getAlertBurstSends();
    json += ",\"idleHbCycles\":";
    json += config->getIdleHeartbeatCycles();
    json += ",\"alertHbCycles\":";
    json += config->getAlertHeartbeatCycles();
    json += ",\"recoverCycles\":";
    json += config->getRecoveryConfirmCycles();
    json += ",\"battFullMv\":";
    json += config->getBattFullMv();
    json += ",\"battLowMv\":";
    json += config->getBattLowMv();
    json += ",\"battSleepMult\":";
    json += config->getBattSleepMult();
    json += ",\"battRecoveryN\":";
    json += config->getBattRecoveryN();
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

    String burstArg = server->arg("burstSends");
    String idleHbArg = server->arg("idleHbCycles");
    String alertHbArg = server->arg("alertHbCycles");
    String recoverCyclesArg = server->arg("recoverCycles");
    if (burstArg.length() == 0 || idleHbArg.length() == 0 || alertHbArg.length() == 0 ||
        recoverCyclesArg.length() == 0) {
      server->send(400, "text/plain", "Cycle values must not be empty");
      return;
    }
    int32_t burst = burstArg.toInt();
    int32_t idleHb = idleHbArg.toInt();
    int32_t alertHb = alertHbArg.toInt();
    int32_t recoverCycles = recoverCyclesArg.toInt();
    if (burst < 1 || burst > 255 || idleHb < 1 || idleHb > 255 || alertHb < 1 || alertHb > 255 ||
        recoverCycles < 1 || recoverCycles > 255) {
      server->send(400, "text/plain", "Cycle values must be 1-255");
      return;
    }
    config->setAlertCycles(burst, idleHb, alertHb, recoverCycles);

    server->send(200, "text/plain", "Saved");
  });

  server->on("/batt-sleep", HTTP_POST, [this]() {
    String battFullArg = server->arg("battFullMv");
    String battLowArg = server->arg("battLowMv");
    String battMultArg = server->arg("battSleepMult");
    String battNArg = server->arg("battRecoveryN");
    if (battFullArg.length() == 0 || battLowArg.length() == 0 || battMultArg.length() == 0 ||
        battNArg.length() == 0) {
      server->send(400, "text/plain", "All fields required");
      return;
    }
    int32_t fullMv = battFullArg.toInt();
    int32_t lowMv = battLowArg.toInt();
    int32_t sleepMult = battMultArg.toInt();
    int32_t recoveryN = battNArg.toInt();
    if (fullMv < 1 || fullMv > 65535 || lowMv < 1 || lowMv > 65535 || sleepMult < 1 ||
        sleepMult > 255 || recoveryN < 1 || recoveryN > 65535) {
      server->send(400, "text/plain", "Invalid values");
      return;
    }
    config->setBattAdaptiveSleep(fullMv, lowMv, sleepMult, recoveryN);
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
