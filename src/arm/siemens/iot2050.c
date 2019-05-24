/*
 * Author: Le Jin <le.jin@siemens.com>
 * Copyright (c) Siemens AG, 2019
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <mraa/types.h>

#include "common.h"
#include "arm/siemens/iot2050.h"
#include "arm/siemens/platform.h"

#define PINMUX_GROUP_MAIN       (0)
#define PINMUX_GROUP_WAKUP      (1)
#define MAIN_GPIO0_BASE         (0)
#define WKUP_GPIO0_BASE         (186)
#define D4200_GPIO_BASE         (496)
#define D4201_GPIO_BASE         (480)
#define D4202_GPIO_BASE         (464)

typedef struct {
    uint8_t     group;
    uint16_t    index;
    uint16_t    pinmap;
    int8_t      mode[MAX_MUX_REGISTER_MODE];
}regmux_info_t;

static void *pinmux_instance = NULL;
static regmux_info_t pinmux_info[MRAA_IOT2050_PINCOUNT];
static mraa_gpio_context output_en_pins[MRAA_IOT2050_PINCOUNT];
static int pull_en_pins[MRAA_IOT2050_PINCOUNT];

static inline int
iot2050_locate_phy_pin_by_name(mraa_board_t *board, char *pin_name)
{
    int i;

    if(!pin_name)
        return -1;
    for(i=0; i<board->phy_pin_count; i++) {
        if(!strncmp(board->pins[i].name, pin_name, MRAA_PIN_NAME_SIZE)) {
            return i;
        }
    }
    return -1;
}

static inline regmux_info_t*
iot2050_get_regmux_by_pinmap(int pinmap)
{
    int i;

    for(i=0; i<MRAA_IOT2050_PINCOUNT; i++) {
        if((pinmux_info[i].mode[MUX_REGISTER_MODE_GPIO] != -1) && 
            (pinmux_info[i].pinmap == pinmap)) {
            return &pinmux_info[i];
        }
    }
    return NULL;
}

static mraa_result_t
iot2050_setup_mux_register(int phy_pin, int mode)
{
    regmux_info_t *info = &pinmux_info[phy_pin];
    int8_t mux_mode;

    if((phy_pin < 0) || (phy_pin > MRAA_IOT2050_PINCOUNT))
        return MRAA_SUCCESS;
    if((mode < 0) || (mode >= MAX_MUX_REGISTER_MODE)) {
        return MRAA_ERROR_FEATURE_NOT_SUPPORTED;
    }
    if(mode == MUX_REGISTER_MODE_AIO) {
        return MRAA_SUCCESS;
    }
    mux_mode = info->mode[mode];
    if(mux_mode < 0) {
        return MRAA_ERROR_FEATURE_NOT_SUPPORTED;
    }
    syslog(LOG_DEBUG, "REGMUX[phy_pin %d] group %d index %d mode %d\n", phy_pin, info->group, info->index, mux_mode);

    platform_pinmux_select_func(pinmux_instance, info->group, info->index, mux_mode);
    /* Configure as input and output for default */
    platform_pinmux_select_inout(pinmux_instance, info->group, info->index);
    return MRAA_SUCCESS;
}

