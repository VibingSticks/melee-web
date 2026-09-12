#include "check.h"
#include "os_shim/os_alarm.h"

static int fired;
static OSAlarm* last_alarm;
static void cb(OSAlarm* a, OSContext* c)
{
    (void) c;
    fired++;
    last_alarm = a;
}

static void cancel_self(OSAlarm* a, OSContext* c)
{
    (void) c;
    fired++;
    OSCancelAlarm(a);
}

static void run(void)
{
    OSAlarm a, b;
    OSInitAlarm();
    OSCreateAlarm(&a);
    OSCreateAlarm(&b);
    fired = 0;

    /* one-shot, relative to the last tick time */
    port_alarm_tick(1000);
    OSSetAlarm(&a, 100, cb);
    CHECK(OSCheckAlarmQueue());
    port_alarm_tick(1050);
    CHECK_EQ_U32(fired, 0);
    port_alarm_tick(1100);
    CHECK_EQ_U32(fired, 1);
    CHECK(last_alarm == &a);
    port_alarm_tick(5000);
    CHECK_EQ_U32(fired, 1); /* one-shot */
    CHECK(!OSCheckAlarmQueue());

    /* absolute */
    OSSetAbsAlarm(&a, 6000, cb);
    port_alarm_tick(5999);
    CHECK_EQ_U32(fired, 1);
    port_alarm_tick(6000);
    CHECK_EQ_U32(fired, 2);

    /* periodic: fires at start, then every period; missed periods collapse */
    OSSetPeriodicAlarm(&a, 7000, 10, cb);
    port_alarm_tick(7000);
    port_alarm_tick(7010);
    port_alarm_tick(7015);
    CHECK_EQ_U32(fired, 4);
    port_alarm_tick(7100); /* 8 periods missed at once -> exactly one call */
    CHECK_EQ_U32(fired, 5);
    OSCancelAlarm(&a);
    port_alarm_tick(9000);
    CHECK_EQ_U32(fired, 5);

    /* ordering: two alarms fire in time order regardless of insertion order */
    fired = 0;
    OSSetAbsAlarm(&b, 9500, cb);
    OSSetAbsAlarm(&a, 9400, cb);
    port_alarm_tick(9450);
    CHECK_EQ_U32(fired, 1);
    CHECK(last_alarm == &a);
    port_alarm_tick(9500);
    CHECK_EQ_U32(fired, 2);
    CHECK(last_alarm == &b);

    /* re-arming a linked alarm moves it instead of duplicating it */
    OSSetAbsAlarm(&a, 10000, cb);
    OSSetAbsAlarm(&a, 10100, cb);
    port_alarm_tick(10000);
    CHECK_EQ_U32(fired, 2);
    port_alarm_tick(10100);
    CHECK_EQ_U32(fired, 3);

    /* a periodic handler may cancel itself */
    fired = 0;
    OSSetPeriodicAlarm(&a, 11000, 5, cancel_self);
    port_alarm_tick(11000);
    port_alarm_tick(11020);
    CHECK_EQ_U32(fired, 1);
    CHECK(!OSCheckAlarmQueue());
}

TEST_MAIN(run)
