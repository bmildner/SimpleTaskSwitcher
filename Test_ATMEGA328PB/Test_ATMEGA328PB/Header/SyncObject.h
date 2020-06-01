/*
 * SyncObject.h
 *
 * Created: 05.05.2020 08:33:36
 *  Author: Berti
 */ 

#ifndef SYNCOBJECT_H_
#define SYNCOBJECT_H_

#include "Switcher.h"

// SyncObject struct is in Switcher.h right above the Task struct!

#define SWITCHER_SYNCOBJECT_WITH_OWNERSHIP_STATIC_INIT()    {.m_pWaitingList = NULL, .m_Flags = 0, .m_HasOwnershipSemantic = true,  .m_pAcquiredListNext = NULL, .m_pCurrentOrNextOwner = NULL}
#define SWITCHER_SYNCOBJECT_WITH_NOTIFICATION_STATIC_INIT() {.m_pWaitingList = NULL, .m_Flags = 0, .m_HasOwnershipSemantic = false, .m_NotificationCounter = 0, .m_PendingNotification = false}

// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline bool IsFreeSyncObject(const SyncObject* syncObject)
{
  SWITCHER_ASSERT(syncObject != NULL);
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);
  
  if (syncObject->m_pCurrentOrNextOwner == NULL)
  {
    SWITCHER_ASSERT(!syncObject->m_HasPendingNewOwner);
    SWITCHER_ASSERT(syncObject->m_pWaitingList == NULL);
    SWITCHER_ASSERT(syncObject->m_pAcquiredListNext == NULL);
    
    return true;
  }
  
  return false;
}

// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline bool IsOwnedSyncObject(const SyncObject* syncObject)
{
  SWITCHER_ASSERT(syncObject != NULL);
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);

  if ((syncObject->m_pCurrentOrNextOwner != NULL) && (!syncObject->m_HasPendingNewOwner))
  {
    return true;
  }
  
  return false;
}

// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline bool IsCurrentSyncObjectOwner(const SyncObject* syncObject, const Task* task)
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);
  
  if ((syncObject->m_pCurrentOrNextOwner == task) && (!syncObject->m_HasPendingNewOwner))
  {
    SWITCHER_ASSERT(task->m_pAcquiredList != NULL);
    
    return true;
  }
  
  return false;
}

// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline bool IsNextSyncObjectOwner(const SyncObject* syncObject, const Task* task)
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);
  
  if ((syncObject->m_pCurrentOrNextOwner == task) && (syncObject->m_HasPendingNewOwner))
  {
    SWITCHER_ASSERT(syncObject->m_pWaitingList != NULL);
    
    return true;
  }
  
  return false;
}

// expects: task switcher is currently paused
//          sync object has ownership semantic
//          sync object has to be either in state free or pending new owner where <task> is the designated new owner
__attribute__((always_inline))
static inline void AcquireSyncObject(SyncObject* syncObject, Task* task)
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);  
  SWITCHER_ASSERT(IsFreeSyncObject(syncObject) || IsNextSyncObjectOwner(syncObject, task));
  
  // set new owner
  syncObject->m_pCurrentOrNextOwner = task;
  
  // reset has pending new owner flag
  syncObject->m_HasPendingNewOwner = false;
  
  // add sync object to acquired list
  syncObject->m_pAcquiredListNext = task->m_pAcquiredList;
  task->m_pAcquiredList = syncObject;
  
  // check for higher priority task in waiting list (someone may have queued up between previous owners release and us taking ownership!)
  if ((syncObject->m_pWaitingList != NULL) && 
      (syncObject->m_pWaitingList->m_Priority > task->m_Priority))
  {
    task->m_Priority = syncObject->m_pWaitingList->m_Priority;
    
    SWITCHER_ASSERT(task->m_Priority > task->m_BasePriority);
  }
}

// expects: task switcher is currently paused
//          sync object has ownership semantic
//          task is the current owner of the sync object
void ReleaseSyncObject(SyncObject* syncObject, Task* task);


// expects: task switcher is currently paused
//          sync object has notification semantic
//          there is at least one task waiting
void SyncObjectNotifyOne(SyncObject* syncObject);

// expects: task switcher is currently paused
//          sync object has notification semantic
//          there is at least one task waiting
void SyncObjectNotifyAll(SyncObject* syncObject);


// expects: task switcher is currently paused
//          task is not already waiting for another sync object
//          if sync object has ownership semantic it must not be in state Free
void QueueForSyncObject(SyncObject* syncObject, Task* task);

// expects: task switcher is currently paused
//          task is currently waiting for the sync object (and with ownership semantic we must not be the current owner, but may be pending new owner)
void UnqueueFromSyncObject(SyncObject* syncObject, Task* task);

#endif /* SYNCOBJECT_H_ */