static mraa_result_t
iot2050_gpio_dir_pre(mraa_gpio_context dev, mraa_gpio_dir_t dir)
{
    int pin = dev->phy_pin;

    if(pin >= 0) {
        if(plat->pins[pin].gpio.complex_cap.complex_pin != 1) {
            return MRAA_SUCCESS;
        }
        if(plat->pins[pin].gpio.complex_cap.output_en == 1) {
            if(output_en_pins[pin] == NULL) {
                output_en_pins[pin] = mraa_gpio_init_raw(plat->pins[pin].gpio.output_enable);
                if(output_en_pins[pin]) {
                    if(mraa_gpio_dir(output_en_pins[pin], MRAA_GPIO_OUT) != MRAA_SUCCESS) {
                        mraa_gpio_close(output_en_pins[pin]);
                        output_en_pins[pin] = NULL;
                        goto failed;
                    }
                } else {
                    goto failed;
                }
            }
            if(dir == MRAA_GPIO_IN) {
                syslog(LOG_DEBUG, "GPIODIR[phy_pin %d] gpio set out en %d to %d\n", pin, plat->pins[pin].gpio.output_enable, !plat->pins[pin].gpio.complex_cap.output_en_high);
                if(mraa_gpio_write(output_en_pins[pin], !plat->pins[pin].gpio.complex_cap.output_en_high) != MRAA_SUCCESS) {
                    goto failed;
                }
            } else {
                syslog(LOG_DEBUG, "GPIODIR[phy_pin %d] gpio set out en %d to %d\n", pin, plat->pins[pin].gpio.output_enable, plat->pins[pin].gpio.complex_cap.output_en_high);
                if(mraa_gpio_write(output_en_pins[pin], plat->pins[pin].gpio.complex_cap.output_en_high) != MRAA_SUCCESS) {
                    goto failed;
                }
            }
        }
    }
    return MRAA_SUCCESS;
failed:
    syslog(LOG_ERR, "iot2050: Error setting gpio direction");
    return MRAA_ERROR_INVALID_RESOURCE;
}

static mraa_result_t
iot2050_gpio_mode_replace(mraa_gpio_context dev, mraa_gpio_mode_t mode)
{
    regmux_info_t *info;
    mraa_gpio_context pull_en_pin;
    mraa_result_t ret = MRAA_SUCCESS;

    /* Only support mode change on interface pins */
    if(dev->phy_pin == -1) {
        ret = MRAA_ERROR_INVALID_RESOURCE;
        goto failed;
    }
    info = iot2050_get_regmux_by_pinmap(dev->pin);
    pull_en_pin = mraa_gpio_init_raw(pull_en_pins[dev->phy_pin]);
    if(pull_en_pin == NULL) {
        ret = MRAA_ERROR_INVALID_RESOURCE;
        goto failed;
    }
    switch(mode) {
        case MRAA_GPIO_PULLUP:
            if(mraa_gpio_dir(pull_en_pin, MRAA_GPIO_OUT_HIGH) != MRAA_SUCCESS) {
                ret = MRAA_ERROR_INVALID_RESOURCE;
                goto failed;
            }
            if(info) {
                platform_pinmux_select_pull_up(pinmux_instance, info->group, info->index);
            }
            break;
        case MRAA_GPIO_PULLDOWN:
            if(mraa_gpio_dir(pull_en_pin, MRAA_GPIO_OUT_LOW) != MRAA_SUCCESS) {
                ret = MRAA_ERROR_INVALID_RESOURCE;
                goto failed;
            }
            if(info) {
                platform_pinmux_select_pull_down(pinmux_instance, info->group, info->index);
            }
            break;
        case MRAA_GPIO_HIZ:
        case MRAA_GPIO_STRONG:
            if(mraa_gpio_dir(pull_en_pin, MRAA_GPIO_IN) != MRAA_SUCCESS) {
                ret = MRAA_ERROR_INVALID_RESOURCE;
                goto failed;
            }
            if(info) {
                platform_pinmux_select_pull_disable(pinmux_instance, info->group, info->index);
            }
            break;
        case MRAA_GPIOD_ACTIVE_LOW:
        case MRAA_GPIOD_OPEN_DRAIN:
        case MRAA_GPIOD_OPEN_SOURCE:
        default:
            ret = MRAA_ERROR_FEATURE_NOT_IMPLEMENTED;
            goto failed;
    }
    mraa_gpio_close(pull_en_pin);
    return ret;
failed:
    syslog(LOG_ERR, "iot2050: Error setting gpio mode");
    if(pull_en_pin) {
        mraa_gpio_close(pull_en_pin);
    }
    return ret;
}

