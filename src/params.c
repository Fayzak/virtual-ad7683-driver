#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>

#include "device.h"

static int test_set(const char *val, const struct kernel_param *kp) {
  unsigned int x;
  int ret = kstrtouint(val, 10, &x);
  if (ret) {
    pr_err("test_set: kstrtouint error\n");
    return DEV_INVALID;
  }

  // struct msgpool_ctx *ctx = queuegetctx();
  // ctx->interval_ms = x;
  // interval_ms = x;
  // mod_timer(&ctx->consumer_timer, jiffies + msecs_to_jiffies(interval_ms));

  return DEV_OK;
}

static int test_get(char *buf, const struct kernel_param *kp) {
  snprintf(buf, PAGE_SIZE, "test=%d\n", 1);
  return strlen(buf);
}

static const struct kernel_param_ops param_ops_test = {
    .set = test_set, .get = test_get};
module_param_cb(test, &param_ops_test, NULL, 0644);
MODULE_PARM_DESC(test, "test runtime param");
