# Pre-deployment TODO

Work to complete before the first FenceRanger is deployed in the field. Each item
records **what** we want and, above all, **why** — the motivation that the eventual
implementation has to serve. The how (approach, open design questions) is deliberately
left out; it gets decided when each item is picked up.

---

## 1. WiFi off by default in config mode; enable explicitly via menu

**What:** WiFi stays off when entering config mode. The operator turns the AP on
explicitly from a menu item. (Today config mode always starts the AP.)

**Why:** The AP is only needed for the occasional firmware update or web-based config —
most config-mode sessions don't use it. Running WiFi the whole time wastes power and
keeps a radio surface open for no reason. Making it opt-in means the device isn't
broadcasting an AP unless someone deliberately asked for one, while the operator is
standing right there. The fixed default password is fine in that physically-present
moment because exposure is bounded to those few minutes.

---

## 2. Activate WiFi AP over LoRa, with the AP password set by the downlink

**What:** A LoRaWAN downlink that puts the device into WiFi-AP update mode and carries
the AP password in its payload, so each activation uses a password we choose at that
moment.

**Why:** There can be up to ~1 hour between remotely activating WiFi and the crew
physically reaching the device. With a fixed or default password, anyone who previously
got the password off one device could connect during that window and push their own
firmware before the crew arrives — a single compromised device would hand an attacker a
reusable key to every device. Making the password a per-activation parameter means a
password is only ever valid for the one activation it was issued for; learning it later
is worthless, and a compromise of one device doesn't endanger the fleet.

---

## 3. LoRa method to join the device to an existing WiFi AP (station mode)

**What:** A LoRaWAN downlink that makes the device connect as a client to a given WiFi
access point, with the credentials (at least the password) carried in the message.

**Why:** We want to prepare a special "gateway" device that acts as the access point,
recognizes the fence monitors that connect to it, and initiates updates from that side.
This flips the workflow: instead of a person walking up to each unit and joining its AP
one at a time, a crew brings one provisioning box into range and the monitors come to
it — far faster for updating many devices in the field. Putting the credentials in the
LoRa message keeps the same per-activation security posture as item 2: no standing
secret baked into the fleet.

---

## 4. Secondary alert: rapid relative-drop detection

**What:** In addition to the existing absolute-threshold alert, trigger a send when the
fence voltage drops by more than a set percentage over a short window — even while it's
still above the absolute alert threshold.

**Why:** The current alert only fires once voltage crosses a fixed line, which misses a
fence that is failing fast but hasn't crossed it yet (e.g. a developing short, a draining
energizer, vegetation load building up). A sharp relative drop is itself a signal that
something changed, and catching it early gives the operator a head start before the fence
is fully down — exactly the kind of early warning that justifies the device being in the
field.

---

## 5. Config option for carrier-board revision

**What:** A stored config option selecting the carrier-board revision (the PCB with the
voltage dividers and opamp enable), used to pick the right pin mapping at runtime.
Settable via AT, Web UI, and OLED — **not** over LoRa (it's physical and a wrong remote
value would silently break measurements).

**Why:** One firmware image runs on every revision, so OTA updates the whole fleet and we
never track or mis-flash per-board builds.
