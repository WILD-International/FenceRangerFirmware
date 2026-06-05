
#include "web_ui.h"

#include <ESPmDNS.h>

#include "helpers.h"
#include "logging.h"
#include "ota_html.h"
#include "version.h"

WebUi::WebUi(Config* config) : config(config), running(false) {}

void WebUi::apSsid(char* out, size_t len) const {
  char ssid[32] = "FR-";
  bytesToHex(config->getDevEUI(), DEVEUI_LEN, ssid + 3);
  snprintf(out, len, "%s", ssid);
}

void WebUi::localIpStr(char* out, size_t len) const {
  snprintf(out, len, "%s", WiFi.localIP().toString().c_str());
}

void WebUi::start(const char* password) {
  if (running) {
    LOGLN("WebUi already active");
    return;
  }
  running = true;
  resumeRequested_ = false;
  stationMode_ = false;
  char ssid[32];
  apSsid(ssid, sizeof(ssid));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  server = new WebServer(80);
  registerRoutes();
  server->begin();
}

bool WebUi::isStationInRange(const char* ssid) {
  WiFi.mode(WIFI_STA);
  int16_t found = WiFi.scanNetworks(false, false, false, 300, 0, ssid);  // directed scan
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  return found > 0;
}

void WebUi::startStation(const char* ssid, const char* password) {
  if (running) {
    LOGLN("WebUi already active");
    return;
  }
  running = true;
  resumeRequested_ = false;
  stationMode_ = true;
  server = nullptr;
  char host[32];
  apSsid(host, sizeof(host));  // "FR-<eui>" doubles as the DHCP hostname / mDNS name
  // setHostname must run BEFORE mode(WIFI_STA): the core applies the stored hostname to the
  // netif when the station interface starts, and falls back to "esp32s3-<MAC>" if it's unset.
  WiFi.setHostname(host);
  WiFi.mode(WIFI_STA);
  // The stack auto-reconnects on its own: it keeps re-issuing the join while the gateway is out
  // of range (NO_AP_FOUND) and again whenever an established link drops.
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  LOG("WiFi station: joining %s\n", ssid);
}

