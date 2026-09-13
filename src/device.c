#include <linux/err.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/uaccess.h>

#include "client.h"

#include "device.h"

struct ad7683_device driver_device;

static inline u16 generate_sample(void)
{
    return get_random_u16();
}

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
    struct ad7683_device *dev = container_of(timer, struct ad7683_device, timer);
    struct adc_client *client;
    unsigned long flags;
    u16 sample = generate_sample();

    atomic64_inc(&dev->samples_generated);

    spin_lock_irqsave(&dev->clients_spinlock, flags);
    list_for_each_entry(client, &dev->clients, list) {
        adc_client_push(client, sample);
    }
    spin_unlock_irqrestore(&dev->clients_spinlock, flags);

    hrtimer_forward_now(timer, dev->sample_period);
    return HRTIMER_RESTART;
}

static int device_open(struct inode *inode, struct file *file)
{
    struct adc_client *client;
    unsigned long flags;

    client = adc_client_create(AD7683_DEFAULT_BUFFER_SIZE);
    if (!client)
        return DEV_NOMEM;

    file->private_data = client;
    spin_lock_irqsave(&driver_device.clients_spinlock, flags);
    list_add(&client->list, &driver_device.clients);
    spin_unlock_irqrestore(&driver_device.clients_spinlock, flags);

    return nonseekable_open(inode, file);
}

static int device_release(struct inode *inode, struct file *file)
{
    struct adc_client *client = file->private_data;
    unsigned long flags;

    spin_lock_irqsave(&driver_device.clients_spinlock, flags);
    list_del(&client->list);
    spin_unlock_irqrestore(&driver_device.clients_spinlock, flags);

    file->private_data = NULL;
    adc_client_destroy(client);

	return 0;
}

static ssize_t device_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
    struct adc_client *client = file->private_data;
    size_t bytes = 0;
    u16 sample;
    int status;

	if (count == 0)
		return 0;

    if (count % sizeof(u16) != 0)
        return DEV_INVALID;

    while (bytes < count) {
        status = adc_client_pop(client, &sample);
        if (status < 0) {
            if (status != CLI_EMPTY)
                return bytes ? (ssize_t)bytes : status;
            if (file->f_flags & O_NONBLOCK)
                return bytes ? (ssize_t)bytes : CLI_EMPTY;

            status = wait_event_interruptible(client->read_queue,
                                             adc_client_has_data(client));
            if (status)
                return bytes ? (ssize_t)bytes : status;
            continue;
        }

        if (copy_to_user(ubuf + bytes, &sample, sizeof(sample)))
            return bytes ? (ssize_t)bytes : DEV_BADCOPY;

        bytes += sizeof(sample);
    }

    return bytes;
}

static const struct file_operations democh_fops = {
	.owner   = THIS_MODULE,
	.open    = device_open,
	.release = device_release,
	.read    = device_read,
	.write   = NULL,
};

int ad7683_device_init(void)
{
    int ret;

    spin_lock_init(&driver_device.clients_spinlock);
    INIT_LIST_HEAD(&driver_device.clients);
    driver_device.sample_rate = AD7683_DEFAULT_SAMPLE_RATE;
    driver_device.sample_period = ns_to_ktime(NSEC_PER_SEC / driver_device.sample_rate);
    atomic64_set(&driver_device.samples_generated, 0);
    hrtimer_init(&driver_device.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    driver_device.timer.function = timer_callback;

    ret = alloc_chrdev_region(&driver_device.devno, 0, AD7683_COUNT, AD7683_NAME);
    if (ret < 0) {
        return ret;
    }

    cdev_init(&driver_device.cdev, &democh_fops);
    driver_device.cdev.owner = THIS_MODULE;

    ret = cdev_add(&driver_device.cdev, driver_device.devno, AD7683_COUNT);
    if (ret < 0) {
        goto err_region;
    }

    driver_device.class = class_create(AD7683_NAME);
    if (IS_ERR(driver_device.class)) {
        ret = PTR_ERR(driver_device.class);
        goto err_cdev;
    }

    driver_device.device = device_create(
        driver_device.class,
        NULL,
        driver_device.devno,
        NULL,
        AD7683_NAME
    );
    if (IS_ERR(driver_device.device)) {
        ret = PTR_ERR(driver_device.device);
        goto err_class;
    }

    hrtimer_start(&driver_device.timer, driver_device.sample_period, HRTIMER_MODE_REL);

    return DEV_OK;

err_class:
	class_destroy(driver_device.class);
err_cdev:
	cdev_del(&driver_device.cdev);
err_region:
	unregister_chrdev_region(driver_device.devno, AD7683_COUNT);

    return ret;
}

void ad7683_device_exit(void)
{
    hrtimer_cancel(&driver_device.timer);
	device_destroy(driver_device.class, driver_device.devno);
	class_destroy(driver_device.class);
	cdev_del(&driver_device.cdev);
	unregister_chrdev_region(driver_device.devno, AD7683_COUNT);
}