static inline void
iot2050_setup_pins(mraa_board_t *board, int pin_index, char *pin_name, mraa_pincapabilities_t cap, regmux_info_t mux_info)
{
    strncpy(board->pins[pin_index].name, pin_name, MRAA_PIN_NAME_SIZE);
    board->pins[pin_index].capabilities = cap;
    pinmux_info[pin_index] = mux_info;
}

static inline void
iot2050_pin_add_gpio(mraa_board_t *board, int pin_index, int chip, int line,
                        int output_en_pin, int pull_en_pin, mraa_mux_t *pin_mux, int num_pinmux)
{
    int i;

    mraa_pininfo_t *pininfo = &(board->pins[pin_index]);
    /*for sysfs*/
    pininfo->gpio.pinmap = pinmux_info[pin_index].pinmap;
    pininfo->gpio.mux_total = 0;
    /*for gpio chardev*/
    pininfo->gpio.gpio_chip = chip;
    pininfo->gpio.gpio_line = line;
    for(i=0; i<num_pinmux; i++) {
        pininfo->gpio.mux[i] = pin_mux[i];
    }
    pininfo->gpio.mux_total = num_pinmux;
    /*output enable pin*/
    if(output_en_pin != -1)
    {
        pininfo->gpio.complex_cap = (mraa_pin_cap_complex_t){ 1, 1, 1, 0, 0 };
        pininfo->gpio.output_enable = output_en_pin;
        pininfo->gpio.complex_cap.output_en_high = 1;
    }
    /*pull enabled pin*/
    pull_en_pins[pin_index] = pull_en_pin;
    board->gpio_count++;
}

static inline void
iot2050_pin_add_aio(mraa_board_t *board, int pin_index, int channel, mraa_mux_t *pin_mux, int num_pinmux)
{
    int i;

    mraa_pininfo_t *pininfo = &(board->pins[pin_index]);
    pininfo->aio.pinmap = channel;
    for(i=0; i<num_pinmux; i++) {
        pininfo->aio.mux[i] = pin_mux[i];
    }
    pininfo->aio.mux_total = num_pinmux;
    board->aio_dev[channel].pin = pin_index;
    board->aio_count++;
}

static inline void
iot2050_pin_add_uart(mraa_board_t *board, int pin_index, int parent_id, mraa_mux_t *pin_mux, int num_pinmux)
{
    int i;

    mraa_pininfo_t *pininfo = &(board->pins[pin_index]);
    pininfo->uart.parent_id = parent_id;
    for(i=0; i<num_pinmux; i++) {
        pininfo->uart.mux[i] = pin_mux[i];
    }
    pininfo->uart.mux_total = num_pinmux;
}

static inline void
iot2050_pin_add_i2c(mraa_board_t *board, int pin_index, int parent_id, mraa_mux_t *pin_mux, int num_pinmux)
{
    int i;

    mraa_pininfo_t *pininfo = &(board->pins[pin_index]);
    pininfo->i2c.parent_id = parent_id;
    for(i=0; i<num_pinmux; i++) {
        pininfo->i2c.mux[i] = pin_mux[i];
    }
    pininfo->i2c.mux_total = num_pinmux;
}

static inline void
iot2050_pin_add_spi(mraa_board_t *board, int pin_index, int parent_id, mraa_mux_t *pin_mux, int num_pinmux)
{
    int i;

    mraa_pininfo_t *pininfo = &(board->pins[pin_index]);
    pininfo->spi.parent_id = parent_id;
    for(i=0; i<num_pinmux; i++) {
        pininfo->spi.mux[i] = pin_mux[i];
    }
    pininfo->spi.mux_total = num_pinmux;
}

static inline void
iot2050_pin_add_pwm(mraa_board_t *board, int pin_index, int parent_id, int pinmap, mraa_mux_t *pin_mux, int num_pinmux)
{
    int i;

    mraa_pininfo_t *pininfo = &(board->pins[pin_index]);
    pininfo->pwm.parent_id = parent_id;
    pininfo->pwm.pinmap = pinmap;
    for(i=0; i<num_pinmux; i++) {
        pininfo->pwm.mux[i] = pin_mux[i];
    }
    pininfo->pwm.mux_total = num_pinmux;
}


