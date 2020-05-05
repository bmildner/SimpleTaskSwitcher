/*
 * SyncObject.h
 *
 * Created: 05.05.2020 08:33:36
 *  Author: Berti
 */ 

#ifndef SYNCOBJECT_H_
#define SYNCOBJECT_H_

#include "Switcher.h"


typedef struct  SyncObject_  // all members requires task switcher to be paused
{
  Task*       m_pWaitingList;       // waiting list head, waiting tasks are sorted by priority (highest first) and FIFO within a given priority
                                    // -> first task in waiting list is of the highest priority currently waiting and is the next to aquire
  SyncObject* m_pAcquiredListNext;  // acquired list next pointer, next pointer for list of all currently acquired sync objects by current owner
  Task*       m_pCurrentOwner;      // current owner task
} SyncObject;


// expects: task switcher is currently paused
__attribute__((always_inline))
static inline void AcquireSyncObject(SyncObject* syncObject, Task* task)
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
  SWITCHER_ASSERT((syncObject->m_pCurrentOwner == NULL) && (syncObject->m_pAcquiredListNext == NULL));
  
  syncObject->m_pCurrentOwner = task;
  syncObject->m_pAcquiredListNext = task->m_pAcquiredList;
  task->m_pAcquiredList = syncObject;
}


// TODO: if task has acquired a syncobject and is currently in the waiting list for another one while his prio is bumped his waiting list entry need to be re-sorted!!
//       same if the high prio task times out the waiting list entry also needs to be re-sorted !!

// expects: task switcher is currently paused
__attribute__((always_inline))
static inline void ReleaseSyncObject(SyncObject* syncObject, Task* task)  // TODO: really inline or not ...
{
  SWITCHER_ASSERT((syncObject != NULL) && (task != NULL));
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
    
    SWITCHER_ASSERT(syncObjectIter != NULL);
    SWITCHER_ASSERT(syncObjectIter->m_pAcquiredListNext == syncObject);
    
    syncObjectIter->m_pAcquiredListNext = syncObject->m_pAcquiredListNext;
  }
  
  // TODO: move acquire implementation into new owners code path!
  //       keep check for our prio here!!
  
  if (syncObject->m_pWaitingList == NULL)
  { // waiting list is empty
    syncObject->m_pAcquiredListNext = NULL;
    syncObject->m_pCurrentOwner = NULL;
  }
  else
  { // new owner in front for waiting list
    Task* newOwner = syncObject->m_pWaitingList;
    
    // remove new owner from waiting list
    syncObject->m_pWaitingList = syncObject->m_pWaitingList->m_pWaitingListNext;
    
    AcquireSyncObject(syncObject, newOwner);
    
    SWITCHER_DISABLE_INTERRUPTS();
    
    // wake new owner if sleeping
    if (newOwner->m_SleepCount > 0)
    {
      newOwner->m_SleepCount = 0;
      g_ActiveTasks++;
    }
    
    SWITCHER_ENABLE_INTERRUPTS();
    
    // TODO: check new owners prio!
    
    // do we have to change our priority
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
  SWITCHER_ASSERT(syncObject->m_pCurrentOwner != NULL);
  
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
  
  // check for priority inversion
  if (syncObject->m_pCurrentOwner->m_Priority < task->m_Priority)  // TODO: do we have to inherit prio changes recursively to all tasks  !?!?!?
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
  SWITCHER_ASSERT(syncObject->m_pCurrentOwner != NULL);
  SWITCHER_ASSERT(syncObject->m_pCurrentOwner != task);
  SWITCHER_ASSERT(syncObject->m_pWaitingList != NULL);
  
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
  
  // do we have to change the owners priority
  if (syncObject->m_pCurrentOwner->m_Priority == task->m_Priority)  // TODO: do we have to inherit prio changes recursively to all tasks which prio depends on this owner !?!?!?
  {
    Priority newPrio = syncObject->m_pCurrentOwner->m_BasePriority;
    
    // find new highest prio in all waiting lists of current owners acquired list
    SyncObject* syncObjectIter = syncObject->m_pCurrentOwner->m_pAcquiredList;
    
    SWITCHER_ASSERT(syncObjectIter != NULL);

    while (syncObjectIter != NULL)
    {
      if (syncObjectIter->m_pWaitingList->m_Priority > newPrio)
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

#define SWITCHER_SYNCOBJECT_STATIC_INIT() {.m_pWaitingList = NULL, .m_pAcquiredListNext = NULL, .m_pCurrentOwner = NULL}

#endif /* SYNCOBJECT_H_ */
