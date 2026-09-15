#ifndef DEVICE_H
#define DEVICE_H

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/ioctl.h>
#include <linux/types.h>

#define DEV_OK 0
#define DEV_INVALID (-EINVAL)
#define DEV_NOMEM (-ENOMEM)
#define DEV_BADCOPY (-EFAULT)
#define DEV_BAD_IOCTL (-ENOTTY)

#define AD7683_IOC_MAGIC 'A'
#define AD7683_IOC_GET_SAMPLE_RATE _IOR(AD7683_IOC_MAGIC, 0, __u32)
#define AD7683_IOC_SET_SAMPLE_RATE _IOW(AD7683_IOC_MAGIC, 1, __u32)
#define AD7683_IOC_CLEAR_BUFFER _IO(AD7683_IOC_MAGIC, 2)

#define AD7683_MAX_SAMPLE_RATE 100000
#define AD7683_DEFAULT_SAMPLE_RATE 10000
#define AD7683_DEFAULT_BUFFER_SIZE 10000 /* Capacity in samples. */
#define AD7683_COUNT 1
#define AD7683_NAME "ad7683"

struct ad7683_device {
    struct cdev cdev;
    dev_t devno;

    struct class *class;
    struct device *device;

    struct hrtimer timer;
    ktime_t sample_period;

    u32 sample_rate;
    struct mutex config_lock;

    spinlock_t clients_spinlock;
    struct list_head clients;

    atomic64_t samples_generated;
};

int ad7683_device_init(void);
void ad7683_device_exit(void);

#endif