static inline void
iot2050_setup_uart(mraa_board_t *board, char *name, char *path, char *rx_pin_name, char *tx_pin_name, char *cts_pin_name, char *rts_pin_name)
{
    board->uart_dev[board->uart_dev_count].name = name;
    board->uart_dev[board->uart_dev_count].device_path = path;
    board->uart_dev[board->uart_dev_count].tx = iot2050_locate_phy_pin_by_name(board, tx_pin_name);
    board->uart_dev[board->uart_dev_count].rx = iot2050_locate_phy_pin_by_name(board, rx_pin_name);
    board->uart_dev[board->uart_dev_count].rts = iot2050_locate_phy_pin_by_name(board, rts_pin_name);
    board->uart_dev[board->uart_dev_count].cts = iot2050_locate_phy_pin_by_name(board, cts_pin_name);
    board->uart_dev_count++;
}

static inline void
iot2050_setup_i2c(mraa_board_t *board, char *name, int bus_id, char *sda_pin_name, char *scl_pin_name)
{
    board->i2c_bus[board->i2c_bus_count].name = name;
    board->i2c_bus[board->i2c_bus_count].bus_id = bus_id;
    board->i2c_bus[board->i2c_bus_count].sda = iot2050_locate_phy_pin_by_name(board, sda_pin_name);
    board->i2c_bus[board->i2c_bus_count].scl = iot2050_locate_phy_pin_by_name(board, scl_pin_name);
    board->i2c_bus_count++;
}

static inline void
iot2050_setup_spi(mraa_board_t *board, char *name, int bus_id, int cs_id,
                                        char *sclk_pin_name, char *mosi_pin_name,
                                        char *miso_pin_name, char *cs_pin_name)
{
    board->spi_bus[board->spi_bus_count].name = name;
    board->spi_bus[board->spi_bus_count].bus_id = bus_id;
    board->spi_bus[board->spi_bus_count].slave_s = cs_id;
    board->spi_bus[board->spi_bus_count].sclk = iot2050_locate_phy_pin_by_name(board, sclk_pin_name);
    board->spi_bus[board->spi_bus_count].mosi = iot2050_locate_phy_pin_by_name(board, mosi_pin_name);
    board->spi_bus[board->spi_bus_count].miso = iot2050_locate_phy_pin_by_name(board, miso_pin_name);
    board->spi_bus[board->spi_bus_count].cs = iot2050_locate_phy_pin_by_name(board, cs_pin_name);
    board->spi_bus_count++;
}

static inline void
iot2050_setup_pwm(mraa_board_t *board, char *name)
{
    board->pwm_dev[board->pwm_dev_count].name = name;
    board->pwm_dev_count++;
}

static inline void
iot2050_setup_led(mraa_board_t *board, char *name)
{
    board->led_dev[board->led_dev_count].name = name;
    board->led_dev_count++;
}

