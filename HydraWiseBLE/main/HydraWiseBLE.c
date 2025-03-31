#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sdkconfig.h"

// Define device
char *TAG = "HydraWise-BLE-Server";
#define CONFIG_IDF_TARGET_ESP32 1
uint8_t ble_addr_type;
static uint16_t conn_handle_global = 0; // Global connection handle to track the current connection
static uint16_t hrm_handle = 0; // Handle for Heart Rate Measurement characteristic
void ble_app_advertise(void);

/*
-------------------------------------------

ESP 32 FUNCTIONALITIES

---------------------------------------------
1. Device Name: HydraWise-BLE-Server
2. Services:
   - Heart Rate Service
   - Conductivity Service
   - Battery Level Service
   - Device Information Service
3. Characteristics:
    - Heart Rate Measurement (Notify)
    - Conductivity Measurement (Notify)
    - Battery Level (Notify)
    - Device Name (Read/Write)
    - Device Information (Read/Write)
    - Custom Commands (e.g., "LIGHT ON", "LIGHT OFF")
4. Access Control:
    - Heart Rate Measurement: Read and Notify
    - Conductivity Measurement: Read and Notify
    - Battery Level: Read and Notify
    - Device Name: Read and Write
    - Device Information: Read and Write
    - Custom Commands: Write only (to control external devices)
5. Connection Handling:
    - Handle connection and disconnection events
    - Retry advertising on disconnection
    - Store connection handle for further communication
---------------------------------------------
*/
// Write data to ESP32 defined as server
static int device_write(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {
    printf("Received WRITE (handle: %d, conn: %d)\n", attr_handle, conn_handle);

    // Print data as a raw string (if safe)
    char buf[ctxt->om->om_len + 1]; // +1 for null-termination
    memcpy(buf, ctxt->om->om_data, ctxt->om->om_len);
    buf[ctxt->om->om_len] = '\0'; // ensure it's null-terminated

    printf("Data from the client: %s\n", buf);

    // You can add parsing logic here
    if (strcmp(buf, "LIGHT ON") == 0) {
    printf("Turning LIGHT ON\n");
    } else if (strcmp(buf, "LIGHT OFF") == 0) {
    printf("Turning LIGHT OFF\n");
    }

    return 0;
}

// Read data from ESP32 defined as a server
static int device_read(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    printf("Read data from the client\n");
    os_mbuf_append(ctxt->om, "Hello from ESP32", 17);
    return 0;
}

// heart rate characteristic
static const struct ble_gatt_chr_def heart_rate_chr[] = {
    {
        .uuid = BLE_UUID16_DECLARE(0x2A37), // HEART RATE MEASUREMENT
        .access_cb = device_read,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    {
        0, // NULL TERMINATOR
    }
};

// conductivity characteristic
static const ble_uuid128_t conductivity_uuid =
    BLE_UUID128_INIT(0xaa, 0x5b, 0x97, 0x50,
                     0xc9, 0x82, 0x4c, 0xe6,
                     0x90, 0xc7, 0x54, 0xc0,
                     0xc8, 0xc6, 0xae, 0x84);

static struct ble_gatt_chr_def conductivity_chr[] = {
    {
        .uuid = (const ble_uuid_t *)&conductivity_uuid,  // Cast to correct type
        .access_cb = device_read,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    {
        0
    },  // Terminator
};

// battery level characteristic
static const struct ble_gatt_chr_def battery_level_chr[] = {
    {
        .uuid = BLE_UUID16_DECLARE(0x2A19), // BATTERY LEVEL
        .access_cb = device_read,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    {
        0, // NULL TERMINATOR
    }
};

// heart rate notification task with FreeRTOS
void notify_heart_rate_task(void *param) {
    while(1) {
        if (conn_handle_global != 0)
        {
            // format: 1 byte for flags, 1 or 2 bytes for heart rate value (example: 60 bpm)
            uint8_t hr_data[2] = { 0x00, 75 }; // Heart rate measurement (75 bpm)   
            struct os_mbuf *om = ble_hs_mbuf_from_flat(hr_data, sizeof(hr_data)); // Allocate a packet header
            int rc = ble_gattc_notify_custom(conn_handle_global, // Connection handle
                hrm_handle, // Heart Rate Measurement UUID
                om); // The data to send
            if (rc != 0) {
                printf("failed to send heart rate notification: %d\n", rc);
                ESP_LOGE(TAG, "Failed to send heart rate notification: %d", rc);
            } else {
                printf("heart rate notification sent: %d bpm\n", hr_data[1]);
                ESP_LOGI(TAG, "Heart rate notification sent: %d bpm", hr_data[1]);
            }
            vTaskDelay(pdMS_TO_TICKS(3000)); // Notify every 3 seconds
        }
    }
}

// Array of pointers to other service definitions
// UUID - Universal Unique Identifier
static const struct ble_gatt_svc_def gatt_svcs[] = {
    // Battery service (service UUID: 0x180F)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180E), // service UUID: battery service ?
        .characteristics = (struct ble_gatt_chr_def[]) 
        {
            {.uuid = BLE_UUID16_DECLARE(0x2A19), // characteristic UUID: battery level
            .access_cb = device_read,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE
            },
            {.uuid = BLE_UUID16_DECLARE(0x2A00), // characteristic UUID: device name
            .access_cb = device_write,
            .flags = BLE_GATT_CHR_F_WRITE
            },
            {0}
        }
    }, 
    // Heart Rate Service (Service UUID: 0x181D)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180D), // service UUID
        .characteristics = heart_rate_chr,  // characteristic 0x2A37 UUID
    },
    // Conductivity Service (Service UUID: 0x181C)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x181C), // service UUID
        .characteristics = conductivity_chr, // characteristic custom 128-bit for conductivity
    },
    // Battery Level  (duplicate?)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = battery_level_chr,
    },
    // device information service (service uuid: 0x180A)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A), // service UUID
        .characteristics = (struct ble_gatt_chr_def[]) 
        {
            {.uuid = BLE_UUID16_DECLARE(0x2A29), // characteristic UUID (manufacturer name string)
            .access_cb = device_read,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE
            },
            {.uuid = BLE_UUID16_DECLARE(0x2A24), // characteristic UUID: model number string
            .access_cb = device_write,
            .flags = BLE_GATT_CHR_F_WRITE
            },
            {0}
        }
    },
    // custom control service (service UUID: 0x180C)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180C), // service UUID
        .characteristics = (struct ble_gatt_chr_def[]) 
        {
            // {.uuid = BLE_UUID16_DECLARE(0x2A37), // characteristic UUID (heart rate duplicate???)
            // .access_cb = device_read,
            // .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE
            // },
            {.uuid = BLE_UUID16_DECLARE(0x2A38), // characteristic UUID: body sensor location
            .access_cb = device_write,
            .flags = BLE_GATT_CHR_F_WRITE
            },
            {0}
        }
    },// End of characteristics
    {0}}; // End of services

