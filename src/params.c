#include <linux/err.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>

#include "device.h"
#include "params.h"

static struct proc_dir_entry *proc_file;

// Sysfs
static ssize_t sample_rate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", ad7683_get_sample_rate());
}

static ssize_t sample_rate_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	unsigned int rate;
	int ret;

	ret = kstrtouint(buf, 10, &rate);
	if (ret)
		return ret;

	ret = ad7683_set_sample_rate(rate);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(sample_rate);

// Procfs
static int proc_show(struct seq_file *m, void *v)
{
	const struct device_stats *stats = m->private;
	seq_printf(m, "sample_rate_hz: %u\n", stats->rate);
	seq_printf(m, "samples_generated: %llu\n", stats->generated);
	seq_printf(m, "clients: %zu\n", stats->count);

	for (size_t i = 0; i < stats->count; i++) {
		const struct client_stats *client = &stats->clients[i];
		seq_printf(m, "\nclient %zu:\n", i);
		seq_printf(m, "  capacity: %zu\n", client->capacity);
		seq_printf(m, "  buffered: %zu\n", client->buffered);
		seq_printf(m, "  samples_read: %llu\n", client->samples_read);
		seq_printf(m, "  overruns: %llu\n", client->overruns);
	}

	return 0;
}

static int proc_open(struct inode *inode, struct file *file)
{
	struct device_stats *stats = ad7683_get_stats();
	int ret;

	if (IS_ERR(stats))
		return PTR_ERR(stats);
	ret = single_open(file, proc_show, stats);
	if (ret)
		kvfree(stats);
	return ret;
}

static int proc_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;

	kvfree(m->private);
	return single_release(inode, file);
}

static const struct proc_ops ad7683_proc_fops = {
	.proc_open = proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = proc_release,
};

// IOCTL
long ad7683_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	__u32 rate;
	struct adc_client *client = file->private_data;
	unsigned long flags;

	switch (cmd) {
	case AD7683_IOC_GET_SAMPLE_RATE:
		rate = ad7683_get_sample_rate();

		if (copy_to_user(argp, &rate, sizeof(rate)))
			return DEV_BADCOPY;

		break;

	case AD7683_IOC_SET_SAMPLE_RATE:
		if (copy_from_user(&rate, argp, sizeof(rate)))
			return DEV_BADCOPY;

		return ad7683_set_sample_rate(rate);

	case AD7683_IOC_CLEAR_BUFFER:
		spin_lock_irqsave(&client->lock, flags);
		client->head = client->tail = client->count = 0;
		spin_unlock_irqrestore(&client->lock, flags);

		break;

	default:
		return DEV_BAD_IOCTL;
	}

	return DEV_OK;
}

int ad7683_params_init(struct device *dev)
{
	int ret;

	proc_file = proc_create(AD7683_NAME, 0444, NULL, &ad7683_proc_fops);
	if (!proc_file)
		return -ENOMEM;
	ret = device_create_file(dev, &dev_attr_sample_rate);
	if (ret) {
		proc_remove(proc_file);
		proc_file = NULL;
	}
	return ret;
}

void ad7683_params_exit(struct device *dev)
{
	device_remove_file(dev, &dev_attr_sample_rate);
	proc_remove(proc_file);
	proc_file = NULL;
}
