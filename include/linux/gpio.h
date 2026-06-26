/*
 * This header was manually generated from a Linux kernel header linux/gpio.h,
 * to make information necessary for compilation to be available. It contains
 * only constants, structures, and macros generated from the original header,
 * and thus, contains no copyrightable information.
 */
#ifndef _GPIO_H_
#define _GPIO_H_

#include <linux/ioctl.h>
#include <linux/types.h>

struct gpiochip_info {
    char name[32];
    char label[32];
    __u32 lines;
};

#define GPIOLINE_FLAG_KERNEL            (1UL << 0)
#define GPIOLINE_FLAG_IS_OUT            (1UL << 1)
#define GPIOLINE_FLAG_ACTIVE_LOW        (1UL << 2)
#define GPIOLINE_FLAG_OPEN_DRAIN        (1UL << 3)
#define GPIOLINE_FLAG_OPEN_SOURCE       (1UL << 4)

struct gpioline_info {
    __u32 line_offset;
    __u32 flags;
    char name[32];
    char consumer[32];
};

#define GPIOHANDLES_MAX 64

#define GPIOHANDLE_REQUEST_INPUT        (1UL << 0)
#define GPIOHANDLE_REQUEST_OUTPUT       (1UL << 1)
#define GPIOHANDLE_REQUEST_ACTIVE_LOW   (1UL << 2)
#define GPIOHANDLE_REQUEST_OPEN_DRAIN   (1UL << 3)
#define GPIOHANDLE_REQUEST_OPEN_SOURCE  (1UL << 4)

struct gpiohandle_request {
    __u32 lineoffsets[GPIOHANDLES_MAX];
    __u32 flags;
    __u8 default_values[GPIOHANDLES_MAX];
    char consumer_label[32];
    __u32 lines;
    int fd;
};

struct gpiohandle_data {
    __u8 values[GPIOHANDLES_MAX];
};

#define GPIOHANDLE_GET_LINE_VALUES_IOCTL _IOWR(0xB4, 0x08, struct gpiohandle_data)
#define GPIOHANDLE_SET_LINE_VALUES_IOCTL _IOWR(0xB4, 0x09, struct gpiohandle_data)

#define GPIOEVENT_REQUEST_RISING_EDGE   (1UL << 0)
#define GPIOEVENT_REQUEST_FALLING_EDGE  (1UL << 1)
#define GPIOEVENT_REQUEST_BOTH_EDGES    ((1UL << 0) | (1UL << 1))

struct gpioevent_request {
    __u32 lineoffset;
    __u32 handleflags;
    __u32 eventflags;
    char consumer_label[32];
    int fd;
};

#define GPIOEVENT_EVENT_RISING_EDGE 0x01
#define GPIOEVENT_EVENT_FALLING_EDGE 0x02

struct gpioevent_data {
    __u64 timestamp;
    __u32 id;
};

#define GPIO_GET_CHIPINFO_IOCTL _IOR(0xB4, 0x01, struct gpiochip_info)
#define GPIO_GET_LINEINFO_IOCTL _IOWR(0xB4, 0x02, struct gpioline_info)
#define GPIO_GET_LINEHANDLE_IOCTL _IOWR(0xB4, 0x03, struct gpiohandle_request)
#define GPIO_GET_LINEEVENT_IOCTL _IOWR(0xB4, 0x04, struct gpioevent_request)

/*
 * GPIO v2 UAPI definitions (Linux 5.10+)
 * Based on the kernel's include/uapi/linux/gpio.h
 */
#define GPIO_V2_LINE_FLAG_INPUT                (1UL << 0)
#define GPIO_V2_LINE_FLAG_OUTPUT               (1UL << 1)
#define GPIO_V2_LINE_FLAG_ACTIVE_LOW           (1UL << 2)
#define GPIO_V2_LINE_FLAG_OPEN_DRAIN           (1UL << 3)
#define GPIO_V2_LINE_FLAG_OPEN_SOURCE          (1UL << 4)
#define GPIO_V2_LINE_FLAG_BIAS_PULL_UP         (1UL << 5)
#define GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN       (1UL << 6)
#define GPIO_V2_LINE_FLAG_BIAS_DISABLED        (1UL << 7)
#define GPIO_V2_LINE_FLAG_EDGE_RISING          (1UL << 8)
#define GPIO_V2_LINE_FLAG_EDGE_FALLING         (1UL << 9)

#define GPIO_V2_LINES_MAX 64

struct gpio_v2_line_values {
    __u64 bits;
    __u64 mask;
};

struct gpio_v2_line_attribute {
    __u32 id;
    __u32 padding;
    __u64 value;
};

struct gpio_v2_line_config_attribute {
    struct gpio_v2_line_attribute attr;
    __u64 mask;
};

struct gpio_v2_line_config {
    __u64 flags;
    __u32 num_attrs;
    __u32 padding[5];
    struct gpio_v2_line_config_attribute attrs[10];
};

struct gpio_v2_line_request {
    __u32 offsets[GPIO_V2_LINES_MAX];
    char consumer[32];
    struct gpio_v2_line_config config;
    __u32 num_lines;
    __u32 event_buffer_size;
    __u32 padding[5];
    int fd;
};

#define GPIO_V2_GET_LINE_IOCTL _IOWR(0xB4, 0x06, struct gpio_v2_line_request)
#define GPIO_V2_LINE_GET_VALUES_IOCTL _IOWR(0xB4, 0x0E, struct gpio_v2_line_values)
#define GPIO_V2_LINE_SET_VALUES_IOCTL _IOWR(0xB4, 0x0F, struct gpio_v2_line_values)
#define GPIO_V2_LINE_SET_CONFIG_IOCTL _IOW(0xB4, 0x0D, struct gpio_v2_line_config)

#endif /* _GPIO_H_ */
