// Claude Hardware Buddy transport: Nordic UART Service over BLE (NimBLE), LE Secure
// Connections with a 6-digit passkey shown on the device, bonded. Newline-delimited JSON.
#pragma once
#include <stddef.h>
#include <stdint.h>

void ble_begin();                 // advertises as "Claude-XXXX" (last two MAC bytes)
const char *ble_name();
bool ble_connected();
bool ble_secure();                // encrypted + authenticated link
uint32_t ble_passkey();           // non-zero while pairing waits for the code on the computer
int ble_bonds();
void ble_clear_bonds();           // "Forget pairing" / unpair command
int ble_read();                   // next received byte, -1 if none
size_t ble_write(const char *data, size_t len);  // notify in MTU-sized chunks
