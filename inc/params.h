#ifndef AD7683_PARAMS_H
#define AD7683_PARAMS_H

#include <linux/device.h>
#include <linux/fs.h>

int ad7683_params_init(struct device *dev);
void ad7683_params_exit(struct device *dev);
long ad7683_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif
