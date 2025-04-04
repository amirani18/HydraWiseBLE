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
#include "esp_random.h"
#include "sdkconfig.h"

// Define device
char *TAG = "HydraWise-BLE-Server";
#define CONFIG_IDF_TARGET_ESP32 1
uint8_t ble_addr_type;
static uint16_t conn_handle_global = 0; // Global connection handle to track the current connection
static uint16_t hrm_handle = 0; // Handle for Heart Rate Measurement characteristic
static uint16_t conductivity_handle = 0; // Handle for Conductivity characteristic
volatile bool is_running = true; // Flag to control task behavior
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
    if (strcmp(buf, "START") == 0) {
        is_running = true;
        printf("Starting...\n");
        ESP_LOGI(TAG, "START command received. Enabling data collection.");
    } else if (strcmp(buf, "STOP") == 0) {
        is_running = false;
        printf("Stopping...\n");
        ESP_LOGI(TAG, "STOP command received. Disabling data collection.");

    return 0;
}

// Read data from ESP32 defined as a server
// static int device_read(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
//     printf("Read data from the client\n");
//     os_mbuf_append(ctxt->om, "Hello from ESP32", 17);
//     return 0;
// }

// extravagant read data from ESP32 defined as a server
static int device_read(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (attr_handle == hrm_handle) {
        ESP_LOGI(TAG, "💓 Client is reading Heart Rate characteristic");
        float dummy_hr = 75.0f;
        os_mbuf_append(ctxt->om, &dummy_hr, sizeof(dummy_hr));
    }
    else if (attr_handle == conductivity_handle) {
        ESP_LOGI(TAG, "💧 Client is reading Conductivity characteristic");
        float dummy_conductivity = 1.23f;
        os_mbuf_append(ctxt->om, &dummy_conductivity, sizeof(dummy_conductivity));
    }
    else {
        ESP_LOGW(TAG, "⚠️ Unknown characteristic read (handle: %d)", attr_handle);
        os_mbuf_append(ctxt->om, "Unknown", 7);
    }

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
        if (conn_handle_global != 0 && is_running) // Check if connected and running
        {
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
            // vTaskDelay(pdMS_TO_TICKS(3000)); // Notify every 3 seconds
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// conductivity notification task with FreeRTOS
void notify_conductivity_task(void *param) {
    while(1) {
        if (conn_handle_global != 0 && is_running) // Check if connected and running
        {
            uint8_t conductivity_data[2] = { 0x00, 50 }; // Conductivity measurement (50 mS/cm)   

            struct os_mbuf *om = ble_hs_mbuf_from_flat(conductivity_data, sizeof(conductivity_data)); // Allocate a packet header
            int rc = ble_gattc_notify_custom(conn_handle_global, // Connection handle
                conductivity_handle, // Conductivity UUID
                om); // The data to send
            if (rc != 0) {
                printf("failed to send conductivity notification: %d\n", rc);
                ESP_LOGE(TAG, "Failed to send conductivity notification: %d", rc);
            } else {
                printf("conductivity notification sent: %d mS/cm\n", conductivity_data[1]);
                ESP_LOGI(TAG, "Conductivity notification sent: %d mS/cm", conductivity_data[1]);
            }
            // vTaskDelay(pdMS_TO_TICKS(3000)); // Notify every 3 seconds
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Yield every 5 seconds
    }
}

// Array of pointers to other service definitions
// UUID - Universal Unique Identifier
static const struct ble_gatt_svc_def gatt_svcs[] = {
    // Battery Service (0x180F)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F), // Service UUID: Battery Service
        .characteristics = battery_level_chr, // Characteristic: 0x2A19
    },

    // Heart Rate Service (0x180D)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180D), // Service UUID: Heart Rate Service
        .characteristics = heart_rate_chr, // Characteristic: 0x2A37
    },

    // Conductivity Service (custom UUID)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x181C), // Service UUID: Custom Conductivity
        .characteristics = conductivity_chr, // Characteristic: 128-bit UUID
    },

    // Device Information Service (0x180A)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A), // Service UUID: Device Info
        .characteristics = (struct ble_gatt_chr_def[])
        {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A29), // Characteristic: Manufacturer Name
                .access_cb = device_read,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A24), // Characteristic: Model Number
                .access_cb = device_read,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 } // Terminator
        }
    },

    // Custom Command Control Service (0x180C)
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180C), // Custom Service
        .characteristics = (struct ble_gatt_chr_def[])
        {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A00), // Characteristic: Device Name Write
                .access_cb = device_write,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 } // Terminator
        }
    },

    { 0 } // End of services
};


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
        // advertise again after completion of event
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI("GAP", "BLE GAP EVENT DISCONNECT");
            conn_handle_global = 0;  // Reset connection handle
            ble_app_advertise();     // Restart advertising
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
// void ble_app_advertise(void)
// {
//     // GAP - Generic Access Profile, device name definition
//     struct ble_hs_adv_fields fields;
//     const char *device_name;
//     memset(&fields, 0, sizeof(fields));
//     device_name = ble_svc_gap_device_name();
//     fields.name = (uint8_t *)device_name;
//     fields.name_len = strlen(device_name);
//     fields.name_is_complete = 1;
//     ble_gap_adv_set_fields(&fields);

