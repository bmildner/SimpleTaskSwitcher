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
  
  SWITCHER_ASSERT(g_CurrentTask->m_pIsWaitingFor == NULL);
  
  // no current owner
  if (IsFreeSyncObject(&mutex->m_SyncObject))
  {
    SWITCHER_ASSERT(mutex->m_LockCount == 0);
    
    AcquireSyncObject(&mutex->m_SyncObject, g_CurrentTask);
    
    mutex->m_LockCount = 1;
    
    ResumeSwitching();
    
    return SwitcherNoError;
  }
  
  // we already own it
  if (IsCurrentSyncObjectOwner(&mutex->m_SyncObject, g_CurrentTask))
  {
    SWITCHER_ASSERT(mutex->m_LockCount > 0);
    
    mutex->m_LockCount++;
    
    ResumeSwitching();
    
    return SwitcherNoError;
  }
  
  // someone else owns it or is pending next owner

  SWITCHER_ASSERT(!IsNextSyncObjectOwner(&mutex->m_SyncObject, g_CurrentTask));  // we can not be a pending next owner here
    
  // exit with timeout if we do not want to wait
  if (timeout == TimeoutNone)
  {
    ResumeSwitching();
    
    return SwitcherTimeout;
  }
  
  // we need to add us to the waiting list
  QueueForSyncObject(&mutex->m_SyncObject, g_CurrentTask);
  
  // sleep until we get ownership or we timeout
  Sleep(timeout);  // returns with task switcher paused
    
  // did we timeout
  if (!IsNextSyncObjectOwner(&mutex->m_SyncObject, g_CurrentTask))
  {
    // remove us from the waiting list
    UnqueueFromSyncObject(&mutex->m_SyncObject, g_CurrentTask);
    
    SWITCHER_ASSERT(g_CurrentTask->m_pIsWaitingFor == NULL);
    
    ResumeSwitching();
    
    return SwitcherTimeout;
  }

  // we are the pending next owner
  
  // remove us from the waiting list
  UnqueueFromSyncObject(&mutex->m_SyncObject, g_CurrentTask);

  // take ownership  
  AcquireSyncObject(&mutex->m_SyncObject, g_CurrentTask);

  SWITCHER_ASSERT(g_CurrentTask->m_pIsWaitingFor == NULL);
  SWITCHER_ASSERT(IsCurrentSyncObjectOwner(&mutex->m_SyncObject, g_CurrentTask));
  SWITCHER_ASSERT(g_CurrentTask->m_pAcquiredList == &mutex->m_SyncObject);
  
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
  
  SWITCHER_ASSERT(g_CurrentTask->m_pIsWaitingFor == NULL);

  // we have to own the mutex to unlock it
  if (!IsCurrentSyncObjectOwner(&mutex->m_SyncObject, g_CurrentTask))
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