void WebUi::registerRoutes() {
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
    json += "\",\"euiOverride\":";
    json += config->hasCustomDevEUI() ? "true" : "false";
    json += ",\"name\":\"";
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
    json += ",\"rxLnaEnabled\":";
    json += (int)config->getRxLnaEnabled();
    json += ",\"measureDurationMs\":";
    json += config->getMeasureDurationMs();
    json += ",\"fenceCalPercent\":";
    json += config->getFenceCalPercent();
    json += ",\"txPower\":";
    json += config->getTxPower();
    json += ",\"dataRate\":";
    json += config->getDataRate();
    json += ",\"adrEnabled\":";
    json += (int)config->getAdrEnabled();
    json += ",\"syncwordPublic\":";
    json += (int)config->getSyncwordPublic();
    json += ",\"scanGuardMs\":";
    json += config->getScanGuardMs();
    json += ",\"version\":\"";
    json += GIT_TAG;
    json += "\"}";
    server->send(200, "application/json", json);
  });

  server->on("/device", HTTP_POST, [this]() {
    config->setDevName(server->arg("name").c_str());

    String hexEui = server->arg("eui");
    if (hexEui.length() == DEVEUI_LEN * 2) {
      uint8_t eui[DEVEUI_LEN];
      if (!hexToBytes(hexEui.c_str(), eui, DEVEUI_LEN)) {
        server->send(400, "text/plain", "Invalid EUI");
        return;
      }
      config->setDevEUI(eui);
    } else if (hexEui.length() == 0) {
      config->clearDevEUI();
    } else {
      server->send(400, "text/plain", "EUI must be 16 hex chars");
      return;
    }

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

  server->on("/alert", HTTP_POST, [this]() {
    String alertArg = server->arg("alertV");
    String recoverArg = server->arg("recoverV");
    if (alertArg.length() == 0 || recoverArg.length() == 0) {
      server->send(400, "text/plain", "alertV and recoverV required");
      return;
    }
    int32_t alertV = alertArg.toInt();
    int32_t recoverV = recoverArg.toInt();
    if (alertV < 0 || alertV > 65535 || recoverV < 0 || recoverV > 65535) {
      server->send(400, "text/plain", "Threshold must be 0-65535");
      return;
    }
    String burstArg = server->arg("burstSends");
    String idleHbArg = server->arg("idleHbCycles");
    String alertHbArg = server->arg("alertHbCycles");
    String recoverCyclesArg = server->arg("recoverCycles");
    if (burstArg.length() == 0 || idleHbArg.length() == 0 || alertHbArg.length() == 0 ||
        recoverCyclesArg.length() == 0) {
      server->send(400, "text/plain", "All cycle fields required");
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
    config->setAlertThresholds(alertV, recoverV);
    config->setAlertCycles(burst, idleHb, alertHb, recoverCycles);
    server->send(200, "text/plain", "Saved");
  });

  server->on("/sleep", HTTP_POST, [this]() {
    String sleepArg = server->arg("sleepInterval");
    String battFullArg = server->arg("battFullMv");
    String battLowArg = server->arg("battLowMv");
    String battMultArg = server->arg("battSleepMult");
    String battNArg = server->arg("battRecoveryN");
    if (sleepArg.length() == 0 || battFullArg.length() == 0 || battLowArg.length() == 0 ||
        battMultArg.length() == 0 || battNArg.length() == 0) {
      server->send(400, "text/plain", "All fields required");
      return;
    }
    int32_t sleepS = sleepArg.toInt();
    int32_t fullMv = battFullArg.toInt();
    int32_t lowMv = battLowArg.toInt();
    int32_t sleepMult = battMultArg.toInt();
    int32_t recoveryN = battNArg.toInt();
    if (sleepS < 1 || sleepS > 65535 || fullMv < 1 || fullMv > 65535 || lowMv < 1 ||
        lowMv > 65535 || sleepMult < 1 || sleepMult > 255 || recoveryN < 1 || recoveryN > 65535) {
      server->send(400, "text/plain", "Invalid values");
      return;
    }
    config->setSleepIntervalS(sleepS);
    config->setBattAdaptiveSleep(fullMv, lowMv, sleepMult, recoveryN);
    server->send(200, "text/plain", "Saved");
  });

  server->on("/measure", HTTP_POST, [this]() {
    String durArg = server->arg("measureDurationMs");
    String calArg = server->arg("fenceCalPercent");
    if (durArg.length() == 0 || calArg.length() == 0) {
      server->send(400, "text/plain", "All fields required");
      return;
    }
    int32_t dur = durArg.toInt();
    int32_t cal = calArg.toInt();
    if (dur < 100 || dur > 60000 || cal < 1 || cal > 1000) {
      server->send(400, "text/plain", "Invalid values");
      return;
    }
    config->setMeasureDurationMs((uint16_t)dur);
    config->setFenceCalPercent((uint16_t)cal);
    server->send(200, "text/plain", "Saved");
  });

  server->on("/radio", HTTP_POST, [this]() {
    String txPowerArg = server->arg("txPower");
    String drArg = server->arg("dataRate");
    if (txPowerArg.length() == 0 || drArg.length() == 0) {
      server->send(400, "text/plain", "All fields required");
      return;
    }
    String scanGuardArg = server->arg("scanGuardMs");
    if (scanGuardArg.length() == 0) {
      server->send(400, "text/plain", "All fields required");
      return;
    }
    int32_t txPow = txPowerArg.toInt();
    int32_t dr = drArg.toInt();
    int32_t scanGuard = scanGuardArg.toInt();
    if (txPow < 0 || txPow > 22 || dr < 0 || dr > 5 || scanGuard < 0 || scanGuard > 500) {
      server->send(400, "text/plain", "Invalid values");
      return;
    }
    config->setRxLnaEnabled(server->arg("rxLnaEnabled") == "1");
    config->setTxPower((int8_t)txPow);
    config->setDataRate((uint8_t)dr);
    config->setAdrEnabled(server->arg("adrEnabled") == "1");
    config->setSyncwordPublic(server->arg("syncwordPublic") == "1");
    config->setScanGuardMs((uint16_t)scanGuard);
    server->send(200, "text/plain", "Saved");
  });

  server->on("/resume", HTTP_POST, [this]() {
    // Just flag it: the config-mode loop polls resumeRequested(), stops the AP, and exits to
    // deep sleep. Tearing WiFi down here would kill the response before it reached the client.
    resumeRequested_ = true;
    server->send(200, "text/plain", "Resuming normal operation");
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
}

void WebUi::stop() {
  if (!running) {
    LOGLN("WebUi not running");
    return;
  }
  running = false;
  stationMode_ = false;
  // The server is null if a station join was still pending / disconnected (never served).
  if (server != nullptr) {
    server->stop();
    delete server;
    server = nullptr;
  }
  // Tear down whichever mode was up (the inapplicable calls are harmless no-ops).
  MDNS.end();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

void WebUi::update() {
  if (!running) {
    LOGLN("WebUi not running");
    return;
  }
  if (stationMode_) {
    // The stack handles (re)joining; we just follow the link with the server + mDNS.
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && server == nullptr) {
      // (Re)associated — bring up mDNS + server. The IP may differ after a reconnect.
      char host[32];
      apSsid(host, sizeof(host));
      MDNS.begin(host);
      MDNS.addService("http", "tcp", 80);
      server = new WebServer(80);
      registerRoutes();
      server->begin();
      LOG("WiFi station connected, IP %s\n", WiFi.localIP().toString().c_str());
    } else if (!connected && server != nullptr) {
      // Link dropped — tear down the server/mDNS; the stack auto-reconnects in the background.
      LOGLN("WiFi station disconnected, waiting for reconnect");
      server->stop();
      delete server;
      server = nullptr;
      MDNS.end();
    }
    if (server != nullptr) {
      server->handleClient();
    }
    return;
  }
  server->handleClient();
}