//     // GAP - Generic Access Profile, device connectivity definition
//     struct ble_gap_adv_params adv_params;
//     memset(&adv_params, 0, sizeof(adv_params));
//     adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
//     adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
//     ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
// }

// for apple
void ble_app_advertise(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    // Include flags for general discoverability and BLE-only support
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Include complete device name
    const char *device_name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    // Include the Heart Rate Service UUID in the advertisement
    const uint16_t svc_uuid = 0x180D;
    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(svc_uuid)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    // Start advertising
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
    ESP_LOGI(TAG, "Attempting to locate Heart Rate Characteristic UUID: 0x2A37 in Service UUID: 0x180D");

    uint16_t def_handle;
    uint16_t val_handle;

    int rc = ble_gatts_find_chr(
        BLE_UUID16_DECLARE(0x180D),
        BLE_UUID16_DECLARE(0x2A37),
        &def_handle,
        &val_handle
    );

    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to find Heart Rate Measurement characteristic: %d", rc);
    } else {
        hrm_handle = val_handle;
        ESP_LOGI(TAG, "Heart Rate Measurement characteristic handle: %d", hrm_handle);
    }

    // conductivity handle
    ESP_LOGI(TAG, "Attempting to locate Conductivity Characteristic UUID: 0xAA5B9750C9824CE690C754C0C8C6AE84 in Service UUID: 0x181C");

    rc = ble_gatts_find_chr(
        BLE_UUID16_DECLARE(0x181C),
        (const ble_uuid_t *)&conductivity_uuid,
        &def_handle,
        &val_handle
    );

    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to find Conductivity characteristic: %d", rc);
    } else {
        conductivity_handle = val_handle;
        ESP_LOGI(TAG, "Conductivity characteristic handle: %d", conductivity_handle);
    }
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
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(host_task);
    xTaskCreate(notify_heart_rate_task, "hr_notify_task", 2048, NULL, 5, NULL); // Create FreeRTOS task for heart rate notifications
    xTaskCreate(notify_conductivity_task, "conductivity_notify_task", 2048, NULL, 5, NULL); // Create FreeRTOS task for conductivity notifications
    // Note: The above task will send heart rate notifications every 3 seconds
    // This will allow the ESP32 to send heart rate notifications to connected clients.
    // The application will now start advertising and waiting for connections.
    // The notify_heart_rate_task will run in parallel and send notifications to the connected client.
    // Make sure to handle the connection and disconnection events properly to manage the connection state.
}