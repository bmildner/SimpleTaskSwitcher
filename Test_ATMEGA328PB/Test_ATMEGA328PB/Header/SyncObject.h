/*
 * SyncObject.h
 *
 * Created: 05.05.2020 08:33:36
 *  Author: Berti
 */ 

#ifndef SYNCOBJECT_H_
#define SYNCOBJECT_H_

#include "Switcher.h"


typedef struct  SyncObject_  // all members require the task switcher to be paused
{
  Task* m_pWaitingList;  // waiting list head, waiting tasks are sorted by priority (highest first) and FIFO within a given priority
                         // -> first task in waiting list is of the highest priority currently waiting and is the next to aquire

  union
  {
    uint8_t m_Flags;  // storage of bitfield flags below
    
    struct    
    {
      Bool m_HasOwnershipSemantic : 1;  // set if sync object is used with ownership semantic, notification/event semantic otherwise
      Bool m_HasPendingOwnership  : 1;  // set to indicate that the current set owner has not yet finished taking ownership (has not had CPU time to do so)
    };
  };       
  
  union
  {
    struct  // members for ownership semantic
    {
      SyncObject* m_pAcquiredListNext;  // acquired list next pointer, next pointer for list of all currently acquired sync objects by current owner
      Task*       m_pCurrentOwner;      // current owner task
    };
    
    struct  // members for notification/event semantic 
    {
      Task* m_pNotificationList;  // list of all notified tasks, uses m_pWaitingListNext member in task struct
    };
  };    
} SyncObject;

#define SWITCHER_SYNCOBJECT_WITH_OWNERSHIP_STATIC_INIT()    {.m_pWaitingList = NULL, .m_Flags = 0, .m_HasOwnershipSemantic = TRUE,  .m_pAcquiredListNext = NULL, .m_pCurrentOwner = NULL}
#define SWITCHER_SYNCOBJECT_WITH_NOTIFICATION_STATIC_INIT() {.m_pWaitingList = NULL, .m_Flags = 0, .m_HasOwnershipSemantic = FALSE, .m_pAcquiredListNext = NULL, .m_pCurrentOwner = NULL}


// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline void AcquireSyncObject(SyncObject* syncObject, Task* task)
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic);  
  SWITCHER_ASSERT((syncObject->m_HasPendingOwnership && (syncObject->m_pCurrentOwner == task)) || 
                  (syncObject->m_pCurrentOwner == NULL));
  SWITCHER_ASSERT(syncObject->m_pAcquiredListNext == NULL);
  
  // set new owner
  syncObject->m_pCurrentOwner = task;
  
  // add sync object to aquired list
  syncObject->m_pAcquiredListNext = task->m_pAcquiredList;
  task->m_pAcquiredList = syncObject;
  
  // reset pending ownership flag
  syncObject->m_HasPendingOwnership = FALSE;
}

// TODO: if task has acquired a syncobject and is currently in the waiting list for another one while his prio is bumped his waiting list entry needs to be re-sorted!!
//       same if the high prio task times out the waiting list entry also needs to be re-sorted !!

// expects: task switcher is currently paused
//          sync object has ownership semantic
__attribute__((always_inline))
static inline void ReleaseSyncObject(SyncObject* syncObject, Task* task)  // TODO: really inline or not ...
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(syncObject->m_HasOwnershipSemantic && !syncObject->m_HasPendingOwnership);
  SWITCHER_ASSERT(syncObject->m_pCurrentOwner == task);
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
  if (syncObject->m_pWaitingList == NULL)
  { 
    // waiting list is empty, no new owner
    syncObject->m_pCurrentOwner = NULL;
  }
  else
  {
    // set pending new ownership
    syncObject->m_pCurrentOwner = syncObject->m_pWaitingList;  // new owner is always in front for waiting list
    syncObject->m_HasPendingOwnership = TRUE;
        
    SWITCHER_DISABLE_INTERRUPTS();
    
    // wake new owner if he is sleeping
    if (syncObject->m_pCurrentOwner->m_SleepCount > 0)
    {
      syncObject->m_pCurrentOwner->m_SleepCount = 0;
      g_ActiveTasks++;
    }
    
    SWITCHER_ENABLE_INTERRUPTS();
    
    
    // do we have to change our priority?
    if (task->m_Priority > task->m_BasePriority)  // TODO: do we have to inherit prio changes recursively to all tasks  !?!?!?
    {
      
    }
  }
}


