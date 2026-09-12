#include <linux/err.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "client.h"

#include "device.h"

struct ad7683_device driver_device;

static u16 generate_sample(void)
{
    return 0;
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
        if (status) {
            /* Уже переданные данные важнее ошибки следующего pop.
             * TODO(wait_queue): если буфер пуст и bytes == 0, обычный
             * read должен ждать; пока возвращаем -EAGAIN для обоих режимов.
             */
            return bytes ? (ssize_t)bytes : status;
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
    atomic64_set(&driver_device.samples_generated, 0);

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
	device_destroy(driver_device.class, driver_device.devno);
	class_destroy(driver_device.class);
	cdev_del(&driver_device.cdev);
	unregister_chrdev_region(driver_device.devno, AD7683_COUNT);
}
