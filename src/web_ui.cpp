
#include "web_ui.h"

#include "debug.h"
#include "helpers.h"

const char* uploadPage = R"(
<!DOCTYPE html><html><body>
<form method='POST' action='/update' enctype='multipart/form-data'>
  <input type='file' name='firmware'>
  <input type='submit' value='Update'>
</form></body></html>
)";

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

  server->on("/", HTTP_GET, [this]() { server->send(200, "text/html", uploadPage); });
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
