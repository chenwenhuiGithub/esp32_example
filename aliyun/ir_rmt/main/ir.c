#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "ir.h"


#define NEC_LEADING_CODE_DURATION_0                 9000
#define NEC_LEADING_CODE_DURATION_1                 4500
#define NEC_DATA_ZERO_DURATION_0                    560
#define NEC_DATA_ZERO_DURATION_1                    560
#define NEC_DATA_ONE_DURATION_0                     560
#define NEC_DATA_ONE_DURATION_1                     1690
#define NEC_ENDING_CODE_DURATION_0                  560
#define NEC_ENDING_CODE_DURATION_1                  0x7FFF
#define NEC_REPEAT_CODE_DURATION_0                  9000
#define NEC_REPEAT_CODE_DURATION_1                  2250
#define NEC_DECODE_MARGIN_DURATION                  200
#define NEC_FRAME_LEN                               4

#define CONFIG_IR_RESOLUTION_HZ                     1000000 // 1MHz, 1 tick = 1us


typedef struct {
    rmt_encoder_t base;                 // the base "class", declares the standard encoder interface
    rmt_encoder_t *encoder_copy;        // use the encoder_copy to encode the leading and ending pulse
    rmt_encoder_t *encoder_bytes;       // use the encoder_bytes to encode the address and command data
    rmt_symbol_word_t symbol_leading;   // NEC leading code with RMT representation
    rmt_symbol_word_t symbol_ending;    // NEC ending code with RMT representation
} ir_nec_encoder;

typedef struct  {
    uint8_t channel_id;
    uint8_t channel_frame[NEC_FRAME_LEN];
} ir_channel_info_t;

typedef struct {
    uint8_t rmt_id;
    ir_channel_info_t rmt_channels[CHANNELID_MAX];
} ir_rmt_info_t;

