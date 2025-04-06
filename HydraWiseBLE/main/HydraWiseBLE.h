#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <stdint.h>
#include "host/ble_gatt.h"

// ---------------- Constants & Macros ----------------

#define CONFIG_IDF_TARGET_ESP32 1

extern char *TAG;

// ---------------- Global Variables ----------------

extern uint8_t ble_addr_type;
extern uint8_t button_state;

extern uint16_t hrm_handle;
extern uint16_t conductivity_handle;
extern uint16_t battery_handle;
extern uint16_t button_char_handle;
extern volatile uint16_t conn_handle_global;

// ---------------- Function Declarations ----------------

// Advertising
void ble_app_advertise(void);

// Event handling
void ble_app_on_sync(void);
void host_task(void *param);

// Tasks
void notify_heart_rate_task(void *param);
void notify_conductivity_task(void *param);

// Access Callbacks
int device_read(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
int device_write(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

#endif // BLE_SERVER_H