// BLE event handling
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event -> type)
    {
        // Advertise if connected
        case BLE_GAP_EVENT_CONNECT:
            if (event -> connect.status == 0) {
                ESP_LOGI("GAP", "Device connected");
                conn_handle_global = event -> connect.conn_handle; // Store the connection handle globally
            }
            else {
                ble_app_advertise(); // Retry advertising if connection failed
            }
            break;
        //     ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s", event -> connect.status == 0 ? "success" : "fail");
        //     if (event -> connect.status != 0) {
        //         ble_app_advertise();
        //     }
        //     break;
        // advertise again after completion of event
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI("GAP", "BLE GAP EVENT DISCONNECT");
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI("GAP", "BLE GAP EVENT ADV COMPLETE");
            ble_app_advertise();
            break;
        default:
            break;
    }
    return 0;          
}

// Define BLE connection
void ble_app_advertise(void)
{
    // GAP - Generic Access Profile, device name definition
    struct ble_hs_adv_fields fields;
    const char *device_name;
    memset(&fields, 0, sizeof(fields));
    device_name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    // GAP - Generic Access Profile, device connectivity definition
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

// The application
void ble_app_on_sync(void) {
    ble_hs_id_infer_auto(0, &ble_addr_type);
    ble_app_advertise();
}

// the inifinite task
void host_task(void *param) {
    nimble_port_run();
}

void app_main() {
    nvs_flash_init();
    // esp_nimble_hci_and_controller_init();
    nimble_port_init();
    ble_svc_gap_device_name_set("HydraWise-BLE-Server");
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    // heart rate measurement characteristic handle
    int rc = ble_gatts_find_chr(BLE_UUID16_DECLARE(0x181A), // Heart Rate Service UUID
        BLE_UUID16_DECLARE(0x2A37), // Heart Rate Measurement UUID
        NULL, // No specific UUID to search for
        &hrm_handle);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to find Heart Rate Measurement characteristic: %d", rc);
        return; // Handle error
    } else {
        ESP_LOGI(TAG, "Heart Rate Measurement characteristic handle: %d", hrm_handle);
    }
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(host_task);
    xTaskCreate(notify_heart_rate_task, "hr_notify_task", 4096, NULL, 5, NULL); // Create FreeRTOS task for heart rate notifications
    // Note: The above task will send heart rate notifications every 3 seconds
    // This will allow the ESP32 to send heart rate notifications to connected clients.
    // The application will now start advertising and waiting for connections.
    // The notify_heart_rate_task will run in parallel and send notifications to the connected client.
    // Make sure to handle the connection and disconnection events properly to manage the connection state.
}