#include "debug.h"

namespace Debug {
static bool _enabled = true;

bool enabled() {
  return _enabled;
}
void enable() {
  _enabled = true;
}
void disable() {
  _enabled = false;
}

}  // namespace Debug
