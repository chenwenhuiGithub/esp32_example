#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "ir_rmt.h"
#include "ir_nec_encoder.h"


#define EXAMPLE_IR_RESOLUTION_HZ            1000000 // 1MHz, 1 tick = 1us

#define RMTID_TV                            0
#define RMTID_SETTOPBOX                     1
#define RMTID_AC_LIVING                     2
#define RMTID_AC_BEDROOM                    3
#define RMTID_LIGHT_BEDROOM                 4
#define RMTID_MAX                           5

#define CHANNELID_0                      	0
#define CHANNELID_1                      	1
#define CHANNELID_2                      	2
#define CHANNELID_3                      	3
#define CHANNELID_4                      	4
#define CHANNELID_5                      	5
#define CHANNELID_6                      	6
#define CHANNELID_7                      	7
#define CHANNELID_8                      	8
#define CHANNELID_9                      	9
#define CHANNELID_UP                       	10
#define CHANNELID_DOWN                     	11
#define CHANNELID_LEFT                  	12
#define CHANNELID_RIGHT                 	13
#define CHANNELID_OK                    	14
#define CHANNELID_VOLUME_ADD            	15
#define CHANNELID_VOLUME_SUB            	16
#define CHANNELID_CHANNEL_ADD           	17
#define CHANNELID_CHANNEL_SUB           	18
#define CHANNELID_POWER                 	19
#define CHANNELID_HOME                   	20
#define CHANNELID_SIGNAL                   	21
#define CHANNELID_MUTE                   	22
#define CHANNELID_BACK                   	23
#define CHANNELID_MENU                   	24
#define CHANNELID_SETTING                 	25
#define CHANNELID_MAX                 	    64


typedef struct  {
    uint8_t channel_id;
    ir_nec_frame_t channel_frame;
} ir_rmt_channel_t;

typedef struct {
    uint8_t rmt_id;
    ir_rmt_channel_t rmt_channels[CHANNELID_MAX];
} ir_rmt_t;

static const char *TAG = "ir_rmt";

static rmt_channel_handle_t hd_tx_channel = NULL;
static rmt_encoder_handle_t hd_nec_encoder = NULL;

