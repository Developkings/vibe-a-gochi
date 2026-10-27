// Port of the reference ble_bridge.cpp (Bluedroid) to NimBLE: same service, same security.
#include "ble_link.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>

namespace {

const char *NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const char *NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const char *NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

// Received bytes wait here for the main loop. Big enough for a heartbeat with 8 entries
// plus a folder-push chunk; the GATT layer flow-controls the sender.
constexpr size_t RX_CAP = 4096;
uint8_t rx_buf[RX_CAP];
volatile size_t rx_head = 0, rx_tail = 0;

NimBLEServer *server = nullptr;
NimBLECharacteristic *tx = nullptr;
volatile bool connected = false, secure = false;
volatile uint32_t passkey = 0;
volatile uint16_t mtu = 23;
char name[16];

void rx_push(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    const size_t next = (rx_head + 1) % RX_CAP;
    if (next == rx_tail) return;   // full: drop (the desktop resends on its next heartbeat)
    rx_buf[rx_head] = p[i];
    rx_head = next;
  }
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    const NimBLEAttValue &v = c->getValue();
    if (v.length()) rx_push(v.data(), v.length());
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &) override {
    connected = true;
    Serial.println("[ble] connected");
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
    connected = secure = false;
    passkey = 0;
    mtu = 23;
    Serial.printf("[ble] disconnected (%d)\n", reason);
  }
  void onMTUChange(uint16_t m, NimBLEConnInfo &) override { mtu = m; }
  // We are DisplayOnly: pick the code, show it, the user types it on the computer.
  uint32_t onPassKeyDisplay() override {
    passkey = 100000 + esp_random() % 900000;
    Serial.printf("[ble] passkey %06lu\n", (unsigned long)passkey);
    return passkey;
  }
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    passkey = 0;
    secure = info.isEncrypted() && info.isAuthenticated();
    Serial.printf("[ble] auth %s\n", secure ? "ok" : "FAILED");
    if (!secure) server->disconnect(info.getConnHandle());
  }
};

}  // namespace

void ble_begin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(name, sizeof name, "Claude-%02X%02X", mac[4], mac[5]);

  NimBLEDevice::init(name);
  NimBLEDevice::setMTU(517);
  NimBLEDevice::setSecurityAuth(true, true, true);              // bonding, MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  server->advertiseOnDisconnect(true);

  NimBLEService *svc = server->createService(NUS_SERVICE);
  tx = svc->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);
  NimBLECharacteristic *rx = svc->createCharacteristic(
      NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  rx->setCallbacks(new RxCallbacks());
  svc->start();

  // 128-bit UUID + flags fill most of the 31-byte advertisement: the name goes in the scan response.
  NimBLEAdvertisementData adv, scan;
  adv.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  adv.addServiceUUID(NimBLEUUID(NUS_SERVICE));
  scan.setName(name);
  NimBLEAdvertising *a = NimBLEDevice::getAdvertising();
  a->setAdvertisementData(adv);
  a->setScanResponseData(scan);
  a->start();
  Serial.printf("[ble] advertising as %s, %d bond(s)\n", name, NimBLEDevice::getNumBonds());
}

const char *ble_name() { return name; }
bool ble_connected() { return connected; }
bool ble_secure() { return secure; }
uint32_t ble_passkey() { return passkey; }
int ble_bonds() { return NimBLEDevice::getNumBonds(); }

void ble_clear_bonds() {
  if (connected) server->disconnect(server->getPeerInfo(0).getConnHandle());
  NimBLEDevice::deleteAllBonds();
  Serial.println("[ble] bonds cleared");
}

int ble_read() {
  if (rx_head == rx_tail) return -1;
  const int b = rx_buf[rx_tail];
  rx_tail = (rx_tail + 1) % RX_CAP;
  return b;
}

size_t ble_write(const char *data, size_t len) {
  if (!connected || !secure || !tx) return 0;
  size_t chunk = mtu > 3 ? mtu - 3 : 20;
  if (chunk > 180) chunk = 180;
  size_t sent = 0;
  while (sent < len) {
    const size_t n = min(chunk, len - sent);
    for (int tries = 0; !tx->notify(reinterpret_cast<const uint8_t *>(data + sent), n) && tries < 20; tries++) {
      delay(5);   // out of buffers: let the stack drain
    }
    sent += n;
    delay(2);
  }
  return sent;
}
