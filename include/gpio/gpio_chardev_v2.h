/*
 * Author: Baocheng Su <baocheng.su@siemens.com>
 * Copyright (c) 2026 Siemens AG
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "gpio_chardev.h"
#include "linux/gpio.h"
#include "mraa_internal.h"

int mraa_gpiod_get_lines_request(int chip_fd, unsigned int line_offsets[], unsigned num_lines);
int mraa_gpiod_get_line_name(int chip_fd, unsigned line_offset, char* name, size_t name_size);
int mraa_gpiod_get_line_flags(int chip_fd, unsigned line_offset, unsigned long* flag);

int mraa_gpiod_find_gpio_line_by_name(const char* name, unsigned* chip_number, unsigned* line_number);

int mraa_gpiod_reconfig_lines(int line_req_fd, unsigned long flag);

int mraa_gpiod_get_values(int line_req_fd, unsigned num_lines, unsigned char line_values[]);
int mraa_gpiod_set_values(int line_req_fd, unsigned num_lines, unsigned char line_values[]);

#ifdef __cplusplus
}
#endif
