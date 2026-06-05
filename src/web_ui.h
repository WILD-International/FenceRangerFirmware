#ifndef WEB_UI_H
#define WEB_UI_H

#include <Update.h>
#include <WebServer.h>

#include "config.h"

class WebUi {
 public:
  WebUi(Config* config);

  void start(const char* password = AP_PASSWORD);
  // Begin joining an existing AP as a station (non-blocking). The connection is retried until the
  // AP comes in range; once connected, update() brings up mDNS and the HTTP server.
  void startStation(const char* ssid, const char* password);
  void stop();
  void update();

  bool isRunning() const {
    return running;
  }

  bool isStationConnected() const {
    return WiFi.status() == WL_CONNECTED;
  }

  // Lean presence check (no association): returns true if an AP with the given SSID is currently
  // in range. Leaves WiFi off. Used by the low-power station acquire to gate a connect attempt.
  bool isStationInRange(const char* ssid);

  // Writes the station IP (e.g. "192.168.4.23") into out.
  void localIpStr(char* out, size_t len) const;

  // True once the web UI's "resume monitoring" button has been pressed. The config-mode loop
  // polls this to stop the AP and return the device to its normal measurement cycle.
  bool resumeRequested() const {
    return resumeRequested_;
  }

  // The fixed AP password. Acceptable because the AP is only ever up while an operator is
  // physically present at the device's WiFi AP screen.
  static constexpr const char* AP_PASSWORD = "wildinternational";

  // The known provisioning-gateway SSID a JoinWifiSta downlink connects to when the downlink
  // omits an SSID override. A network name is not a secret, so baking it in is fine. Set this to
  // the real gateway name.
  static constexpr const char* STA_GATEWAY_SSID = "FenceUpdater";

  // Writes the AP SSID ("FR-<DevEUI>") into out.
  void apSsid(char* out, size_t len) const;

 private:
  // Registers all HTTP routes on `server`; shared by the AP and station paths.
  void registerRoutes();

  Config* config;
  WebServer* server;
  bool running;
  bool resumeRequested_ = false;
  // Station mode: the WiFi stack's own auto-reconnect keeps the link up (it re-issues the join
  // on NO_AP_FOUND and on drops); update() only brings the HTTP server + mDNS up and down to
  // follow the connection state.
  bool stationMode_ = false;
};

#endif  // WEB_UI_H