static ir_rmt_info_t rmt_infos[RMTID_MAX] = {
    {
        RMTID_TV, {
            {CHANNELID_0,           {0x4C, 0x65, 0x45, 0xBA}},
            {CHANNELID_1,           {0x4C, 0x65, 0x01, 0xFE}},
            {CHANNELID_2,           {0x4C, 0x65, 0x02, 0xFD}},
            {CHANNELID_3,           {0x4C, 0x65, 0x03, 0xFC}},
            {CHANNELID_4,           {0x4C, 0x65, 0x04, 0xFB}},
            {CHANNELID_5,           {0x4C, 0x65, 0x05, 0xFA}},
            {CHANNELID_6,           {0x4C, 0x65, 0x06, 0xF9}},
            {CHANNELID_7,           {0x4C, 0x65, 0x07, 0xF8}},
            {CHANNELID_8,           {0x4C, 0x65, 0x08, 0xF7}},
            {CHANNELID_9,           {0x4C, 0x65, 0x09, 0xF6}},
            {CHANNELID_UP,          {0x4C, 0x65, 0x0B, 0xF4}},
            {CHANNELID_DOWN,        {0x4C, 0x65, 0x0E, 0xF1}},
            {CHANNELID_LEFT,        {0x4C, 0x65, 0x10, 0xEF}},
            {CHANNELID_RIGHT,       {0x4C, 0x65, 0x11, 0xEE}},
            {CHANNELID_OK,          {0x4C, 0x65, 0x0D, 0xF2}},
            {CHANNELID_VOLUME_ADD,  {0x4C, 0x65, 0x15, 0xEA}},
            {CHANNELID_VOLUME_SUB,  {0x4C, 0x65, 0x1C, 0xE3}},
            {CHANNELID_CHANNEL_ADD, {0x4C, 0x65, 0x1F, 0xE0}},
            {CHANNELID_CHANNEL_SUB, {0x4C, 0x65, 0x1E, 0xE1}},
            {CHANNELID_POWER,       {0x4C, 0x65, 0x0A, 0xF5}},
            {CHANNELID_HOME,        {0x4C, 0x65, 0x16, 0xE9}},
            {CHANNELID_SIGNAL,      {0x4C, 0x65, 0x0C, 0xF3}},
            {CHANNELID_MUTE,        {0x4C, 0x65, 0x0F, 0xF0}},
            {CHANNELID_BACK,        {0x4C, 0x65, 0x1D, 0xE2}},
            {CHANNELID_MENU,        {0x4C, 0x65, 0x37, 0xC8}},
            {CHANNELID_SETTING,     {0x4C, 0x65, 0x00, 0xFF}}
        }
    },
    {
        RMTID_SETTOPBOX, {
            {CHANNELID_0,           {0x22, 0xDD, 0x87, 0x78}},
            {CHANNELID_1,           {0x22, 0xDD, 0x92, 0x6D}},
            {CHANNELID_2,           {0x22, 0xDD, 0x93, 0x6C}},
            {CHANNELID_3,           {0x22, 0xDD, 0xCC, 0x33}},
            {CHANNELID_4,           {0x22, 0xDD, 0x8E, 0x71}},
            {CHANNELID_5,           {0x22, 0xDD, 0x8F, 0x70}},
            {CHANNELID_6,           {0x22, 0xDD, 0xC8, 0x37}},
            {CHANNELID_7,           {0x22, 0xDD, 0x8A, 0x75}},
            {CHANNELID_8,           {0x22, 0xDD, 0x8B, 0x74}},
            {CHANNELID_9,           {0x22, 0xDD, 0xC4, 0x3B}},
            {CHANNELID_UP,          {0x22, 0xDD, 0xCA, 0x35}},
            {CHANNELID_DOWN,        {0x22, 0xDD, 0xD2, 0x2D}},
            {CHANNELID_LEFT,        {0x22, 0xDD, 0x99, 0x66}},
            {CHANNELID_RIGHT,       {0x22, 0xDD, 0xC1, 0x3E}},
            {CHANNELID_OK,          {0x22, 0xDD, 0xCE, 0x31}},
            {CHANNELID_VOLUME_ADD,  {0x22, 0xDD, 0x80, 0x7F}},
            {CHANNELID_VOLUME_SUB,  {0x22, 0xDD, 0x81, 0x7E}},
            {CHANNELID_CHANNEL_ADD, {0x22, 0xDD, 0x85, 0x7A}},
            {CHANNELID_CHANNEL_SUB, {0x22, 0xDD, 0x86, 0x79}},
            {CHANNELID_POWER,       {0x22, 0xDD, 0xDC, 0x23}},
            {CHANNELID_HOME,        {0x22, 0xDD, 0x88, 0x77}},
            {CHANNELID_SIGNAL,      {0x4C, 0x65, 0x0C, 0xF3}},
            {CHANNELID_MUTE,        {0x22, 0xDD, 0x9C, 0x63}},
            {CHANNELID_BACK,        {0x22, 0xDD, 0x95, 0x6A}},
            {CHANNELID_MENU,        {0x22, 0xDD, 0x82, 0x7D}},
            {CHANNELID_SETTING,     {0x22, 0xDD, 0x8D, 0x72}}
        }
    }, 
    {
        RMTID_LIGHT_BEDROOM, {
            {CHANNELID_VOLUME_ADD,  {0x00, 0xFF, 0x40, 0xBF}},
            {CHANNELID_VOLUME_SUB,  {0x00, 0xFF, 0x19, 0xE6}},
            {CHANNELID_POWER,       {0x00, 0xFF, 0x0D, 0xF2}}
        }
    }
};

static QueueHandle_t s_hd_queue = NULL;
static rmt_channel_handle_t s_hd_rx_channel = NULL;
static rmt_channel_handle_t s_hd_tx_channel = NULL;
static rmt_encoder_handle_t s_hd_nec_encoder = NULL;
static const char *TAG = "ir";

