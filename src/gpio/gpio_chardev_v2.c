/*
 * Author: Baocheng Su <baocheng.su@siemens.com>
 * Copyright (c) 2026 Siemens AG
 *
 * SPDX-License-Identifier: MIT
 */

#include "gpio/gpio_chardev_v2.h"
#include "linux/gpio.h"
#include "mraa_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int
mraa_gpiod_get_lines_request(int chip_fd, unsigned int line_offsets[], unsigned num_lines)
{
    struct gpio_v2_line_config config;
    struct gpio_v2_line_request req;

    memset(&config, 0, sizeof(config));
    memset(&req, 0, sizeof(req));
    // config.flags = GPIO_V2_LINE_FLAG_INPUT;
    req.config = config;
    strcpy(req.consumer, "mraa");
    req.num_lines = num_lines;
    memcpy(req.offsets, line_offsets, num_lines * sizeof(unsigned int));

    int status = _mraa_gpiod_ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req);
    if (status < 0) {
        syslog(LOG_ERR, "gpiod: ioctl() GPIO_V2_GET_LINE_IOCTL fail");
        return status;
    }

    return req.fd;
}

int
mraa_gpiod_get_line_name(int chip_fd, unsigned line_offset, char* name, size_t name_size)
{
    struct gpio_v2_line_info info;

    memset(&info, 0, sizeof(info));
    info.offset = line_offset;

    int status = _mraa_gpiod_ioctl(chip_fd, GPIO_V2_GET_LINEINFO_IOCTL, &info);
    if (status < 0) {
        syslog(LOG_ERR, "[GPIOD_INTERFACE]: ioctl() GPIO_V2_GET_LINEINFO_IOCTL fail");
        return status;
    }

    strncpy(name, info.name, name_size);
    name[name_size - 1] = '\0';

    return status;
}

int
mraa_gpiod_get_line_flags(int chip_fd, unsigned line_offset, unsigned long* flag)
{
    struct gpio_v2_line_info info;

    memset(&info, 0, sizeof(info));
    info.offset = line_offset;

    int status = _mraa_gpiod_ioctl(chip_fd, GPIO_V2_GET_LINEINFO_IOCTL, &info);
    if (status < 0) {
        syslog(LOG_ERR, "[GPIOD_INTERFACE]: ioctl() GPIO_V2_GET_LINEINFO_IOCTL fail");
    }

    syslog(LOG_ERR, "[GPIOD_INTERFACE]: line_offset %u, flags 0x%llx", line_offset, info.flags);
    syslog(LOG_ERR, "[GPIOD_INTERFACE]: line_offset %u, consumer %s", line_offset, info.consumer);
    syslog(LOG_ERR, "[GPIOD_INTERFACE]: line_offset %u, name %s", line_offset, info.name);

    *flag = info.flags;

    return status;
}


int
mraa_gpiod_find_gpio_line_by_name(const char* name, unsigned* chip_number, unsigned* line_number)
{
    char line_name[GPIO_MAX_NAME_SIZE];
    mraa_gpiod_chip_info** cinfos;
    mraa_gpiod_chip_info* cinfo;
    int ret = -1;

    int num_chips = mraa_get_chip_infos(&cinfos);
    if (num_chips < 0) {
        return -1;
    }

    for_each_gpio_chip(cinfo, cinfos, num_chips)
    {
        for (int i = 0; i < cinfo->chip_info.lines; i++) {
            ret = mraa_gpiod_get_line_name(cinfo->chip_fd, i, line_name, GPIO_MAX_NAME_SIZE);
            if (ret < 0) {
                syslog(LOG_ERR, "[GPIOD_INTERFACE]: error getting line name for chip %d line %d", idx, i);
                continue;
            }

            if (!strncmp(line_name, name, GPIO_MAX_NAME_SIZE)) {
                if (chip_number) {
                    /* idx is coming from `for_each_gpio_chip` definition */
                    *chip_number = idx;
                }

                if (line_number) {
                    *line_number = i;
                }

                ret = 0;
                goto out;
            }

        }
    }

out:
    for_each_gpio_chip(cinfo, cinfos, num_chips)
    {
        if (cinfo)
            close(cinfo->chip_fd);
    }
    free(cinfos);
    return ret;
}

int
mraa_gpiod_get_values(int line_req_fd, unsigned num_lines, unsigned char line_values[])
{
    struct gpio_v2_line_values values;

    memset(&values, 0, sizeof(values));

    for (unsigned i = 0; i < num_lines; i++) {
        values.mask |= (1ULL << i);
    }

    int status = _mraa_gpiod_ioctl(line_req_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values);
    if (status < 0) {
        syslog(LOG_ERR, "[GPIOD_INTERFACE]: ioctl() GPIO_V2_LINE_GET_VALUES_IOCTL fail");
        return status;
    }

    for (unsigned i = 0; i < num_lines; i++) {
        line_values[i] = (values.bits & (1ULL << i)) ? 1 : 0;
    }

    return status;
}

int
mraa_gpiod_set_values(int line_req_fd, unsigned num_lines, unsigned char line_values[])
{
    struct gpio_v2_line_values values;

    memset(&values, 0, sizeof(values));
    for (unsigned i = 0; i < num_lines; i++) {
        if (line_values[i]) {
            values.bits |= (1ULL << i);
        }
        values.mask |= (1ULL << i);
    }

    int status = _mraa_gpiod_ioctl(line_req_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
    if (status < 0) {
        syslog(LOG_ERR, "[GPIOD_INTERFACE]: ioctl() GPIO_V2_LINE_SET_VALUES_IOCTL fail");
        return status;
    }

    return status;
}

int
mraa_gpiod_reconfig_lines(int line_req_fd, unsigned long flag)
{
    struct gpio_v2_line_config config;

    memset(&config, 0, sizeof(config));
    config.flags = flag;

    syslog(LOG_ERR, "[GPIOD_INTERFACE]: reconfiguring line flags: 0x%lx", flag);

    int status = _mraa_gpiod_ioctl(line_req_fd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &config);
    if (status < 0) {
        syslog(LOG_ERR, "[GPIOD_INTERFACE]: ioctl() GPIO_V2_LINE_SET_CONFIG_IOCTL fail");
        return status;
    }

    return status;
}

