#include "event_queue.h"

#define QUEUE_BARRIER() __asm__ volatile("" ::: "memory")

#if (SYNTH_EVENT_QUEUE_SIZE & (SYNTH_EVENT_QUEUE_SIZE - 1u)) != 0
#error "SYNTH_EVENT_QUEUE_SIZE must be a power of two"
#endif

void synth_event_queue_init(synth_event_queue_t *q)
{
    q->write_index = 0u;
    q->read_index = 0u;
}

int synth_event_queue_push(synth_event_queue_t *q,
                           const synth_event_t *event)
{
    uint32_t write = q->write_index;
    uint32_t read = q->read_index;

    if ((uint32_t)(write - read) >= SYNTH_EVENT_QUEUE_SIZE)
        return 0;

    q->events[write & (SYNTH_EVENT_QUEUE_SIZE - 1u)] = *event;

    QUEUE_BARRIER();
    q->write_index = write + 1u;
    return 1;
}

int synth_event_queue_pop(synth_event_queue_t *q,
                          synth_event_t *event)
{
    uint32_t read = q->read_index;
    uint32_t write = q->write_index;

    if (read == write)
        return 0;

    *event = q->events[read & (SYNTH_EVENT_QUEUE_SIZE - 1u)];

    QUEUE_BARRIER();
    q->read_index = read + 1u;
    return 1;
}
