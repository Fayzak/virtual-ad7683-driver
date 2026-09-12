#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/timer.h>

#include "device.h"

static void timer_callback(struct timer_list *t) {

  // mod_timer(&ctx->consumer_timer, jiffies + msecs_to_jiffies(ctx->interval_ms));
}

static int __init virtual_ad7683_init(void) {
  int ret = ad7683_device_init();
  if (ret < 0) {
      pr_err("ad7683: init error: %d\n", ret);
      return ret;
  }

  pr_info("virtual_ad7683 loaded successfully");
  return 0;
}

static void __exit virtual_ad7683_exit(void) {
  ad7683_device_exit();
  pr_info("virtual_ad7683 unloaded\n");
}

module_init(virtual_ad7683_init);
module_exit(virtual_ad7683_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Timofey Zaika");
MODULE_DESCRIPTION("AD7683 virtual ADC symbolic driver for Linux OS with implementation of per-process data buffering and /proc and /sys interfaces");
MODULE_VERSION("1.0");
