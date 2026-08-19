#ifndef _BSP_STC8H1KXX_H_
#define _BSP_STC8H1KXX_H_

/*————————————————————————————————————————Header file declaration————————————————————————————————————————*/
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "bsp/esp32_p4_function_ev_board.h"
/*——————————————————————————————————————Header file declaration end——————————————————————————————————————*/

#ifdef __cplusplus
extern "C"
{
#endif

/*——————————————————————————————————————————Variable declaration—————————————————————————————————————————*/
#define STC8H1KXX_TAG "STC8H1KXX"
#define STC8H1KXX_INFO(fmt, ...)        ESP_LOGI(STC8H1KXX_TAG, fmt, ##__VA_ARGS__)
#define STC8H1KXX_DEBUG(fmt, ...)       ESP_LOGD(STC8H1KXX_TAG, fmt, ##__VA_ARGS__)
#define STC8H1KXX_ERROR(fmt, ...)       ESP_LOGE(STC8H1KXX_TAG, fmt, ##__VA_ARGS__)

#define CONFIG_BSP_STC8H1KXX_ENABLED
#define CONFIG_BSP_STC8H1KXX_BATTERY_ENABLED
#define CONFIG_BSP_STC8H1KXX_GPIO_ENABLED
#define CONFIG_BSP_STC8H1KXX_PWM_ENABLED

#ifdef CONFIG_BSP_STC8H1KXX_ENABLED
typedef uint8_t   	u8;
typedef uint16_t  	u16;
typedef uint32_t  	u32;

#define BSP_STC8H1KXX_I2C_ADDRESS       0x2F

esp_err_t stc8_i2c_init();

#ifdef CONFIG_BSP_STC8H1KXX_BATTERY_ENABLED

typedef struct{
  	u32 adc_voltage;    //Collected voltage(mV)
	u32 bat_voltage;    //The battery voltage after converting the voltage divider resistor(mV)
	u8 bat_level;       //Battery percentage charge(%)
    u8 bat_state;       //battery status
    u8 led_state;	    //led indicator light status
}Battery_info_t;

typedef enum
{
	BAT_CHARGE_IDLE = 0,
	BAT_CHARGE_CHARGING,        //Charging
	BAT_CHARGE_FULLY_CHARGED,	//Already full
	BAT_CHARGE_NO_CHARGE,		//Not charged
	BAT_CHARGE_ERROR,			//error condition
}EM_BAT_CHARGE_STATE;

typedef enum
{
	LED_IDLE = 0,			
	LED_CHARGING,		//Charging: Red light
	LED_FULLY_CHARGED,	//Full: Green light
	LED_NO_CHARGE,		//Uncharged
	LED_LOW_POWER,		//Low voltage :0.5HZ red light
}EM_LED_STATE;

esp_err_t stc8_battery_info_get(Battery_info_t *bat_info);

#endif

#ifdef CONFIG_BSP_STC8H1KXX_GPIO_ENABLED
typedef enum
{
    STC8_GPIO_OUT_AUDIO_SD = 0,
    STC8_GPIO_OUT_LCD_BK_POWER,
    STC8_GPIO_OUT_TP_RST,
    STC8_GPIO_OUT_CSI_RST,
    STC8_GPIO_OUT_MAX,
}EM_STC8_GPIO_OUT_NUM;

typedef enum
{
    STC8_GPIO_IN_MAX,
}EM_STC8_GPIO_IN_NUM;

esp_err_t stc8_gpio_get_level(int gpio_num, uint8_t* level);
esp_err_t stc8_gpio_set_level(int gpio_num, uint8_t level);

#endif

#ifdef CONFIG_BSP_STC8H1KXX_PWM_ENABLED
typedef enum
{
    STC8_PWM_1 = 0,
    STC8_PWM_MAX
}EM_STC8_PWM_NUM;

esp_err_t stc8_set_pwm_duty(int pwm_num, uint8_t duty);

#endif
/*———————————————————————————————————————Variable declaration end——————————————-—————————————————————————*/
#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif