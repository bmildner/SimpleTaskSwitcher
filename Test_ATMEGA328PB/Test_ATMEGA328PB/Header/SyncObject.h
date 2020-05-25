/*
 * SyncObject.h
 *
 * Created: 05.05.2020 08:33:36
 *  Author: Berti
 */ 

#ifndef SYNCOBJECT_H_
#define SYNCOBJECT_H_

#include "Switcher.h"


// SyncObject is the common base for all synchronization objects (like (recursive) mutex, semaphore, event, ...).
// A sync object has either ownership or notification/event semantic, depending on the needs of the synchronization object using it.
// Common to all sync object is a waiting list and a flags field, other members depend on its semantic, ownership or notification.
//
// Possible states for ownership semantic:
//    - Free:              not owned, no one waiting 
//                         (m_pCurrentOrNextOwner == NULL, m_pWaitingList == NULL, m_HasPendingNewOwner == false)
//
//    - Owned:             has current owner, there may or may not be someone waiting
//                         (m_pCurrentOrNextOwner != NULL, m_pWaitingList != NULL or m_pWaitingList == NULL, m_HasPendingNewOwner == false)
//
//    - Pending new owner: has no current owner, there is someone waiting 
//                         (m_pCurrentOrNextOwner != NULL, m_pWaitingList != NULL, m_HasPendingNewOwner = true)
//                         The previous owner has released the sync object and woken up the next owner according to the waiting list at the time.
//                         Any task trying to acquire a sync object in this state has to queue up in the waiting list even if it has a higher priority
//                         than anyone already waiting (incl. the task woken by the previous owner!).
//
typedef struct  SyncObject_  // all members require the task switcher to be paused
{
  Task* m_pWaitingList;  // waiting list head, waiting tasks are sorted by priority (highest first) and FIFO within a given priority
                         // -> first task in waiting list is of the highest priority currently waiting and is the next to aquire
  union
  {
    uint8_t m_Flags;  // storage of bitfield flags below
    
    struct    
    {
      bool m_HasOwnershipSemantic : 1;  // set if sync object is used with ownership semantic, notification/event semantic otherwise
      bool m_HasPendingNewOwner : 1;    // set if state is "Pending new Owner"
    };
  };       
  
  union
  {
    struct  // members for ownership semantic
    {
      SyncObject* m_pAcquiredListNext;    // acquired list next pointer, next pointer for list of all currently acquired sync objects by current owner
      Task*       m_pCurrentOrNextOwner;  // current or next owner task, 
    };
    
    struct  // members for notification/event semantic 
    {
      Task* m_pNotificationList;  // list of all notified tasks, uses m_pWaitingListNext member in task struct
    };
  };    
} SyncObject;

#define SWITCHER_SYNCOBJECT_WITH_OWNERSHIP_STATIC_INIT()    {.m_pWaitingList = NULL, .m_Flags = 0, .m_HasOwnershipSemantic = true,  .m_pAcquiredListNext = NULL, .m_pCurrentOrNextOwner = NULL}
#define SWITCHER_SYNCOBJECT_WITH_NOTIFICATION_STATIC_INIT() {.m_pWaitingList = NULL, .m_Flags = 0, .m_HasOwnershipSemantic = false, .m_pNotificationList = NULL}

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
  
  // check for higher prio task in waiting list (someone may have queued up between prev owners release and us taking ownership!)
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
__attribute__((always_inline))
static inline void ReleaseSyncObject(SyncObject* syncObject, Task* task)  // TODO: really inline or not ...
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);
  SWITCHER_ASSERT(IsCurrentSyncObjectOwner(syncObject, task));
  SWITCHER_ASSERT(task->m_pAcquiredList != NULL);
  
  // remove sync object from tasks acquire list
  if (task->m_pAcquiredList == syncObject)
  {
    task->m_pAcquiredList = task->m_pAcquiredList->m_pAcquiredListNext;
  }
  else
  {
    SyncObject* syncObjectIter = task->m_pAcquiredList;
    
    SWITCHER_ASSERT(syncObjectIter->m_pAcquiredListNext != NULL);
    
    while(syncObjectIter->m_pAcquiredListNext != syncObject)
    {
      syncObjectIter = syncObjectIter->m_pAcquiredListNext;
      
      SWITCHER_ASSERT(syncObjectIter->m_pAcquiredListNext != NULL);
    }
    
    SWITCHER_ASSERT((syncObjectIter != NULL) && (syncObjectIter->m_pAcquiredListNext == syncObject));
    
    syncObjectIter->m_pAcquiredListNext = syncObject->m_pAcquiredListNext;
  }
  
  syncObject->m_pAcquiredListNext = NULL;

  // set new pending ownership if a task is in the waiting list
  if (syncObject->m_pWaitingList != NULL)
  {
    // set pending new ownership
    syncObject->m_pCurrentOrNextOwner = syncObject->m_pWaitingList;  // new owner is always in front for waiting list
    syncObject->m_HasPendingNewOwner = true;
    
    SWITCHER_DISABLE_INTERRUPTS();
    
    // wake new owner if he is sleeping
    if (syncObject->m_pCurrentOrNextOwner->m_SleepCount > 0)
    {
      syncObject->m_pCurrentOrNextOwner->m_SleepCount = 0;
      g_ActiveTasks++;
    }
    
    SWITCHER_ENABLE_INTERRUPTS();

    // task priority for new owner will be fixed up when next owner takes ownership of sync object
  }
  else
  {
    syncObject->m_pCurrentOrNextOwner = NULL;
  }
  
  // do we have to check the priority of the releasing task?
  if (task->m_Priority > task->m_BasePriority)
  {
    SWITCHER_ASSERT(task->m_pIsWaitingFor == NULL);
    
    // find highest priority in tasks acquired list that is higher than his base priority
    Priority newPrio = task->m_BasePriority;
    
    const SyncObject* syncObjectIter = task->m_pAcquiredList;
    
    while (syncObjectIter != NULL)
    {
      if ((syncObjectIter->m_pWaitingList != NULL) &&
          (syncObjectIter->m_pWaitingList->m_Priority > newPrio))  // first task in waiting list always has highest priority
      {
        newPrio = syncObjectIter->m_pWaitingList->m_Priority;
      }
      
      syncObjectIter = syncObjectIter->m_pAcquiredListNext;
    }
    
    task->m_Priority = newPrio;
  }
}


// Do not use directly! Internal use in SyncObject implementation only!
//
// expects: task switcher is currently paused
//          task is not already waiting for this or any other sync object
__attribute__((always_inline))
static inline void AddTaskToSyncObjectsWaitingList(SyncObject* syncObject, Task* task)
{
  SWITCHER_ASSERT(task->m_pWaitingListNext == NULL);
      
  // add task to waiting list
  if (syncObject->m_pWaitingList == NULL)
  {
    syncObject->m_pWaitingList = task;
  }
  else
  {
    // find correct spot in the waiting list
    Task* taskIter = syncObject->m_pWaitingList;
    
    while ((taskIter->m_pWaitingListNext != NULL) &&
    (taskIter->m_pWaitingListNext->m_Priority >= task->m_Priority))
    {
      taskIter = taskIter->m_pWaitingListNext;
    }
    
    SWITCHER_ASSERT((taskIter->m_pWaitingListNext == NULL) || (taskIter->m_pWaitingListNext->m_Priority < task->m_Priority));

    // insert task behind taskIter
    task->m_pWaitingListNext = taskIter->m_pWaitingListNext;
    taskIter->m_pWaitingListNext = task;
  }
  
  // set is waiting for member in task to sync object
  task->m_pIsWaitingFor = syncObject;  
}

// Do not use directly! Internal use in SyncObject implementation only!
//
// expects: task switcher is currently paused
//          task is currently waiting for this sync object
__attribute__((always_inline))
static inline void RemoveTaskFromSyncObjectsWaitingList(SyncObject* syncObject, Task* task)
{
  SWITCHER_ASSERT(task->m_pWaitingListNext != NULL);
  
  // remove task from waiting list
  if (syncObject->m_pWaitingList == task)
  {
    syncObject->m_pWaitingList = syncObject->m_pWaitingList->m_pWaitingListNext;
  }
  else
  {
    // find task in waiting list
    Task* taskIter = syncObject->m_pWaitingList;

    SWITCHER_ASSERT(taskIter->m_pWaitingListNext != NULL);
    
    while (taskIter->m_pWaitingListNext != task)
    {
      taskIter = taskIter->m_pWaitingListNext;
      
      SWITCHER_ASSERT(taskIter->m_pWaitingListNext != NULL);
    }
    
    SWITCHER_ASSERT(taskIter->m_pWaitingListNext == task);
    
    taskIter->m_pWaitingListNext = task->m_pWaitingListNext;
  }
  
  task->m_pWaitingListNext = NULL;
  task->m_pIsWaitingFor = NULL;
}

