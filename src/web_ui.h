#ifndef WEB_UI_H
#define WEB_UI_H

#include <Update.h>
#include <WebServer.h>

#include "config.h"

class WebUi {
 public:
  WebUi(Config* config);

  void start(const char* password = AP_PASSWORD);
  void stop();
  void update();

  bool isRunning() const {
    return running;
  }

  // True once the web UI's "resume monitoring" button has been pressed. The config-mode loop
  // polls this to stop the AP and return the device to its normal measurement cycle.
  bool resumeRequested() const {
    return resumeRequested_;
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
  bool resumeRequested_ = false;
};

#endif  // WEB_UI_H
