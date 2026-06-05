#ifndef WEB_UI_H
#define WEB_UI_H

#include <Update.h>
#include <WebServer.h>

#include "config.h"

class WebUi {
 public:
  WebUi(Config* config);

  void start();
  void stop();
  void update();

  bool isRunning() const {
    return running;
  }

  // The fixed AP password. Acceptable because the AP is only ever up while an operator is
  // physically present at the device's WiFi AP screen.
  static constexpr const char* AP_PASSWORD = "wildinternational";

  // Writes the AP SSID ("FR-<DevEUI>") into out.
  void apSsid(char* out, size_t len) const;

 private:
  Config* config;
  WebServer* server;
  bool running;
};

#endif  // WEB_UI_H