// expects: task switcher is currently paused
//          task is not already waiting for another sync object
//          if sync object has ownership semantic it must not be in state Free
__attribute__((always_inline))
static inline void QueueForSyncObject(SyncObject* syncObject, Task* task)  // TODO: really inline or not ...
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT((syncObject->m_HasOwnershipSemantic && !IsFreeSyncObject(syncObject) && !IsCurrentSyncObjectOwner(syncObject, task)) ||
                   !syncObject->m_HasOwnershipSemantic);
  SWITCHER_ASSERT(task->m_pIsWaitingFor == NULL);
  
  // add task to waiting list according to his priority
  AddTaskToSyncObjectsWaitingList(syncObject, task);
  
  // check for priority inversion, in case of ownership semantic, if there is a current owner and his priority is lower than our priority
  while ((syncObject != NULL) &&
         syncObject->m_HasOwnershipSemantic &&
         IsOwnedSyncObject(syncObject) &&
         (syncObject->m_pCurrentOrNextOwner->m_Priority < task->m_Priority))
  {
    // grant current owner our own priority
    syncObject->m_pCurrentOrNextOwner->m_Priority = task->m_Priority;
    
    // check if owner is currently waiting of another sync object so we can (recursively!) update his position in the waiting list and also update the owners priority if necessary
    if (syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor != NULL)
    {
      // fix position in waiting list!
      RemoveTaskFromSyncObjectsWaitingList(syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor, syncObject->m_pCurrentOrNextOwner);
      AddTaskToSyncObjectsWaitingList(syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor, syncObject->m_pCurrentOrNextOwner);      
    }
    
    // if owner is waiting for an sync object might also have to bump the owners priority of that sync object!
    syncObject = syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor;
  }
}

// expects: task switcher is currently paused
//          task is currently waiting for the sync object (and with ownership semantic we must not be the current owner, but may be pending new owner)
__attribute__((always_inline))
static inline void UnqueueFromSyncObject(SyncObject* syncObject, Task* task)  // TODO: really inline or not ...
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));  
  SWITCHER_ASSERT(syncObject->m_pWaitingList != NULL);
  SWITCHER_ASSERT(task->m_pIsWaitingFor == syncObject);
  SWITCHER_ASSERT(!IsCurrentSyncObjectOwner(syncObject, task));
  
  RemoveTaskFromSyncObjectsWaitingList(syncObject, task);
    
  // do we have to check the owners priority, only needed for ownership semantic
  while (syncObject->m_HasOwnershipSemantic &&
         IsOwnedSyncObject(syncObject) &&
         (syncObject->m_pCurrentOrNextOwner->m_BasePriority < syncObject->m_pCurrentOrNextOwner->m_Priority) &&
         (syncObject->m_pCurrentOrNextOwner->m_Priority == task->m_Priority))
  {
    Priority newPrio = syncObject->m_pCurrentOrNextOwner->m_BasePriority;
    
    // find new highest priority in all waiting lists of current owners acquired list
    const SyncObject* syncObjectIter = syncObject->m_pCurrentOrNextOwner->m_pAcquiredList;
    
    SWITCHER_ASSERT(syncObjectIter != NULL);

    while (syncObjectIter != NULL)
    {
      // only consider sync objects with ownership, first in waiting list has highest priority
      if (syncObjectIter->m_HasOwnershipSemantic &&
          (syncObjectIter->m_pWaitingList != NULL) && 
          (syncObjectIter->m_pWaitingList->m_Priority > newPrio))
      {
        newPrio = syncObjectIter->m_pWaitingList->m_Priority;
      }
    }

    // no need to trigger a forced switch here, the new priority can only be <= our own priority!
    SWITCHER_ASSERT(newPrio <= task->m_Priority);
    SWITCHER_ASSERT(newPrio >= syncObject->m_pCurrentOrNextOwner->m_BasePriority);
    
    // check if owner is currently waiting of another sync object so we can (recursively!) update his position in the waiting list and also update the owners priority
    if ((syncObject->m_pCurrentOrNextOwner->m_Priority != newPrio) &&
       (syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor != NULL))
    {
      // set new priority
      syncObject->m_pCurrentOrNextOwner->m_Priority = newPrio;
      
      // fix position in waiting list!
      RemoveTaskFromSyncObjectsWaitingList(syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor, syncObject->m_pCurrentOrNextOwner);
      AddTaskToSyncObjectsWaitingList(syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor, syncObject->m_pCurrentOrNextOwner);
    }
    else
    {
      // set new priority
      syncObject->m_pCurrentOrNextOwner->m_Priority = newPrio;
    }

    // if owner is waiting for an sync object with ownership semantic we might also have to bump the owners priority!    
    syncObject = syncObject->m_pCurrentOrNextOwner->m_pIsWaitingFor;
  }
}

#endif /* SYNCOBJECT_H_ */
