/*————————————————————————————————————————Header file declaration————————————————————————————————————————*/
#include "bsp_stc8h1kxx.h"
/*——————————————————————————————————————Header file declaration end——————————————————————————————————————*/

/*——————————————————————————————————————————Variable declaration—————————————————————————————————————————*/
#ifdef CONFIG_BSP_STC8H1KXX_ENABLED
typedef enum
{
    STC8_REG_ADDR_BATTERY   = 0x00,
    STC8_REG_ADDR_GET_GPIO  = 0x10,
    STC8_REG_ADDR_SET_GPIO  = 0x18,
    STC8_REG_ADDR_SET_PWM   = 0x20,
}EM_STC8_REG_ADDR;

static i2c_master_dev_handle_t stc8_handle = NULL;
#endif
/*————————————————————————————————————————Variable declaration end———————————————————————————————————————*/

/*—————————————————————————————————————————Functional function———————————————————————————————————————————*/

i2c_master_dev_handle_t i2c_dev_register(uint16_t dev_device_address)
{
    esp_err_t err = ESP_OK;
    i2c_master_dev_handle_t dev_handle = NULL;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_device_address,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &dev_handle);
    if (err == ESP_OK)
        return dev_handle;
    return 0;
}
esp_err_t i2c_read_reg(i2c_master_dev_handle_t i2c_dev, uint8_t reg_addr, uint8_t *read_buffer, size_t read_size)
{
    return i2c_master_transmit_receive(i2c_dev, &reg_addr, 1, read_buffer, read_size, 1000);
}

esp_err_t i2c_write_reg(i2c_master_dev_handle_t i2c_dev, uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_transmit(i2c_dev, write_buf, sizeof(write_buf), 1000);
}


#ifdef CONFIG_BSP_STC8H1KXX_ENABLED

esp_err_t stc8_i2c_init()
{
    stc8_handle = i2c_dev_register(BSP_STC8H1KXX_I2C_ADDRESS);
    if (stc8_handle == NULL)
    {
        STC8H1KXX_ERROR("stc8 i2c register fail");
        return ESP_FAIL;
    }
    return ESP_OK;
}

#ifdef CONFIG_BSP_STC8H1KXX_BATTERY_ENABLED

esp_err_t stc8_battery_info_get(Battery_info_t *bat_info)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < sizeof(Battery_info_t); i++)
    {
        err = i2c_read_reg(stc8_handle, STC8_REG_ADDR_BATTERY+i, (uint8_t*)bat_info+i, 1);
        if (ESP_OK != err)
        {
            STC8H1KXX_ERROR("stc8 read battery info fail");
            return err;
        }
    }
    return err;
}

#endif

#ifdef CONFIG_BSP_STC8H1KXX_GPIO_ENABLED

esp_err_t stc8_gpio_get_level(int gpio_num, uint8_t* level)
{
    esp_err_t err;
    if (STC8_GPIO_IN_MAX <= gpio_num) {
        STC8H1KXX_ERROR("stc8 can't get gpio %d", gpio_num);
        return ESP_FAIL;
    }    
    err = i2c_read_reg(stc8_handle, STC8_REG_ADDR_GET_GPIO + gpio_num, level, 1);
    if (ESP_OK != err)
    {
        STC8H1KXX_ERROR("stc8 get gpio %d fail", gpio_num);
        return err;
    }
    return err;
}

esp_err_t stc8_gpio_set_level(int gpio_num, uint8_t level)
{
    esp_err_t err;
    if (STC8_GPIO_OUT_MAX <= gpio_num) {
        STC8H1KXX_ERROR("stc8 can't set gpio %d", gpio_num);
        return ESP_FAIL;
    }
    err = i2c_write_reg(stc8_handle, STC8_REG_ADDR_SET_GPIO + gpio_num, level);
    if (ESP_OK != err)
    {
        STC8H1KXX_ERROR("stc8 set gpio %d fail", gpio_num);
        return err;
    }
    return err;
}

#endif

#ifdef CONFIG_BSP_STC8H1KXX_PWM_ENABLED

esp_err_t stc8_set_pwm_duty(int pwm_num, uint8_t duty)
{
    esp_err_t err;
    if (STC8_PWM_MAX <= pwm_num) {
        STC8H1KXX_ERROR("stc8 don't have pwm %d", pwm_num);
        return false;
    }
    err = i2c_write_reg(stc8_handle, STC8_REG_ADDR_SET_PWM + pwm_num, duty);
    if (ESP_OK != err)
    {
        STC8H1KXX_ERROR("stc8 set pwm %d fail", pwm_num);
        return err;
    }
    return err;
}

#endif

#endif
/*———————————————————————————————————————Functional function end—————————————————————————————————————————*/