static const ir_rmt_t ir_rms[RMTID_MAX] = {
    {
        RMTID_TV, {
            {CHANNELID_0,           {0x654C, 0xBA45}},
            {CHANNELID_1,           {0x654C, 0xFE01}},
            {CHANNELID_2,           {0x654C, 0xFD02}},
            {CHANNELID_3,           {0x654C, 0xFC03}},
            {CHANNELID_4,           {0x654C, 0xFB04}},
            {CHANNELID_5,           {0x654C, 0xFA05}},
            {CHANNELID_6,           {0x654C, 0xF906}},
            {CHANNELID_7,           {0x654C, 0xF807}},
            {CHANNELID_8,           {0x654C, 0xF708}},
            {CHANNELID_9,           {0x654C, 0xF609}},
            {CHANNELID_UP,          {0x654C, 0xF40B}},
            {CHANNELID_DOWN,        {0x654C, 0xF10E}},
            {CHANNELID_LEFT,        {0x654C, 0xEF10}},
            {CHANNELID_RIGHT,       {0x654C, 0xEE11}},
            {CHANNELID_OK,          {0x654C, 0xF20D}},
            {CHANNELID_VOLUME_ADD,  {0x654C, 0xEA15}},
            {CHANNELID_VOLUME_SUB,  {0x654C, 0xE31C}},
            {CHANNELID_CHANNEL_ADD, {0x654C, 0xE01F}},
            {CHANNELID_CHANNEL_SUB, {0x654C, 0xE11E}},
            {CHANNELID_POWER,       {0x654C, 0xF50A}},
            {CHANNELID_HOME,        {0x654C, 0xE916}},
            {CHANNELID_SIGNAL,      {0x654C, 0xF30C}},
            {CHANNELID_MUTE,        {0x654C, 0xF00F}},
            {CHANNELID_BACK,        {0x654C, 0xE21D}},
            {CHANNELID_MENU,        {0x654C, 0xC837}},
            {CHANNELID_SETTING,     {0x654C, 0xFF00}}
        }
    },
    {
        RMTID_SETTOPBOX, {
            {CHANNELID_0,           {0xDD22, 0x7887}},
            {CHANNELID_1,           {0xDD22, 0x6D92}},
            {CHANNELID_2,           {0xDD22, 0x6C93}},
            {CHANNELID_3,           {0xDD22, 0x33CC}},
            {CHANNELID_4,           {0xDD22, 0x718E}},
            {CHANNELID_5,           {0xDD22, 0x708F}},
            {CHANNELID_6,           {0xDD22, 0x37C8}},
            {CHANNELID_7,           {0xDD22, 0x758A}},
            {CHANNELID_8,           {0xDD22, 0x748B}},
            {CHANNELID_9,           {0xDD22, 0x3BC4}},
            {CHANNELID_UP,          {0xDD22, 0x35CA}},
            {CHANNELID_DOWN,        {0xDD22, 0x2DD2}},
            {CHANNELID_LEFT,        {0xDD22, 0x6699}},
            {CHANNELID_RIGHT,       {0xDD22, 0x3EC1}},
            {CHANNELID_OK,          {0xDD22, 0x31CE}},
            {CHANNELID_VOLUME_ADD,  {0xDD22, 0x7F80}},
            {CHANNELID_VOLUME_SUB,  {0xDD22, 0x7E81}},
            {CHANNELID_CHANNEL_ADD, {0xDD22, 0x7A85}},
            {CHANNELID_CHANNEL_SUB, {0xDD22, 0x7986}},
            {CHANNELID_POWER,       {0xDD22, 0x23DC}},
            {CHANNELID_HOME,        {0xDD22, 0x7788}},
            {CHANNELID_SIGNAL,      {0x654C, 0xF30C}},
            {CHANNELID_MUTE,        {0xDD22, 0x639C}},
            {CHANNELID_BACK,        {0xDD22, 0x6A95}},
            {CHANNELID_MENU,        {0xDD22, 0x7D82}},
            {CHANNELID_SETTING,     {0xDD22, 0x728D}}
        }
    },
    {
        RMTID_AC_LIVING, {
            {CHANNELID_CHANNEL_ADD, {0x0001, 0x0002}},
            {CHANNELID_CHANNEL_SUB, {0x0001, 0x0002}},
            {CHANNELID_VOLUME_SUB,  {0x0001, 0x0002}},
            {CHANNELID_VOLUME_ADD,  {0x0001, 0x0002}},
            {CHANNELID_OK,          {0x0001, 0x0002}},
            {CHANNELID_POWER,       {0x0001, 0x0002}}
        }
    },  
    {
        RMTID_AC_BEDROOM, {
            {CHANNELID_CHANNEL_ADD, {0x0001, 0x0002}},
            {CHANNELID_CHANNEL_SUB, {0x0001, 0x0002}},
            {CHANNELID_VOLUME_SUB,  {0x0001, 0x0002}},
            {CHANNELID_VOLUME_ADD,  {0x0001, 0x0002}},
            {CHANNELID_OK,          {0x0001, 0x0002}},
            {CHANNELID_POWER,       {0x0001, 0x0002}}
        }
    },  
    {
        RMTID_LIGHT_BEDROOM, {
            {CHANNELID_VOLUME_ADD,  {0xFF00, 0xBF40}},
            {CHANNELID_VOLUME_SUB,  {0xFF00, 0xE619}},
            {CHANNELID_POWER,       {0xFF00, 0xF20D}}
        }
    }
};

void ir_rmt_init() {
    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = EXAMPLE_IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = EXAMPLE_IR_TX_GPIO_NUM,
    };
    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = 38000,
    };

    rmt_new_ir_nec_encoder(EXAMPLE_IR_RESOLUTION_HZ, &hd_nec_encoder);
    rmt_new_tx_channel(&tx_channel_cfg, &hd_tx_channel);
    rmt_apply_carrier(hd_tx_channel, &carrier_cfg);
    rmt_enable(hd_tx_channel);
}

void ir_rmt_send(ir_nec_frame_t frame) {
    rmt_transmit_config_t transmit_cfg = {
        .loop_count = 0, // no loop
    };

    rmt_transmit(hd_tx_channel, hd_nec_encoder, &frame, sizeof(frame), &transmit_cfg);
}

void ir_rmt_recv(uint8_t rmt_id, uint8_t channel_id) {
    uint8_t i = 0, j = 0;

    for (i = 0; i < RMTID_MAX; i++) {
        if (rmt_id == ir_rms[i].rmt_id) {
            for (j = 0; j < CHANNELID_MAX; j++) {
                if (channel_id == ir_rms[i].rmt_channels[j].channel_id) {
                    ESP_LOGI(TAG, "address:0x%04X, command:0x%04X",
                             ir_rms[i].rmt_channels[j].channel_frame.address, ir_rms[i].rmt_channels[j].channel_frame.command);
                    ir_rmt_send(ir_rms[i].rmt_channels[j].channel_frame);
                    return;                    
                }
            }
        }
    }
}

