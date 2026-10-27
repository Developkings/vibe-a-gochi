// WiFi station: saved network (NVS), scanning for the WiFi screen, and mDNS + companion server
// once an address is assigned.
#pragma once
#include <stdint.h>

enum class WifiStatus : uint8_t { OFF, CONNECTING, CONNECTED, FAILED };

struct WifiNet {
  char ssid[33];
  int8_t rssi;
  bool secure;
};

void wifi_begin();
void wifi_tick();
WifiStatus wifi_status();
const char *wifi_ssid();          // saved / connected network
const char *wifi_ip();
int wifi_rssi();
int wifi_drops();               // disconnects since boot (diagnostics)
int wifi_last_drop_reason();    // wifi_err_reason_t of the latest
const char *wifi_host();          // mDNS name, e.g. vibe-buddy-a1b2.local
void wifi_connect(const char *ssid, const char *password);   // saved
void wifi_forget();

void wifi_scan();
bool wifi_scanning();
int wifi_scan_count();
const WifiNet &wifi_scan_at(int i);   // strongest first
uint16_t wifi_scan_gen();             // bumps when a scan finishes
