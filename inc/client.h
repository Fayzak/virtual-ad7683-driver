#ifndef CLIENT_H
#define CLIENT_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/wait.h>

#define CLI_OK 0
#define CLI_NOSPACE (-ENOSPC)
#define CLI_EMPTY (-EAGAIN)

struct adc_client {
    struct list_head list;

    u16 *buffer;
    size_t buffer_size; /* Capacity in u16 samples. */

    size_t head; /* Next write position. */
    size_t tail; /* Next read position. */
    size_t count;

    spinlock_t lock;

    wait_queue_head_t read_queue;

    u64 samples_read; /* Samples successfully removed by pop, not user copies. */
    u64 overruns;
};

struct adc_client *adc_client_create(size_t buffer_size);
void adc_client_destroy(struct adc_client *client);
int adc_client_push(struct adc_client *client, u16 sample);
int adc_client_pop(struct adc_client *client, u16 *sample);
bool adc_client_has_data(struct adc_client *client);

#endif
