/* OSAlarm implementation driven from the frame loop instead of a timer interrupt.
 *
 * Time is whatever the caller passes to port_alarm_tick(); on the web that is
 * OSGetTime() once per frame, in host tests it is synthetic. OSSetAlarm's
 * relative tick is measured from the most recent port_alarm_tick() time.
 * Handlers run synchronously inside port_alarm_tick() with a NULL context. */
#ifndef PORT_OS_ALARM_H
#define PORT_OS_ALARM_H

#include <dolphin/os.h>

/* Fire every alarm whose time is <= now, in time order. Periodic alarms are
 * re-armed before their handler runs so the handler may cancel them. */
void port_alarm_tick(OSTime now);

/* The time passed to the last port_alarm_tick() (0 before the first call). */
OSTime port_alarm_now(void);

#endif
