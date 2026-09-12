#include "os_alarm.h"

#include <stddef.h>

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
    while (cur != NULL && cur->fire <= a->fire) {
        prev = cur;
        cur = cur->next;
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
    g_now = now;
    while (g_head != NULL && g_head->fire <= now) {
        OSAlarm* a = g_head;
        OSAlarmHandler handler = a->handler;
        unlink_alarm(a);
        if (a->period > 0) {
            /* Skip periods that were missed entirely (long stall) so the
             * handler does not fire many times in one tick. */
            do {
                a->fire += a->period;
            } while (a->fire <= now);
            insert_alarm(a);
        }
        if (handler != NULL) {
            handler(a, NULL);
        }
    }
}
