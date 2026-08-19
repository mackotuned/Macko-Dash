/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MY_CODEC_H_
#define _MY_CODEC_H_

#include <stdio.h>
#include <string.h>
#include "esp_codec_dev.h"
#include "audio_codec_ctrl_if.h"
#include "audio_codec_gpio_if.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    esp_codec_dev_hw_gain_t hw_gain;
} my_codec_cfg_t;

typedef enum {
    MY_CODEC_REG_VOL,
    MY_CODEC_REG_MUTE,
    MY_CODEC_REG_MIC_GAIN,
    MY_CODEC_REG_MIC_MUTE,
    MY_CODEC_REG_SUSPEND,
    MY_CODEC_REG_MAX,
} my_codec_reg_type_t;

typedef struct {
    audio_codec_ctrl_if_t base;
    uint8_t reg[MY_CODEC_REG_MAX];
    bool is_open;
} my_codec_ctrl_t;

typedef struct {
    audio_codec_data_if_t base;
    esp_codec_dev_sample_info_t fmt;
    int read_idx;
    int write_idx;
    bool is_open;
} my_codec_data_t;

typedef struct {
    audio_codec_vol_if_t base;
    esp_codec_dev_sample_info_t fs;
    int shift;
    int process_len;
    float vol_db;
    bool is_open;
} my_codec_vol_t;

const audio_codec_ctrl_if_t *my_codec_ctrl_new(void);
const audio_codec_data_if_t *my_codec_data_new(void);
const audio_codec_if_t *my_codec_new(my_codec_cfg_t *codec_cfg);
const audio_codec_vol_if_t *my_codec_vol_new(void);
void my_codec_set_pa_active_high(bool active_high);
bool my_codec_get_pa_active_high(void);

#ifdef __cplusplus
}
#endif

#endif