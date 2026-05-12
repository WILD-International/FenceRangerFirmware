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

 private:
  Config* config;
  WebServer* server;
  bool running;
};

#endif  // WEB_UI_H
