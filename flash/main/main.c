#include <string.h>
#include "esp_partition.h"
#include "esp_log.h"

static const char *TAG = "flash";

void app_main(void)
{
    /*
    #  Name,     Type, SubType, Offset,   Size,   Flags
    #  Note: if you have increased the bootloader size, make sure to update the offsets to avoid overlap 
    *
    *  nvs,      data, nvs,     0x9000,   0x6000,
    *  phy_init, data, phy,     0xf000,   0x1000,
    *  factory,  app,  factory, 0x10000,  1M,
    *  storage,  data, 0xff,    0x110000, 0x1000,
    */

    esp_partition_t *partition = NULL;
    uint8_t write_buf[] = "Hello world, my name is esp32";
    uint8_t read_buf[32] = { 0 };

    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (partition != NULL) {
        ESP_LOGI(TAG, "found partition, addr:0x%X size:0x%X", partition->address, partition->size);
    } else {
        ESP_LOGE(TAG, "not found partition");
        return;
    }

    esp_partition_write(partition, 0, write_buf, sizeof(write_buf));
    ESP_LOGI(TAG, "write data, %s", write_buf);
    memset(read_buf, 0, sizeof(read_buf));
    esp_partition_read(partition, 0, read_buf, sizeof(read_buf));
    ESP_LOGI(TAG, " read data, %s", (char *)read_buf);
    esp_partition_erase_range(partition, 0, partition->size); // offset:4K aligned, size:4K multiple
    ESP_LOGI(TAG, "earse data");
    memset(read_buf, 0, sizeof(read_buf));
    esp_partition_read(partition, 0, read_buf, sizeof(read_buf));
    ESP_LOGI(TAG, " read data");
    esp_log_buffer_hex(TAG, read_buf, sizeof(read_buf));  
}
