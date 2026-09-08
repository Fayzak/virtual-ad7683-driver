#ifndef CLIENT_H
#define CLIENT_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/wait.h>

struct adc_client {
    struct list_head list;

    u16 *buffer;
    size_t buffer_size;

    size_t head;
    size_t tail;
    size_t count;

    spinlock_t lock;

    wait_queue_head_t read_queue;

    u64 samples_read;
    u64 overruns;
};

struct adc_client *adc_client_create(size_t buffer_size);
void adc_client_destroy(struct adc_client *client);
bool adc_client_buffer_empty(struct adc_client *client);
bool adc_client_buffer_full(struct adc_client *client);
int adc_client_push(struct adc_client *client, u16 sample);
int adc_client_pop(struct adc_client *client, u16 *sample);

#endif
