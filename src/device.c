#include <linux/err.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "client.h"

#include "device.h"
#include "params.h"

static struct ad7683_device driver_device;

static u16 generate_sample(struct ad7683_device *dev)
{
	u16 sample = dev->next_sample;

	dev->next_sample = (u16)(sample + 1);
	return sample;
}

// Devfs
static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	struct ad7683_device *dev =
		container_of(timer, struct ad7683_device, timer);
	struct adc_client *client;
	unsigned long flags;
	u16 sample = generate_sample(dev);

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
	driver_device.clients_count++;
	spin_unlock_irqrestore(&driver_device.clients_spinlock, flags);

	return nonseekable_open(inode, file);
}

static int device_release(struct inode *inode, struct file *file)
{
	struct adc_client *client = file->private_data;
	unsigned long flags;

	spin_lock_irqsave(&driver_device.clients_spinlock, flags);
	list_del(&client->list);
	driver_device.clients_count--;
	spin_unlock_irqrestore(&driver_device.clients_spinlock, flags);

	file->private_data = NULL;
	adc_client_destroy(client);

	return 0;
}

static ssize_t device_read(struct file *file, char __user *ubuf, size_t count,
			   loff_t *ppos)
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

			status = wait_event_interruptible(
				client->read_queue,
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

static const struct file_operations ad7683_fops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.release = device_release,
	.read = device_read,
	.unlocked_ioctl = ad7683_ioctl,
};

int ad7683_set_sample_rate(u32 rate)
{
	if (!rate || rate > AD7683_MAX_SAMPLE_RATE)
		return DEV_INVALID;

	mutex_lock(&driver_device.config_lock);
	hrtimer_cancel(&driver_device.timer);
	driver_device.sample_rate = rate;
	driver_device.sample_period = ns_to_ktime(NSEC_PER_SEC / rate);
	hrtimer_start(&driver_device.timer, driver_device.sample_period,
		      HRTIMER_MODE_REL);
	mutex_unlock(&driver_device.config_lock);

	return DEV_OK;
}

u32 ad7683_get_sample_rate(void)
{
	u32 rate;

	mutex_lock(&driver_device.config_lock);
	rate = driver_device.sample_rate;
	mutex_unlock(&driver_device.config_lock);

	return rate;
}

struct device_stats *ad7683_get_stats(void)
{
	struct device_stats *stats;
	struct adc_client *client;
	unsigned long flags;
	size_t capacity;

	for (int attempt = 0; attempt < 4; attempt++) {
		spin_lock_irqsave(&driver_device.clients_spinlock, flags);
		capacity = driver_device.clients_count;
		spin_unlock_irqrestore(&driver_device.clients_spinlock, flags);

		stats = kvzalloc(struct_size(stats, clients, capacity),
				 GFP_KERNEL);
		if (!stats)
			return ERR_PTR(DEV_NOMEM);

		mutex_lock(&driver_device.config_lock);
		stats->rate = driver_device.sample_rate;
		spin_lock_irqsave(&driver_device.clients_spinlock, flags);
		if (driver_device.clients_count > capacity) {
			spin_unlock_irqrestore(&driver_device.clients_spinlock,
					       flags);
			mutex_unlock(&driver_device.config_lock);
			kvfree(stats);
			continue;
		}

		stats->generated =
			atomic64_read(&driver_device.samples_generated);
		list_for_each_entry(client, &driver_device.clients, list) {
			struct client_stats *row =
				&stats->clients[stats->count++];
			spin_lock(&client->lock);
			row->capacity = client->buffer_size;
			row->buffered = client->count;
			row->samples_read = client->samples_read;
			row->overruns = client->overruns;
			spin_unlock(&client->lock);
		}
		spin_unlock_irqrestore(&driver_device.clients_spinlock, flags);
		mutex_unlock(&driver_device.config_lock);

		return stats;
	}

	return ERR_PTR(DEV_BUSY);
}

int ad7683_device_init(void)
{
	int ret;

	mutex_init(&driver_device.config_lock);
	spin_lock_init(&driver_device.clients_spinlock);
	INIT_LIST_HEAD(&driver_device.clients);
	driver_device.clients_count = 0;
	driver_device.sample_rate = AD7683_DEFAULT_SAMPLE_RATE;
	driver_device.sample_period =
		ns_to_ktime(NSEC_PER_SEC / driver_device.sample_rate);
	atomic64_set(&driver_device.samples_generated, 0);
	driver_device.next_sample = 0;
	hrtimer_init(&driver_device.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	driver_device.timer.function = timer_callback;

	ret = alloc_chrdev_region(&driver_device.devno, 0, AD7683_COUNT,
				  AD7683_NAME);
	if (ret < 0) {
		return ret;
	}

	cdev_init(&driver_device.cdev, &ad7683_fops);
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

	driver_device.device = device_create(driver_device.class, NULL,
					     driver_device.devno, NULL,
					     AD7683_NAME);
	if (IS_ERR(driver_device.device)) {
		ret = PTR_ERR(driver_device.device);
		goto err_class;
	}

	hrtimer_start(&driver_device.timer, driver_device.sample_period,
		      HRTIMER_MODE_REL);
	ret = ad7683_params_init(driver_device.device);
	if (ret) {
		hrtimer_cancel(&driver_device.timer);
		goto err_device;
	}

	return DEV_OK;

err_device:
	device_destroy(driver_device.class, driver_device.devno);
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
	ad7683_params_exit(driver_device.device);
	hrtimer_cancel(&driver_device.timer);
	device_destroy(driver_device.class, driver_device.devno);
	class_destroy(driver_device.class);
	cdev_del(&driver_device.cdev);
	unregister_chrdev_region(driver_device.devno, AD7683_COUNT);
}