mraa_board_t*
mraa_siemens_iot2050()
{
    mraa_board_t* b = (mraa_board_t*) calloc(1, sizeof(mraa_board_t));
    mraa_mux_t mux_info[6];
    int pin_index = 0;

    if(b == NULL) {
        return NULL;
    }
    memset(output_en_pins, 0, sizeof(mraa_gpio_context) * MRAA_IOT2050_PINCOUNT);
    b->platform_name = PLATFORM_NAME;
    b->phy_pin_count = MRAA_IOT2050_PINCOUNT;
    b->chardev_capable = 0;
    b->adc_raw = 12;
    b->adc_supported = 12;
    b->pwm_default_period = 1000; /*us*/
    b->pwm_max_period = 939509;
    b->pwm_min_period = 1;
    b->aio_non_seq = 1;
    b->adv_func = (mraa_adv_func_t*) calloc(1, sizeof(mraa_adv_func_t));
    if(b->adv_func == NULL) {
        goto error;
    }
    b->adv_func->gpio_dir_pre = &iot2050_gpio_dir_pre;
    b->adv_func->gpio_mode_replace = &iot2050_gpio_mode_replace;
    b->adv_func->setup_mux_register = &iot2050_setup_mux_register;
    b->pins = (mraa_pininfo_t*) calloc(MRAA_IOT2050_PINCOUNT, sizeof(mraa_pininfo_t));
    if(b->pins == NULL) {
        free(b->adv_func);
        goto error;
    }
    pinmux_instance = platfrom_pinmux_get_instance("iot2050");
    /* IO */
    iot2050_setup_pins(b, pin_index, "IO0",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            1}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            17,
                            WKUP_GPIO0_BASE+29,
                            {
                                7,  /*GPIO*/
                                4,  /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                -1  /*PWM*/
                            }
                        });

    iot2050_pin_add_gpio(b, pin_index, 2, 29, D4201_GPIO_BASE+0, D4202_GPIO_BASE+0, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+0;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_LOW;
    mux_info[1].pin = D4202_GPIO_BASE+0;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_uart(b, pin_index, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO1",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            1}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            18,
                            WKUP_GPIO0_BASE+30,
                            {
                                7,  /*GPIO*/
                                4,  /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 2, 30, D4201_GPIO_BASE+1, D4202_GPIO_BASE+1, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+1;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+1;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_uart(b, pin_index, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO2",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            1}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            19,
                            WKUP_GPIO0_BASE+31,
                            {
                                7,  /*GPIO*/
                                4,  /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 2, 31, D4201_GPIO_BASE+2, D4202_GPIO_BASE+2, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+2;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_LOW;
    mux_info[1].pin = D4202_GPIO_BASE+2;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_uart(b, pin_index, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO3",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            1}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            21,
                            WKUP_GPIO0_BASE+33,
                            {
                                7,  /*GPIO*/
                                4,  /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 2, 33, D4201_GPIO_BASE+3, D4202_GPIO_BASE+3, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+3;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+3;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_uart(b, pin_index, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO4",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            1,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_MAIN,
                            33,
                            MAIN_GPIO0_BASE+33,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                5   /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 0, 33, D4201_GPIO_BASE+4, D4202_GPIO_BASE+4, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+4;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+4;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_pwm(b, pin_index, 0, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO5",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            1,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_MAIN,
                            35,
                            MAIN_GPIO0_BASE+35,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                5   /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 0, 35, D4201_GPIO_BASE+5, D4202_GPIO_BASE+5, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+5;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+5;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_pwm(b, pin_index, 2, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO6",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            1,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_MAIN,
                            38,
                            MAIN_GPIO0_BASE+38,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                5   /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 0, 38, D4201_GPIO_BASE+6, D4202_GPIO_BASE+6, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+6;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+6;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_pwm(b, pin_index, 4, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO7",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            1,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_MAIN,
                            43,
                            MAIN_GPIO0_BASE+43,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                5   /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 0, 43, D4201_GPIO_BASE+7, D4202_GPIO_BASE+7, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+7;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+7;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_pwm(b, pin_index, 6, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO8",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            1,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_MAIN,
                            48,
                            MAIN_GPIO0_BASE+48,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                5   /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 0, 48, D4201_GPIO_BASE+8, D4202_GPIO_BASE+8, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+8;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+8;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_pwm(b, pin_index, 8, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO9",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            1,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_MAIN,
                            51,
                            MAIN_GPIO0_BASE+51,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                5   /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 0, 51, D4201_GPIO_BASE+9, D4202_GPIO_BASE+9, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+9;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+9;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_pwm(b, pin_index, 10, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO10",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            1,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            39,
                            WKUP_GPIO0_BASE+51,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                0, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 2, 51, D4201_GPIO_BASE+10, D4202_GPIO_BASE+10, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+10;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+10;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_spi(b, pin_index, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO11",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            1,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            37,
                            WKUP_GPIO0_BASE+49,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                0, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 2, 49, D4201_GPIO_BASE+11, D4202_GPIO_BASE+11, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+11;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+11;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_spi(b, pin_index, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO12",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            1,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            38,
                            WKUP_GPIO0_BASE+50,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                0, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 2, 50, D4201_GPIO_BASE+12, D4202_GPIO_BASE+12, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+12;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_LOW;
    mux_info[1].pin = D4202_GPIO_BASE+12;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_spi(b, pin_index, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "IO13",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            1,  /*spi*/
                            0,  /*i2c*/
                            0,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            36,
                            WKUP_GPIO0_BASE+48,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                0, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    iot2050_pin_add_gpio(b, pin_index, 2, 48, D4201_GPIO_BASE+13, D4202_GPIO_BASE+13, NULL, 0);
    mux_info[0].pin = D4201_GPIO_BASE+13;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_HIGH;
    mux_info[1].pin = D4202_GPIO_BASE+13;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    iot2050_pin_add_spi(b, pin_index, 0, mux_info, 2);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "A0",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            1,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            33,
                            WKUP_GPIO0_BASE+45,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    mux_info[0].pin = D4200_GPIO_BASE+8;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_LOW;
    iot2050_pin_add_gpio(b, pin_index, 2, 45, WKUP_GPIO0_BASE+38, D4200_GPIO_BASE+0, mux_info, 1);
    // D/A switch
    mux_info[0].pin = D4200_GPIO_BASE+8;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_IN;
    // muxed GPIO as input
    mux_info[1].pin = WKUP_GPIO0_BASE+45;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    // output enable as input
    mux_info[2].pin = WKUP_GPIO0_BASE+38;
    mux_info[2].pincmd = PINCMD_SET_DIRECTION;
    mux_info[2].value = MRAA_GPIO_OUT_LOW;
    // pull enable as input
    mux_info[3].pin = D4200_GPIO_BASE+0;
    mux_info[3].pincmd = PINCMD_SET_DIRECTION;
    mux_info[3].value = MRAA_GPIO_IN;
    iot2050_pin_add_aio(b, pin_index, 0, mux_info, 4);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "A1",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            1,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            32,
                            WKUP_GPIO0_BASE+44,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    mux_info[0].pin = D4200_GPIO_BASE+9;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_LOW;
    iot2050_pin_add_gpio(b, pin_index, 2, 44, WKUP_GPIO0_BASE+37, D4200_GPIO_BASE+1, mux_info, 1);
    // D/A switch
    mux_info[0].pin = D4200_GPIO_BASE+9;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_IN;
    // muxed GPIO as input
    mux_info[1].pin = WKUP_GPIO0_BASE+44;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    // output enable as input
    mux_info[2].pin = WKUP_GPIO0_BASE+37;
    mux_info[2].pincmd = PINCMD_SET_DIRECTION;
    mux_info[2].value = MRAA_GPIO_OUT_LOW;
    // pull enable as input
    mux_info[3].pin = D4200_GPIO_BASE+1;
    mux_info[3].pincmd = PINCMD_SET_DIRECTION;
    mux_info[3].value = MRAA_GPIO_IN;
    iot2050_pin_add_aio(b, pin_index, 1, mux_info, 4);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "A2",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            1,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            31,
                            WKUP_GPIO0_BASE+43,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    mux_info[0].pin = D4200_GPIO_BASE+10;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_LOW;
    iot2050_pin_add_gpio(b, pin_index, 2, 43, WKUP_GPIO0_BASE+36, D4200_GPIO_BASE+2, mux_info, 1);
    // D/A switch
    mux_info[0].pin = D4200_GPIO_BASE+10;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_IN;
    // muxed GPIO as input
    mux_info[1].pin = WKUP_GPIO0_BASE+43;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    // output enable as input
    mux_info[2].pin = WKUP_GPIO0_BASE+36;
    mux_info[2].pincmd = PINCMD_SET_DIRECTION;
    mux_info[2].value = MRAA_GPIO_OUT_LOW;
    // pull enable as input
    mux_info[3].pin = D4200_GPIO_BASE+2;
    mux_info[3].pincmd = PINCMD_SET_DIRECTION;
    mux_info[3].value = MRAA_GPIO_IN;
    iot2050_pin_add_aio(b, pin_index, 2, mux_info, 4);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "A3",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            0,  /*i2c*/
                            1,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            27,
                            WKUP_GPIO0_BASE+39,
                            {
                                7,  /*GPIO*/
                                -1, /*UART*/
                                -1, /*I2C*/
                                -1, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    mux_info[0].pin = D4200_GPIO_BASE+11;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_LOW;
    iot2050_pin_add_gpio(b, pin_index, 2, 39, WKUP_GPIO0_BASE+34, D4200_GPIO_BASE+3, mux_info, 1);
    // D/A switch
    mux_info[0].pin = D4200_GPIO_BASE+11;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_IN;
    // muxed GPIO as input
    mux_info[1].pin = WKUP_GPIO0_BASE+39;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_IN;
    // output enable as input
    mux_info[2].pin = WKUP_GPIO0_BASE+34;
    mux_info[2].pincmd = PINCMD_SET_DIRECTION;
    mux_info[2].value = MRAA_GPIO_OUT_LOW;
    // pull enable as input
    mux_info[3].pin = D4200_GPIO_BASE+3;
    mux_info[3].pincmd = PINCMD_SET_DIRECTION;
    mux_info[3].value = MRAA_GPIO_IN;
    iot2050_pin_add_aio(b, pin_index, 3, mux_info, 4);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "A4",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            1,  /*i2c*/
                            1,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            30,
                            WKUP_GPIO0_BASE+42,
                            {
                                7, /*GPIO*/
                                -1, /*UART*/
                                0,  /*I2C*/
                                -1, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    mux_info[0].pin = D4200_GPIO_BASE+12;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_LOW;
    mux_info[1].pin = WKUP_GPIO0_BASE+21;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_OUT_HIGH;
    iot2050_pin_add_gpio(b, pin_index, 2, 42, WKUP_GPIO0_BASE+41, D4200_GPIO_BASE+4, mux_info, 2);
    // A/D/I switch
    mux_info[0].pin = D4200_GPIO_BASE+12;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_IN;
    mux_info[1].pin = WKUP_GPIO0_BASE+21;
    mux_info[1].pincmd = PINCMD_SET_OUT_VALUE;
    mux_info[1].value = 0;
    // pull enable as input
    mux_info[2].pin = D4200_GPIO_BASE+4;
    mux_info[2].pincmd = PINCMD_SET_DIRECTION;
    mux_info[2].value = MRAA_GPIO_IN;
    iot2050_pin_add_i2c(b, pin_index, 0, mux_info, 3);
    // A/D/I switch
    mux_info[0].pin = D4200_GPIO_BASE+12;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_IN;
    mux_info[1].pin = WKUP_GPIO0_BASE+21;
    mux_info[1].pincmd = PINCMD_SET_OUT_VALUE;
    mux_info[1].value = 1;
    // muxed GPIO as input
    mux_info[2].pin = WKUP_GPIO0_BASE+42;
    mux_info[2].pincmd = PINCMD_SET_DIRECTION;
    mux_info[2].value = MRAA_GPIO_IN;
    // output enable as input
    mux_info[3].pin = WKUP_GPIO0_BASE+41;
    mux_info[3].pincmd = PINCMD_SET_DIRECTION;
    mux_info[3].value = MRAA_GPIO_OUT_LOW;
    // pull enable as input
    mux_info[4].pin = D4200_GPIO_BASE+4;
    mux_info[4].pincmd = PINCMD_SET_DIRECTION;
    mux_info[4].value = MRAA_GPIO_IN;
    iot2050_pin_add_aio(b, pin_index, 4, mux_info, 5);
    pin_index++;

    iot2050_setup_pins(b, pin_index, "A5",
                        (mraa_pincapabilities_t) {
                            1,  /*valid*/
                            1,  /*gpio*/
                            0,  /*pwm*/
                            0,  /*fast gpio*/
                            0,  /*spi*/
                            1,  /*i2c*/
                            1,  /*aio*/
                            0}, /*uart*/
                        (regmux_info_t) {
                            PINMUX_GROUP_WAKUP,
                            23,
                            WKUP_GPIO0_BASE+35,
                            {
                                7, /*GPIO*/
                                -1, /*UART*/
                                0,  /*I2C*/
                                -1, /*SPI*/
                                -1  /*PWM*/
                            }
                        });
    mux_info[0].pin = D4200_GPIO_BASE+13;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_OUT_LOW;
    mux_info[1].pin = WKUP_GPIO0_BASE+21;
    mux_info[1].pincmd = PINCMD_SET_DIRECTION;
    mux_info[1].value = MRAA_GPIO_OUT_HIGH;
    iot2050_pin_add_gpio(b, pin_index, 2, 35, D4201_GPIO_BASE+14, D4200_GPIO_BASE+5, mux_info, 2);
    // A/D/I switch
    mux_info[0].pin = D4200_GPIO_BASE+13;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_IN;
    mux_info[1].pin = WKUP_GPIO0_BASE+21;
    mux_info[1].pincmd = PINCMD_SET_OUT_VALUE;
    mux_info[1].value = 0;
    // pull enable as input
    mux_info[2].pin = D4200_GPIO_BASE+5;
    mux_info[2].pincmd = PINCMD_SET_DIRECTION;
    mux_info[2].value = MRAA_GPIO_IN;
    iot2050_pin_add_i2c(b, pin_index, 0, mux_info, 3);
    // A/D/I switch
    mux_info[0].pin = D4200_GPIO_BASE+13;
    mux_info[0].pincmd = PINCMD_SET_DIRECTION;
    mux_info[0].value = MRAA_GPIO_IN;
    mux_info[1].pin = WKUP_GPIO0_BASE+21;
    mux_info[1].pincmd = PINCMD_SET_OUT_VALUE;
    mux_info[1].value = 1;
    // muxed GPIO as input
    mux_info[2].pin = WKUP_GPIO0_BASE+35;
    mux_info[2].pincmd = PINCMD_SET_DIRECTION;
    mux_info[2].value = MRAA_GPIO_IN;
    // output enable as input
    mux_info[3].pin = D4201_GPIO_BASE+14;
    mux_info[3].pincmd = PINCMD_SET_DIRECTION;
    mux_info[3].value = MRAA_GPIO_OUT_LOW;
    // pull enable as input
    mux_info[4].pin = D4200_GPIO_BASE+5;
    mux_info[4].pincmd = PINCMD_SET_DIRECTION;
    mux_info[4].value = MRAA_GPIO_IN;
    iot2050_pin_add_aio(b, pin_index, 5, mux_info, 5);
    pin_index++;

    /* UART */
    iot2050_setup_uart(b, "UART0", "/dev/ttyS1", "IO0", "IO1", "IO2", "IO3");
    b->def_uart_dev = 0;
    /* I2C */
    iot2050_setup_i2c(b, "I2C0", 4, "A4", "A5");
    b->def_i2c_bus = 0;
    /* SPI */
    iot2050_setup_spi(b, "SPI0", 0, 0, "IO13", "IO11", "IO12", "IO10");
    b->def_spi_bus = 0;
    /* PWM */
    iot2050_setup_pwm(b, "PWM0");
    iot2050_setup_pwm(b, "PWM1");
    iot2050_setup_pwm(b, "PWM2");
    iot2050_setup_pwm(b, "PWM3");
    iot2050_setup_pwm(b, "PWM4");
    iot2050_setup_pwm(b, "PWM5");
    b->def_pwm_dev = 0;
    /* LED */
    iot2050_setup_led(b, "user-led0-green");
    iot2050_setup_led(b, "user-led0-red");
    iot2050_setup_led(b, "user-led1-green");
    iot2050_setup_led(b, "user-led1-red");
    return b;
error:
    syslog(LOG_CRIT, "iot2050: Platform failed to initialise");
    free(b);
    return NULL;
}
