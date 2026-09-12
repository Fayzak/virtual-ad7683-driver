#include <linux/slab.h>

#include "client.h"

struct adc_client *adc_client_create(size_t buffer_size)
{
    struct adc_client *client;

    if (buffer_size == 0)
        return NULL;

    client = kmalloc(sizeof(*client), GFP_KERNEL);
    if (!client)
        return NULL;

    client->buffer = kmalloc_array(buffer_size, sizeof(*client->buffer), GFP_KERNEL);
    if (!client->buffer) {
        kfree(client);
        return NULL;
    }

    client->buffer_size = buffer_size;
    client->head = 0;
    client->tail = 0;
    client->count = 0;
    client->samples_read = 0;
    client->overruns = 0;

    spin_lock_init(&client->lock);
    INIT_LIST_HEAD(&client->list);
    init_waitqueue_head(&client->read_queue);

    return client;
}

void adc_client_destroy(struct adc_client *client)
{
    if (!client)
        return;

    /* The caller must detach the client and stop all accesses before freeing it. */
    kfree(client->buffer);
    kfree(client);
}

int adc_client_push(struct adc_client *client, u16 sample)
{
    unsigned long flags;

    spin_lock_irqsave(&client->lock, flags);
    if (client->count == client->buffer_size) {
        client->overruns++;
        spin_unlock_irqrestore(&client->lock, flags);
        return CLI_NOSPACE;
    }

    client->buffer[client->head] = sample;
    client->head = (client->head + 1) % client->buffer_size;

    client->count++;
    spin_unlock_irqrestore(&client->lock, flags);

    return CLI_OK;
}

int adc_client_pop(struct adc_client *client, u16 *sample)
{
    unsigned long flags;

    spin_lock_irqsave(&client->lock, flags);
    if (client->count == 0) {
        spin_unlock_irqrestore(&client->lock, flags);
        return CLI_EMPTY;
    }

    *sample = client->buffer[client->tail];

    client->tail = (client->tail + 1) % client->buffer_size;
    client->count--;
    client->samples_read++;
    spin_unlock_irqrestore(&client->lock, flags);

    return CLI_OK;
}