static size_t encode_nec_encoder(rmt_encoder_t *encoder, rmt_channel_handle_t tx_channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    ir_nec_encoder *nec_encoder = __containerof(encoder, ir_nec_encoder, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encoder_handle_t hd_encoder_copy = nec_encoder->encoder_copy;
    rmt_encoder_handle_t hd_encoder_bytes = nec_encoder->encoder_bytes;
    size_t encoded_symbols = 0;

    // send leading code
    encoded_symbols += hd_encoder_copy->encode(hd_encoder_copy, tx_channel, &nec_encoder->symbol_leading, sizeof(rmt_symbol_word_t), &session_state);
    if (session_state & RMT_ENCODING_MEM_FULL) {
        *ret_state |= RMT_ENCODING_MEM_FULL;
        return encoded_symbols;
    }
    // send address and command
    encoded_symbols += hd_encoder_bytes->encode(hd_encoder_bytes, tx_channel, (uint8_t *)primary_data, NEC_FRAME_LEN, &session_state);
    if (session_state & RMT_ENCODING_MEM_FULL) {
        *ret_state |= RMT_ENCODING_MEM_FULL;
        return encoded_symbols;
    }
    // send ending code
    encoded_symbols += hd_encoder_copy->encode(hd_encoder_copy, tx_channel, &nec_encoder->symbol_ending, sizeof(rmt_symbol_word_t), &session_state);
    if (session_state & RMT_ENCODING_MEM_FULL) {
        *ret_state |= RMT_ENCODING_MEM_FULL;
        return encoded_symbols;
    }

    *ret_state |= RMT_ENCODING_COMPLETE;
    return encoded_symbols;
}

static esp_err_t del_nec_encoder(rmt_encoder_t *encoder)
{
    ir_nec_encoder *nec_encoder = __containerof(encoder, ir_nec_encoder, base);
    rmt_del_encoder(nec_encoder->encoder_copy);
    rmt_del_encoder(nec_encoder->encoder_bytes);
    free(nec_encoder);
    return ESP_OK;
}

static esp_err_t reset_nec_encoder(rmt_encoder_t *encoder)
{
    ir_nec_encoder *nec_encoder = __containerof(encoder, ir_nec_encoder, base);
    rmt_encoder_reset(nec_encoder->encoder_copy);
    rmt_encoder_reset(nec_encoder->encoder_bytes);
    return ESP_OK;
}

static esp_err_t create_nec_encoder()
{
    ir_nec_encoder *nec_encoder = NULL;
    nec_encoder = rmt_alloc_encoder_mem(sizeof(ir_nec_encoder));
    nec_encoder->base.encode = encode_nec_encoder;
    nec_encoder->base.del = del_nec_encoder;
    nec_encoder->base.reset = reset_nec_encoder;

    rmt_copy_encoder_config_t copy_encoder_config = {};
    rmt_new_copy_encoder(&copy_encoder_config, &nec_encoder->encoder_copy);

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = CONFIG_IR_RESOLUTION_HZ / 1000000 * NEC_DATA_ZERO_DURATION_0, // 560us
            .level1 = 0,
            .duration1 = CONFIG_IR_RESOLUTION_HZ / 1000000 * NEC_DATA_ZERO_DURATION_1, // 560us
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = CONFIG_IR_RESOLUTION_HZ / 1000000 * NEC_DATA_ONE_DURATION_0, // 560us
            .level1 = 0,
            .duration1 = CONFIG_IR_RESOLUTION_HZ / 1000000 * NEC_DATA_ONE_DURATION_1, // 1690us
        },
    };
    rmt_new_bytes_encoder(&bytes_encoder_config, &nec_encoder->encoder_bytes);

    nec_encoder->symbol_leading = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = CONFIG_IR_RESOLUTION_HZ / 1000000 * NEC_LEADING_CODE_DURATION_0, // 9000us
        .level1 = 0,
        .duration1 = CONFIG_IR_RESOLUTION_HZ / 1000000 * NEC_LEADING_CODE_DURATION_1, // 4500us
    };
    nec_encoder->symbol_ending = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = CONFIG_IR_RESOLUTION_HZ / 1000000 * NEC_ENDING_CODE_DURATION_0, // 560us
        .level1 = 0,
        .duration1 = CONFIG_IR_RESOLUTION_HZ / 1000000 * NEC_ENDING_CODE_DURATION_1, // 0x7FFF
    };

    s_hd_nec_encoder = &nec_encoder->base;
    return ESP_OK;
}

static inline bool check_nec_range(uint32_t signal, uint32_t spec)
{
    return ((signal < (spec + NEC_DECODE_MARGIN_DURATION)) && (signal > (spec - NEC_DECODE_MARGIN_DURATION)));
}

static bool parse_nec_logic0(rmt_symbol_word_t symbol)
{
    return (check_nec_range(symbol.duration0, NEC_DATA_ZERO_DURATION_0) && check_nec_range(symbol.duration1, NEC_DATA_ZERO_DURATION_1));
}

static bool parse_nec_logic1(rmt_symbol_word_t symbol)
{
    return (check_nec_range(symbol.duration0, NEC_DATA_ONE_DURATION_0) && check_nec_range(symbol.duration1, NEC_DATA_ONE_DURATION_1));
}

