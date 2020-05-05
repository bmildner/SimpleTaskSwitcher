/*
 * Switcher.h
 *
 * Created: 15.02.2019 11:51:01
 *  Author: Berti
 */ 

#ifndef SWITCHER_H_
#define SWITCHER_H_

#include <inttypes.h>
#include <stddef.h>

#include <avr/io.h>
#include <avr/interrupt.h>

#ifdef TRUE
# undef TRUE
#endif
#define TRUE 1

#ifdef FALSE
# undef FALSE
#endif
#define FALSE 0

typedef uint8_t Bool;

#ifdef RAMPX
# define SWITCHER_RAMPX_SIZE 1
#else
# define SWITCHER_RAMPX_SIZE 0
#endif

#ifdef RAMPY
# define SWITCHER_RAMPY_SIZE 1
#else
# define SWITCHER_RAMPY_SIZE 0
#endif

#ifdef RAMPZ
# define SWITCHER_RAMPZ_SIZE 1
#else
# define SWITCHER_RAMPZ_SIZE 0
#endif

#ifdef RAMPD
# define SWITCHER_RAMPD_SIZE 1
#else
# define SWITCHER_RAMPD_SIZE 0
#endif

#ifdef EIND
# define SWITCHER_EIND_SIZE 1
#else
# define SWITCHER_EIND_SIZE 0
#endif

#define SWITCHER_EXTENSION_REGS_SIZE (SWITCHER_RAMPX_SIZE + SWITCHER_RAMPY_SIZE + SWITCHER_RAMPX_SIZE + SWITCHER_RAMPD_SIZE + SWITCHER_EIND_SIZE)

#ifdef __AVR_2_BYTE_PC__
# define SWITCHER_RETURN_ADDR_SIZE 2
#elif defined(__AVR_3_BYTE_PC__)
# define SWITCHER_RETURN_ADDR_SIZE 3
#else
# error "Unknown return address size"
#endif


#define SWITCHER_DISABLE_INTERRUPTS()            \
  __attribute__ ((unused)) uint8_t sreg = SREG;  \
  cli();                                         \
  asm volatile ("" ::: "memory")


#define SWITCHER_ENABLE_INTERRUPTS()          \
  SREG = sreg;                                \
  asm volatile ("" ::: "memory")

#ifdef DEBUG
# define SWITCHER_ASSERT(condition)   \
           if (!(condition))          \
           {                          \
             asm volatile ("break");  \
           }
#else
# define SWITCHER_ASSERT(condition)
#endif


// error codes
typedef enum {SwitcherNoError = 0,
              SwitcherNotInitialized,
              SwitcherInvalidParameter,
              SwitcherTimeout,
              SwitcherTooManyTasks,
              SwitcherResourceNotOwned,
              } SwitcherError;

// 64 bit arithmetic produces horrible code (especially within an ISR!)
// doing it manually produces denser AND faster code in an ISR ....
typedef struct
{
  union
  {
    uint64_t m_TickCount;
    
    struct
    {
      uint32_t m_TickCountLow;
      uint32_t m_TickCountHigh;
    };
  };
} TickCount;


typedef enum {PriorityIdle    = 0, 
              PriorityLowest  = 1,
              PriorityLow     = 64,
              PriorityNormal  = 128,
              PriorityHigh    = 192,
              PriorityHighest = 255
              } Priority;

typedef enum {TimeoutNone     = 0,
              TimeoutMaximum  = 0xfffe,
              TimeoutInfinite = 0xffff
              } Timeout;

enum {MaxNumberOfTasks = 0xff};

typedef struct Task_       Task;
typedef struct SyncObject_ SyncObject;

typedef struct Task_
{
  void*    m_pStackPointer;          // points to the last saved byte on the stack, not the first free byte
  
  uint16_t m_SleepCount;             // remaining number of switcher ticks this task is sleeping, requires IRQ protection
                                     // 0 == active, 0xffff = infinite
  uint8_t  m_PauseSwitchingCounter;  // tracks number of PauseSwitching calls, allows for nested Pause/Resume blocks

  Task*    m_pTaskListNext;          // task list next pointer, ring list
  Task*    m_pWaitingListNext;       // waiting list next pointer, linear list, task waits for an synchronization object
  Task*    m_pTaskWaitingList;       // pointer to waiting list, other tasks waiting for this task to terminate

  SyncObject* m_pAcquiredList;       // list of all currently acquired sync object by this task

  Priority m_BasePriority;           // assigned base priority
  Priority m_Priority;               // actual current priority

#if 0
  // stack check only
  void*    m_StackBuffer;            // may be NULL for main task
  uint16_t m_StackSize;              // may be 0 for main task
#endif  
} Task;

typedef void (*TaskFunction)(void*);

extern Task* g_CurrentTask;   // DO NOT TOUCH!
extern uint8_t g_ActiveTasks; // DO NOT TOUCH!

// TODO: move SyncObject in own header
typedef struct  SyncObject_
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
  if (syncObject->m_pCurrentOwner->m_Priority == task->m_Priority)  // TODO: do we have to inherit prio changes recursively to all tasks  !?!?!?
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

// 32 register + SREG + extension registers + return address 
#define SWITCHER_TASK_STATE_SIZE (32 + 1 + SWITCHER_EXTENSION_REGS_SIZE + SWITCHER_RETURN_ADDR_SIZE)

// task state + call into task function + address of task function + parameter for task function
#define SWITCHER_TASK_STATE_MIN_STACK_SIZE (SWITCHER_TASK_STATE_SIZE + sizeof(TaskFunction) + sizeof(void*))

SwitcherError Initialize(Task* mainTask);

// preserves the global interrupt flag and state of switcher IRQs
// interrupts are enabled during execution but state is restored before return
void Yield();

// terminates the current task, does not return
__attribute__ ((noreturn))
void TerminateTask();

// preserves the global interrupt flag and state of switcher IRQs
// interrupts are enabled during execution but state is restored before return
void Sleep(Timeout timeout);

// TODO: test if this causes more or less code then using ABI!
#define PauseSwitching() asm volatile ("call PauseSwitchingImpl" ::: "r18", "r19", "r30", "r31", "memory")

#define ResumeSwitching() asm volatile ("call ResumeSwitchingImpl" ::: "r18", "r19", "r30", "r31", "memory")

// <stackBuffer> points to the beginning of the stack memory, 
//               first stack location will be (stackBuffer + (stackSize - 1))!
SwitcherError AddTask(Task* task,
                      void* stackBuffer, size_t stackSize,
                      TaskFunction taskFunction, void* taskParameter,
                      Priority priority);

// returns SwitcherNoError if task is unknown or has been joined
// returns SwitcherInvalidParameter for task == g_CurrentTask
SwitcherError JoinTask(Task* task, Timeout timeout);

TickCount GetSwitcherTickCount();

Bool IsKnownTask(const Task* task);

#endif /* SWITCHER_H_ */
