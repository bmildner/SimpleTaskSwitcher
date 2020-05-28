/*
 * Event.h
 *
 * Created: 25.05.2020 20:42:35
 *  Author: Berti
 */ 

#ifndef EVENT_H_
#define EVENT_H_

#include "SyncObject.h"

typedef struct
{
  SyncObject m_SyncObject;
  bool       m_PendingNotification;
} Event;

#define SWITCHER_EVENT_STATIC_INIT() {.m_SyncObject = SWITCHER_SYNCOBJECT_WITH_NOTIFICATION_STATIC_INIT(), .m_PendingNotification = false}

SwitcherError WaitForEvent(Event* event, Timeout timeout);

SwitcherError EventNotifyOne(Event* event);

SwitcherError EventNotifyAll(Event* event);

#endif /* EVENT_H_ */