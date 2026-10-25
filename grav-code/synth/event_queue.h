#pragma once
#include <stdint.h>

#define SYNTH_EVENT_QUEUE_SIZE 32u

typedef enum {
    SYNTH_EVENT_NOTE_ON = 1,
    SYNTH_EVENT_NOTE_OFF,
    SYNTH_EVENT_PANIC,
    SYNTH_EVENT_VOICES                   /* the tracks' voices: 4 bits each, S1's lowest, in .note */
} synth_event_type_t;

typedef struct {
    synth_event_type_t type;
    int32_t note;
    uint32_t velocity;
    uint32_t track;                      /* a note's track (S1..S4 = 0..3, D1..D4 = 4..7) */
} synth_event_t;

typedef struct {
    synth_event_t events[SYNTH_EVENT_QUEUE_SIZE];
    volatile uint32_t write_index;
    volatile uint32_t read_index;
} synth_event_queue_t;

void synth_event_queue_init(synth_event_queue_t *q);
int synth_event_queue_push(synth_event_queue_t *q,
                           const synth_event_t *event);
int synth_event_queue_pop(synth_event_queue_t *q,
                          synth_event_t *event);
