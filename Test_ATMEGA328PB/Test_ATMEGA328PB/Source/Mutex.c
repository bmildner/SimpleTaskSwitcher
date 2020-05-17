/*
 * Mutex.c
 *
 * Created: 03.05.2020 09:43:18
 *  Author: Berti
 */ 

#include "Mutex.h"

SwitcherError LockMutex(Mutex* mutex, Timeout timeout)
{
  if (g_CurrentTask == NULL)
  {
    return SwitcherNotInitialized;
  }

  if (mutex == NULL)
  {
    return SwitcherInvalidParameter;
  }

  PauseSwitching();
  
  // no current owner
  if (mutex->m_SyncObject.m_pCurrentOwner == NULL)
  {
    SWITCHER_ASSERT(mutex->m_LockCount == 0);
    
    AcquireSyncObject(&mutex->m_SyncObject, g_CurrentTask);
    
    mutex->m_LockCount = 1;
    
    ResumeSwitching();
    
    return SwitcherNoError;
  }
  
  // we already own it
  if (mutex->m_SyncObject.m_pCurrentOwner == g_CurrentTask)
  {
    SWITCHER_ASSERT(g_CurrentTask->m_pAcquiredList != NULL);
    SWITCHER_ASSERT(((mutex->m_SyncObject.m_pAcquiredListNext == NULL) && (g_CurrentTask->m_pAcquiredList == &mutex->m_SyncObject)) || 
                    (mutex->m_SyncObject.m_pAcquiredListNext != NULL));

    mutex->m_LockCount++;
    
    ResumeSwitching();
    
    return SwitcherNoError;
  }
  
  // someone else owns it
  if (timeout == TimeoutNone)
  {
    ResumeSwitching();
    
    return SwitcherTimeout;
  }
  
  // we need to add us to the waiting list
  QueueForSyncObject(&mutex->m_SyncObject, g_CurrentTask);
  
  // sleep until we get ownership or we timeout
  Sleep(timeout);  // returns with task switcher paused
  
  SWITCHER_ASSERT(mutex->m_SyncObject.m_pCurrentOwner != NULL);
  
  // did we timeout
  if (mutex->m_SyncObject.m_pCurrentOwner != g_CurrentTask)
  {
    // remove us from the waiting list
    UnqueueFromSyncObject(&mutex->m_SyncObject, g_CurrentTask);
    
    ResumeSwitching();
    
    return SwitcherTimeout;
  }
  
  SWITCHER_ASSERT(mutex->m_SyncObject.m_pCurrentOwner == g_CurrentTask);
  
  ResumeSwitching();
  
  return SwitcherNoError;
}

SwitcherError UnlockMutex(Mutex* mutex)
{
  if (g_CurrentTask == NULL)
  {
    return SwitcherNotInitialized;
  }

  if (mutex == NULL)
  {
    return SwitcherInvalidParameter;
  }

  PauseSwitching();

  if (mutex->m_SyncObject.m_pCurrentOwner != g_CurrentTask)
  {
    ResumeSwitching();
    
    return SwitcherResourceNotOwned;
  }
  
  SWITCHER_ASSERT(mutex->m_LockCount > 0);
  
  mutex->m_LockCount--;
  
  // only release if lock count dropped to zero
  if (mutex->m_LockCount == 0)
  {
    ReleaseSyncObject(&mutex->m_SyncObject, g_CurrentTask);
  }
  
  ResumeSwitching();
  
  return SwitcherNoError;
}