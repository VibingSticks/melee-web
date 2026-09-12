#include "os_alarm.h"

#include <stddef.h>
#include <stdlib.h>

#include "../port.h"

/* Intrusive doubly linked list sorted by fire time; head fires first. */
static OSAlarm* g_head;
static OSTime g_now;

static int is_linked(const OSAlarm* a) { return a->prev != NULL || a->next != NULL || g_head == a; }

static void unlink_alarm(OSAlarm* a)
{
    if (a->prev != NULL) {
        a->prev->next = a->next;
    } else if (g_head == a) {
        g_head = a->next;
    }
    if (a->next != NULL) {
        a->next->prev = a->prev;
    }
    a->prev = a->next = NULL;
}

static void insert_alarm(OSAlarm* a)
{
    OSAlarm* prev = NULL;
    OSAlarm* cur = g_head;
    unsigned steps = 0;
    while (cur != NULL && cur->fire <= a->fire) {
        prev = cur;
        cur = cur->next;
        if (++steps > 4096) {
            port_log("os_alarm: queue is cyclic (inserting handler %p)", (void*) a->handler);
            abort();
        }
    }
    a->prev = prev;
    a->next = cur;
    if (prev != NULL) {
        prev->next = a;
    } else {
        g_head = a;
    }
    if (cur != NULL) {
        cur->prev = a;
    }
}

void OSInitAlarm(void)
{
    g_head = NULL;
}

void OSCreateAlarm(OSAlarm* a)
{
    /* The game re-creates alarms that are still queued (lbmemory's 3 ms
     * alarm, the pad alarm); zeroing the links of a queued alarm would leave
     * its neighbours pointing at it and turn the list into a cycle. */
    if (is_linked(a)) {
        unlink_alarm(a);
    }
    a->handler = NULL;
    a->tag = 0;
    a->fire = 0;
    a->prev = a->next = NULL;
    a->period = 0;
    a->start = 0;
}

void OSSetAbsAlarm(OSAlarm* a, OSTime time, OSAlarmHandler handler)
{
    if (is_linked(a)) {
        unlink_alarm(a);
    }
    a->handler = handler;
    a->period = 0;
    a->fire = time;
    insert_alarm(a);
}

void OSSetAlarm(OSAlarm* a, OSTime tick, OSAlarmHandler handler)
{
    OSSetAbsAlarm(a, g_now + tick, handler);
}

void OSSetPeriodicAlarm(OSAlarm* a, OSTime start, OSTime period, OSAlarmHandler handler)
{
    if (is_linked(a)) {
        unlink_alarm(a);
    }
    a->handler = handler;
    a->period = period;
    a->start = start;
    a->fire = start;
    insert_alarm(a);
}

void OSCancelAlarm(OSAlarm* a)
{
    if (is_linked(a)) {
        unlink_alarm(a);
    }
    a->handler = NULL;
}

void OSSetAlarmTag(OSAlarm* a, u32 tag)
{
    a->tag = tag;
}

BOOL OSCheckAlarmQueue(void)
{
    return g_head != NULL;
}

OSTime port_alarm_now(void)
{
    return g_now;
}

void port_alarm_tick(OSTime now)
{
    /* Two phases: first take every alarm that is due *now* off the queue, then
     * run their handlers. An alarm a handler (re)arms with an immediate or zero
     * delay therefore fires on the next tick, as it would on the hardware's
     * next decrementer interrupt, instead of keeping this loop busy forever. */
    OSAlarm* due[32];
    unsigned n = 0;
    g_now = now;
    while (g_head != NULL && g_head->fire <= now && n < 32) {
        OSAlarm* a = g_head;
        unlink_alarm(a);
        if (a->period > 0) {
            /* Skip periods that were missed entirely (long stall) so the
             * handler does not fire many times in one tick. Arithmetic, not a
             * loop: the game arms some periodic alarms with a tiny absolute
             * start, which could be billions of periods behind. */
            OSTime behind = now - a->fire;
            a->fire += (behind / a->period + 1) * a->period;
            insert_alarm(a);
        }
        due[n++] = a;
    }
    for (unsigned i = 0; i < n; i++) {
        if (due[i]->handler != NULL) {
            due[i]->handler(due[i], NULL);
        }
    }
}
