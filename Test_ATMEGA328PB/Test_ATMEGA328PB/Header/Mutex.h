/*
 * Mutex.h
 *
 * Created: 03.05.2020 09:43:01
 *  Author: Berti
 */ 

#ifndef MUTEX_H_
#define MUTEX_H_

#include "SyncObject.h"

// Recursive Mutex
// Do not use in ISRs!
typedef struct
{
  SyncObject m_SyncObject;
  uint16_t   m_LockCount;
} Mutex;

#define SWITCHER_MUTEX_STATIC_INIT() {.m_SyncObject = SWITCHER_SYNCOBJECT_WITH_OWNERSHIP_STATIC_INIT(), .m_LockCount = 0}

SwitcherError LockMutex(Mutex* mutex, Timeout timeout);
SwitcherError UnlockMutex(Mutex* mutex);

#endif /* MUTEX_H_ */