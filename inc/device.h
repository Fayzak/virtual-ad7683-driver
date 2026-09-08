#ifndef DEVICE_H
#define DEVICE_H

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define DEV_OK 0              /* операция успешна             */
#define DEV_INVALID (-EINVAL) /* неверный параметр            */
#define DEV_NOMEM (-ENOMEM)   /* недостаточно памяти          */
#define DEV_BUSY (-EBUSY)     /* операция недоступна сейчас   */

#define AD7683_MAX_SAMPLE_RATE 100000
#define AD7683_DEFAULT_SAMPLE_RATE 10000

struct ad7683_device {
    struct cdev cdev;
    dev_t devno;

    struct class *class;
    struct device *device;

    struct hrtimer timer;

    u32 sample_rate;

    spinlock_t clients_spinlock;
    struct list_head clients;

    atomic64_t samples_generated;
};

int ad7683_device_init(void);
void ad7683_device_exit(void);

#endif
