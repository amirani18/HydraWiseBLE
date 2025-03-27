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
uint8_t ble_addr_type;
void ble_app_advertise(void);

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

// Array of pointers to other service definitions
// UUID - Universal Unique Identifier
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) 
        {
            {.uuid = BLE_UUID16_DECLARE(0x2A19),
            .access_cb = device_read,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE
            },
            {.uuid = BLE_UUID16_DECLARE(0x2A00),
            .access_cb = device_write,
            .flags = BLE_GATT_CHR_F_WRITE
            },
            {0}
        }
    }, 
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x181A),
        .characteristics = heart_rate_chr,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x181C),
        .characteristics = conductivity_chr,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = battery_level_chr,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) 
        {
            {.uuid = BLE_UUID16_DECLARE(0x2A29),
            .access_cb = device_read,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE
            },
            {.uuid = BLE_UUID16_DECLARE(0x2A24),
            .access_cb = device_write,
            .flags = BLE_GATT_CHR_F_WRITE
            },
            {0}
        }
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180C),
        .characteristics = (struct ble_gatt_chr_def[]) 
        {
            {.uuid = BLE_UUID16_DECLARE(0x2A37),
            .access_cb = device_read,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE
            },
            {.uuid = BLE_UUID16_DECLARE(0x2A38),
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
            ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s", event -> connect.status == 0 ? "success" : "fail");
            if (event -> connect.status != 0) {
                ble_app_advertise();
            }
            break;
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
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(host_task);
}