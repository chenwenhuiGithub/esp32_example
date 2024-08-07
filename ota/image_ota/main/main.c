#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_partition.h"
#include "esp_flash_partitions.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_log.h"


static const char *TAG = "image_ota";

void app_main(void)
{
	uint32_t i = 0;
	const esp_partition_t *running_partition = NULL;
	esp_app_desc_t running_app_desc = {0};

	ESP_LOGI(TAG, "hello world");
	running_partition = esp_ota_get_running_partition();
    esp_ota_get_partition_description(running_partition, &running_app_desc);
    ESP_LOGI(TAG, "running partition, address:0x%08"PRIx32" size:0x%08"PRIx32" firmware_version:%s",
             running_partition->address, running_partition->size, running_app_desc.version);

	while (1) {
		ESP_LOGI(TAG, "%ld", i);
		i++;
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}
