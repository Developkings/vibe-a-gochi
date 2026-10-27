#include "wifi_link.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <algorithm>
#include "companion.h"
#include "power.h"

namespace {

Preferences prefs;
char ssid[33] = "", pass[65] = "", ip[16] = "", host[32] = "";
WifiStatus status = WifiStatus::OFF;
uint32_t connect_started = 0;
bool services_up = false;
volatile uint16_t drops = 0;          // WiFi disconnects since boot (diagnostics)
volatile uint8_t last_drop_reason = 0;   // wifi_err_reason_t of the latest one
bool worked = false;   // the saved network connected at least once since boot / since it was set

constexpr int MAX_NETS = 16;
WifiNet nets[MAX_NETS];
int n_nets = 0;
bool scanning = false;
uint16_t scan_gen = 0;

// A new attempt must first stop the old one: after a wrong password the driver keeps retrying it,
// and a begin() issued meanwhile is ignored (stuck on "searching").
void start(const char *s, const char *p) {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);   // drop the old network and its saved config
  for (uint32_t t0 = millis(); WiFi.status() == WL_CONNECTED && millis() - t0 < 1000;) delay(10);
  delay(100);
  WiFi.setAutoReconnect(true);
  const wl_status_t r = WiFi.begin(s, p);
  Serial.printf("[wifi] connecting to %s (begin %d)\n", s, int(r));
  status = WifiStatus::CONNECTING;
  connect_started = millis();
}

void services() {
  if (services_up) return;
  services_up = true;
  if (MDNS.begin(host)) {
    MDNS.addService("vibebuddy", "tcp", COMPANION_PORT);
    MDNS.addServiceTxt("vibebuddy", "tcp", "name", (const char *)host);
  }
  companion_begin();
}

}  // namespace

void wifi_begin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(host, sizeof host, "vibe-buddy-%02x%02x", mac[4], mac[5]);
  prefs.begin("wifi", false);
  prefs.getString("ssid", ssid, sizeof ssid);
  prefs.getString("pass", pass, sizeof pass);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    drops++;
    last_drop_reason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(host);
  WiFi.setAutoReconnect(true);
  // A mesh (several access points, one name): scan every channel and join the strongest, not the
  // first one found (the default fast scan), which can be a far one with a weak signal.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  worked = ssid[0] != 0;   // a saved network: keep retrying it if the router is off at boot
  if (ssid[0]) start(ssid, pass);
}

void wifi_tick() {
  // Modem sleep between beacons saves a lot of battery but can drop packets with some routers:
  // only on battery.
  static int on_usb = -1;
  if (int(power_on_usb()) != on_usb) {
    on_usb = power_on_usb();
    WiFi.setSleep(!on_usb);
  }
  const wl_status_t s = WiFi.status();
  if (s == WL_CONNECTED) {
    if (status != WifiStatus::CONNECTED) {
      status = WifiStatus::CONNECTED;
      worked = true;
      strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof ip);
      Serial.printf("[wifi] %s, %s (%s.local)\n", ssid, ip, host);
      services();
    }
  } else if (status == WifiStatus::CONNECTED) {
    start(ssid, pass);   // dropped (router restart, out of range): try again from scratch
  } else if (status == WifiStatus::CONNECTING && millis() - connect_started > 20000) {
    // Not connected in 20 s, whatever the driver reports: say so and stop retrying (a wrong
    // password would otherwise retry forever). Auto-reconnect only after a working connection.
    status = WifiStatus::FAILED;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    Serial.printf("[wifi] %s: failed (status %d)\n", ssid, int(s));
  } else if (status == WifiStatus::FAILED && worked && millis() - connect_started > 60000) {
    start(ssid, pass);   // a network that worked before (router restarting): retry every minute
  }

  if (scanning) {
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    scanning = false;
    n_nets = 0;
    for (int i = 0; i < n && n_nets < MAX_NETS; i++) {
      const String name = WiFi.SSID(i);
      if (!name.length()) continue;
      bool dup = false;   // one row per name (mesh / dual-band show up several times)
      for (int k = 0; k < n_nets; k++) dup |= !strcmp(nets[k].ssid, name.c_str());
      if (dup) continue;
      WifiNet &w = nets[n_nets++];
      strlcpy(w.ssid, name.c_str(), sizeof w.ssid);
      w.rssi = WiFi.RSSI(i);
      w.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    std::sort(nets, nets + n_nets, [](const WifiNet &a, const WifiNet &b) { return a.rssi > b.rssi; });
    WiFi.scanDelete();
    scan_gen++;
  }
}

WifiStatus wifi_status() { return status; }
const char *wifi_ssid() { return ssid; }
const char *wifi_ip() { return status == WifiStatus::CONNECTED ? ip : ""; }
int wifi_drops() { return drops; }
int wifi_last_drop_reason() { return last_drop_reason; }

int wifi_rssi() { return status == WifiStatus::CONNECTED ? WiFi.RSSI() : -127; }
const char *wifi_host() { return host; }

void wifi_connect(const char *s, const char *p) {
  strlcpy(ssid, s, sizeof ssid);
  strlcpy(pass, p ? p : "", sizeof pass);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  worked = false;
  start(ssid, pass);
}

void wifi_forget() {
  ssid[0] = pass[0] = 0;
  prefs.clear();
  WiFi.disconnect(false, true);
  status = WifiStatus::OFF;
}

void wifi_scan() {
  if (scanning) return;
  scanning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
}

bool wifi_scanning() { return scanning; }
int wifi_scan_count() { return n_nets; }
const WifiNet &wifi_scan_at(int i) { return nets[i]; }
uint16_t wifi_scan_gen() { return scan_gen; }