// expects: task switcher is currently paused
__attribute__((always_inline))
static inline void QueueForSyncObject(SyncObject* syncObject, Task* task)  // TODO: really inline or not ...
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT(((syncObject->m_HasOwnershipSemantic) && (syncObject->m_pCurrentOwner != NULL) && (syncObject->m_pCurrentOwner != task)) ||
                   !syncObject->m_HasOwnershipSemantic);
  
  // add task to waiting list
  if (syncObject->m_pWaitingList == NULL)
  {
    syncObject->m_pWaitingList = task;
  }
  else
  { // find our spot in the waiting list
    Task* taskIter = syncObject->m_pWaitingList;
    
    while ((taskIter->m_pWaitingListNext != NULL) &&
    (taskIter->m_pWaitingListNext->m_Priority >= task->m_Priority))
    {
      taskIter = taskIter->m_pWaitingListNext;
    }
    
    SWITCHER_ASSERT((taskIter->m_pWaitingListNext == NULL) || (taskIter->m_pWaitingListNext->m_Priority < task->m_Priority));
    
    task->m_pWaitingListNext = taskIter->m_pWaitingListNext;
    taskIter->m_pWaitingListNext = task;
  }
  
  // check for priority inversion if we have ownership semantic
  if (syncObject->m_HasOwnershipSemantic &&
      (syncObject->m_pCurrentOwner->m_Priority < task->m_Priority))  // TODO: do we have to inherit prio changes recursively to all tasks  !?!?!?
  {
    // grant current owner or own priority
    syncObject->m_pCurrentOwner->m_Priority = task->m_Priority;
    
    // TODO: no need for a forced switch because we are going to sleep anyway which will enter the task switcher!?!?
  }
}

// expects: task switcher is currently paused
__attribute__((always_inline))
static inline void UnqueueFromSyncObject(SyncObject* syncObject, Task* task)  // TODO: really inline or not ...
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT((syncObject->m_HasOwnershipSemantic && (syncObject->m_pCurrentOwner != NULL) && (syncObject->m_pCurrentOwner != task)) ||
                  (syncObject->m_HasOwnershipSemantic && syncObject->m_HasPendingOwnership && (syncObject->m_pCurrentOwner == task)) || 
                  !syncObject->m_HasOwnershipSemantic);
  SWITCHER_ASSERT(syncObject->m_pWaitingList != NULL);
  SWITCHER_ASSERT((syncObject->m_HasPendingOwnership && (syncObject->m_pWaitingList == syncObject->m_pCurrentOwner)) ||
                  !syncObject->m_HasPendingOwnership);
  
  // remove task from waiting list
  if (syncObject->m_pWaitingList == task)
  {
    syncObject->m_pWaitingList = syncObject->m_pWaitingList->m_pWaitingListNext;
  }
  else
  {
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
  
  // do we have to change the owners priority, only needed for ownership semantic
  if (syncObject->m_HasOwnershipSemantic &&
      (syncObject->m_pCurrentOwner != task) &&
      (syncObject->m_pCurrentOwner->m_Priority == task->m_Priority))  // TODO: do we have to inherit prio changes recursively to all tasks which prio depends on this owner !?!?!?
  {
    Priority newPrio = syncObject->m_pCurrentOwner->m_BasePriority;
    
    // find new highest prio in all waiting lists of current owners acquired list
    SyncObject* syncObjectIter = syncObject->m_pCurrentOwner->m_pAcquiredList;
    
    SWITCHER_ASSERT(syncObjectIter != NULL);

    while (syncObjectIter != NULL)
    {
      // we only have to check the first task in the waiting list because it is sorted by prio
      if ((syncObjectIter->m_pWaitingList != NULL) && 
          (syncObjectIter->m_pWaitingList->m_Priority > newPrio))
      {
        newPrio = syncObjectIter->m_pWaitingList->m_Priority;
      }
    }

    // no need to trigger a forced switch here, the new prio can only be <= our own prio!
    SWITCHER_ASSERT(newPrio <= task->m_Priority);
    SWITCHER_ASSERT(newPrio >= syncObject->m_pCurrentOwner->m_BasePriority);

    syncObject->m_pCurrentOwner->m_Priority = newPrio;
  }
}

#endif /* SYNCOBJECT_H_ */
