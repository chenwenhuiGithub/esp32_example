#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "ir_nec_encoder.h"

#define EXAMPLE_IR_RESOLUTION_HZ     1000000 // 1MHz, 1 tick = 1us
#define EXAMPLE_IR_TX_GPIO_NUM       18
#define EXAMPLE_IR_RX_GPIO_NUM       19
#define EXAMPLE_IR_NEC_DECODE_MARGIN 200

#define NEC_LEADING_CODE_DURATION_0  9000
#define NEC_LEADING_CODE_DURATION_1  4500
#define NEC_PAYLOAD_ZERO_DURATION_0  560
#define NEC_PAYLOAD_ZERO_DURATION_1  560
#define NEC_PAYLOAD_ONE_DURATION_0   560
#define NEC_PAYLOAD_ONE_DURATION_1   1690
#define NEC_REPEAT_CODE_DURATION_0   9000
#define NEC_REPEAT_CODE_DURATION_1   2250

static const char *TAG = "ir_nec";

static rmt_channel_handle_t hd_rx_channel = NULL;
static rmt_channel_handle_t hd_tx_channel = NULL;
static QueueHandle_t hd_queue = NULL;
static rmt_encoder_handle_t hd_nec_encoder = NULL;
static uint16_t nec_address = 0;
static uint16_t nec_command = 0;


static inline bool nec_check_in_range(uint32_t signal_duration, uint32_t spec_duration)
{
    return (signal_duration < (spec_duration + EXAMPLE_IR_NEC_DECODE_MARGIN)) &&
           (signal_duration > (spec_duration - EXAMPLE_IR_NEC_DECODE_MARGIN));
}

static bool nec_parse_logic0(rmt_symbol_word_t *rmt_nec_symbols)
{
    return nec_check_in_range(rmt_nec_symbols->duration0, NEC_PAYLOAD_ZERO_DURATION_0) &&
           nec_check_in_range(rmt_nec_symbols->duration1, NEC_PAYLOAD_ZERO_DURATION_1);
}

static bool nec_parse_logic1(rmt_symbol_word_t *rmt_nec_symbols)
{
    return nec_check_in_range(rmt_nec_symbols->duration0, NEC_PAYLOAD_ONE_DURATION_0) &&
           nec_check_in_range(rmt_nec_symbols->duration1, NEC_PAYLOAD_ONE_DURATION_1);
}

static bool nec_recv_done_cb(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    xQueueSendFromISR(hd_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

void nec_frame_parse(rmt_symbol_word_t *rmt_nec_symbols, size_t symbol_num)
{
    size_t i = 0;

    ESP_LOGI(TAG, "NEC symbols");
    for (i = 0; i < symbol_num; i++) {
        ESP_LOGI(TAG, "%02d: {%d:%d},{%d:%d}", i, rmt_nec_symbols[i].level0, rmt_nec_symbols[i].duration0, rmt_nec_symbols[i].level1, rmt_nec_symbols[i].duration1);
    }

    if (34 == symbol_num) {
        if (!(nec_check_in_range(rmt_nec_symbols->duration0, NEC_LEADING_CODE_DURATION_0) && nec_check_in_range(rmt_nec_symbols->duration1, NEC_LEADING_CODE_DURATION_1))) {
            ESP_LOGE(TAG, "invalid leading symbol");
            return;
        }
        rmt_nec_symbols++;
        for (i = 0; i < 16; i++) {
            if (nec_parse_logic1(rmt_nec_symbols)) {
                nec_address |= 1 << i;
            } else if (nec_parse_logic0(rmt_nec_symbols)) {
                nec_address &= ~(1 << i);
            } else {
                ESP_LOGE(TAG, "invalid address symbol:%u", i);
                return;
            }
            rmt_nec_symbols++;
        }
        for (i = 0; i < 16; i++) {
            if (nec_parse_logic1(rmt_nec_symbols)) {
                nec_command |= 1 << i;
            } else if (nec_parse_logic0(rmt_nec_symbols)) {
                nec_command &= ~(1 << i);
            } else {
                ESP_LOGE(TAG, "invalid command symbol:%u", i);
                return;
            }
            rmt_nec_symbols++;
        }
        ESP_LOGI(TAG, "NEC frame: address=0x%04X, command=0x%04X", nec_address, nec_command);
    } else {
        ESP_LOGE(TAG, "Unknown NEC frame");
    }
}

void nec_frame_transmit(uint16_t address, uint16_t command) {
    const ir_nec_scan_code_t scan_code = {
            .address = address,
            .command = command,
    };
    rmt_transmit_config_t transmit_cfg = {
        .loop_count = 0, // no loop
    };

    rmt_transmit(hd_tx_channel, hd_nec_encoder, &scan_code, sizeof(scan_code), &transmit_cfg);
}

void app_main(void) {
    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = EXAMPLE_IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .gpio_num = EXAMPLE_IR_RX_GPIO_NUM,
    };
    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = EXAMPLE_IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = EXAMPLE_IR_TX_GPIO_NUM,
    };
    rmt_rx_event_callbacks_t rx_cbs = {
        .on_recv_done = nec_recv_done_cb,
    };
    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = 38000,
    };
    rmt_receive_config_t receive_cfg = {
        .signal_range_min_ns = 1250,     // the shortest duration for NEC signal is 560us, valid signal won't be treated as noise
        .signal_range_max_ns = 12000000, // the longest duration for NEC signal is 9000us, the receive won't stop early
    };
    rmt_symbol_word_t raw_symbols[64] = {0}; // standard NEC frame 34 symbols
    rmt_rx_done_event_data_t rx_data = {0};
    uint32_t i = 0;

    hd_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rmt_new_ir_nec_encoder(EXAMPLE_IR_RESOLUTION_HZ, &hd_nec_encoder);

    rmt_new_rx_channel(&rx_channel_cfg, &hd_rx_channel);
    rmt_rx_register_event_callbacks(hd_rx_channel, &rx_cbs, NULL);
    rmt_new_tx_channel(&tx_channel_cfg, &hd_tx_channel);
    rmt_apply_carrier(hd_tx_channel, &carrier_cfg);
    rmt_enable(hd_tx_channel);
    rmt_enable(hd_rx_channel);
    
    rmt_receive(hd_rx_channel, raw_symbols, sizeof(raw_symbols), &receive_cfg);
    while (1) {
        if (xQueueReceive(hd_queue, &rx_data, (1000 / portTICK_PERIOD_MS)) == pdPASS) {
            nec_frame_parse(rx_data.received_symbols, rx_data.num_symbols);
            rmt_receive(hd_rx_channel, raw_symbols, sizeof(raw_symbols), &receive_cfg);
        } else {
            ESP_LOGI(TAG, "%lu", i++);
        }
    }
}