static void parse_nec_frame(rmt_symbol_word_t *symbols, uint32_t num)
{
    uint32_t i = 0, j = 0, k = 0;
    uint8_t frame[NEC_FRAME_LEN] = {0};

    // for (k = 0; k < num; k++) {
    //     ESP_LOGI(TAG, "%02lu: {%u:%u},{%u:%u}", k, symbols[k].level0, symbols[k].duration0, symbols[k].level1, symbols[k].duration1);
    // }

    if (34 == num) {
        if (!(check_nec_range(symbols[0].duration0, NEC_LEADING_CODE_DURATION_0) && check_nec_range(symbols[0].duration1, NEC_LEADING_CODE_DURATION_1))) {
            ESP_LOGE(TAG, "invalid leading symbol");
            return;
        }

        k = 1;
        for (i = 0; i < NEC_FRAME_LEN; i++) {
            for (j = 0; j < 8; j++) {
                if (parse_nec_logic1(symbols[k])) {
                    frame[i] |= 1 << j;
                } else if (parse_nec_logic0(symbols[k])) {
                    frame[i] &= ~(1 << j);
                } else {
                    ESP_LOGE(TAG, "invalid symbol, k:%lu", k);
                    return;
                }
                k++;
            }            
        }
        ESP_LOGI(TAG, "recv frame:%02X %02X %02X %02X", frame[0], frame[1], frame[2], frame[3]);
    } else if (2 == num) {
        if (check_nec_range(symbols[0].duration0, NEC_REPEAT_CODE_DURATION_0) && check_nec_range(symbols[0].duration1, NEC_REPEAT_CODE_DURATION_1)) {
            ESP_LOGI(TAG, "repeat frame");
        } else {
            ESP_LOGE(TAG, "unknown frame");
        }
    } else {
        ESP_LOGE(TAG, "unknown frame");
    }
}

static bool ir_recv_done_cb(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    xQueueSendFromISR(s_hd_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void ir_recv_task(void* parameter) {
    rmt_receive_config_t receive_cfg = {
        .signal_range_min_ns = 1250,     // the shortest duration for NEC signal is 560us, valid signal won't be treated as noise
        .signal_range_max_ns = 12000000, // the longest duration for NEC signal is 9000us, the receive won't stop early
    };
    rmt_symbol_word_t raw_symbols[64] = {0}; // standard NEC frame 34 symbols
    rmt_rx_done_event_data_t rx_data = {0};

    rmt_receive(s_hd_rx_channel, raw_symbols, sizeof(raw_symbols), &receive_cfg);
    while (1) {
        if (xQueueReceive(s_hd_queue, &rx_data, (1000 / portTICK_PERIOD_MS)) == pdPASS) {
            parse_nec_frame(rx_data.received_symbols, rx_data.num_symbols);
            rmt_receive(s_hd_rx_channel, raw_symbols, sizeof(raw_symbols), &receive_cfg);
        }
    }
}

void ir_init() {
    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = CONFIG_IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .gpio_num = CONFIG_GPIO_NUM_IR_RX,
    };
    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = CONFIG_IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = CONFIG_GPIO_NUM_IR_TX,
    };
    rmt_rx_event_callbacks_t rx_cbs = {
        .on_recv_done = ir_recv_done_cb,
    };
    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = 38000,
    };

    s_hd_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));

    rmt_new_rx_channel(&rx_channel_cfg, &s_hd_rx_channel);
    rmt_rx_register_event_callbacks(s_hd_rx_channel, &rx_cbs, NULL);
    rmt_new_tx_channel(&tx_channel_cfg, &s_hd_tx_channel);
    rmt_apply_carrier(s_hd_tx_channel, &carrier_cfg);
    rmt_enable(s_hd_rx_channel);
    rmt_enable(s_hd_tx_channel);

    create_nec_encoder();
    xTaskCreate(ir_recv_task, "ir_recv_task", 2048, NULL, 1, NULL);
}

void ir_send(uint8_t *data, uint32_t len) {
    rmt_transmit_config_t transmit_cfg = {
        .loop_count = 0, // no loop
    };

    rmt_transmit(s_hd_tx_channel, s_hd_nec_encoder, data, len, &transmit_cfg);
}

void ir_recv(uint8_t rmt_id, uint8_t channel_id) {
    uint8_t i = 0, j = 0;

    for (i = 0; i < RMTID_MAX; i++) {
        if (rmt_id == rmt_infos[i].rmt_id) {
            for (j = 0; j < CHANNELID_MAX; j++) {
                if (channel_id == rmt_infos[i].rmt_channels[j].channel_id) {
                    ESP_LOGI(TAG, "send frame:%02X %02X %02X %02X",
                             rmt_infos[i].rmt_channels[j].channel_frame[0], rmt_infos[i].rmt_channels[j].channel_frame[1],
                             rmt_infos[i].rmt_channels[j].channel_frame[2], rmt_infos[i].rmt_channels[j].channel_frame[3]);
                    ir_send(rmt_infos[i].rmt_channels[j].channel_frame, NEC_FRAME_LEN);
                    return;                    
                }
            }
        }
    }
}

