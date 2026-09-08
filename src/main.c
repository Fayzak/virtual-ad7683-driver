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
  // if (alloc_type != 0 && alloc_type != 1) {
  //   pr_err("Error: alloc_type must be 0 or 1\n");
  //   return MP_INVALID;
  // }

  // struct msgpool_ctx *ctx = queuegetctx();
  // queuesetctx(ctx, alloc_type, pool_min_nr, interval_ms);
  // timer_setup(&ctx->consumer_timer, timer_callback, 0);
  // mod_timer(&ctx->consumer_timer, jiffies + msecs_to_jiffies(interval_ms));

  pr_info("virtual_ad7683 loaded successfully");
  return 0;
}

static void __exit virtual_ad7683_exit(void) {
  struct msgpool_ctx *ctx = queuegetctx();
  queueclearctx(ctx);

  pr_info("virtual_ad7683 unloaded\n");
}

module_init(virtual_ad7683_init);
module_exit(virtual_ad7683_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Timofey Zaika");
MODULE_DESCRIPTION("AD7683 virtual ADC symbolic driver for Linux OS with implementation of per-process data buffering and /proc and /sys interfaces");
MODULE_VERSION("1.0");
