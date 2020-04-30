/*
 * Switcher.c
 *
 * Created: 15.02.2019 11:50:37
 *  Author: Berti
 */ 

#include "Switcher.h"

#include <inttypes.h>
#include <string.h>
#include <stddef.h>

#include <avr/interrupt.h>

#include "SwitcherConfig.h"


#define SWITCHER_STACK_SIZE 32

typedef enum {Yielded = 0, PreemptiveSwitch, ForcedSwitch, SwitcherTick, TerminatingTask} SwitchingSource;

typedef struct  
{
  void* m_NewSP;         // new stack pointer after switch, may be same as current
  
  Task* m_PreviousTask;  // pointer to task struct of previous task, may be same as current task
} SwitcherResult;


Task* g_CurrentTask = NULL;

static uint8_t g_Tasks = 0;        // total number of tasks excl. IdelTask
static uint8_t g_ActiveTasks = 0;  // number of currently active task, needs IRQ protection!

static TickCount g_TickCount;

static uint8_t g_SwitcherStackData[SWITCHER_STACK_SIZE];

static Task    g_IdleTask;
static uint8_t g_IdleTaskStackBuffer[SWITCHER_TASK_STATE_SIZE * 2];  // TODO: correctly size stack!!


__attribute__((naked, used))
void YieldImpl();

__attribute__((naked, used, noreturn))
void TerminateTaskImpl();

__attribute__((naked, used))
void PauseSwitchingImpl();

__attribute__((naked, used))
void ResumeSwitchingImpl();


// Common part of task switcher asm implementation, calls SwitcherImplCore
__attribute__((naked, used))
static void SwitcherImpl();

// Task switcher core implementation in C
//   Takes switching source and SP of current task, returns SP of new task
__attribute__((noinline, used))
static SwitcherResult SwitcherCore(SwitchingSource source, void* stackPointer);

// Handles monotone switch tick, called from SwitchImplCore
__attribute__((always_inline, used))
static inline Task* SwitcherTickCore();


// Common asm code every switcher entry function (Yield, ISRs, ...) starts with
//   expects: interrupts are disabled
#define SwitcherEntryImpl(source)                                               \
  asm volatile ("push r17         \n\t"   /* save r17 */                        \
                "ldi r17,%[src]   \n\t"   /* setup switcher source */           \
                "jmp SwitcherImpl"        /* jump to switcher implementation */ \
                :: [src] "i"(source))

__attribute__((used))
static void TaskStartup();


ISR(SWITCHER_FORCED_SWITCH_VECTOR, ISR_NAKED)
{
  SwitcherEntryImpl(ForcedSwitch);
}

ISR(SWITCHER_PREEMPTIVE_SWITCH_VECTOR, ISR_NAKED)
{
  SwitcherEntryImpl(PreemptiveSwitch);
}

ISR(SWITCHER_TICK_VECTOR, ISR_NAKED)
{
  SwitcherEntryImpl(SwitcherTick);
}

void YieldImpl()
{
  SwitcherEntryImpl(Yielded);
}

void TerminateTaskImpl()
{
  SwitcherEntryImpl(TerminatingTask);
}

/*
  Layout of full task state on the tasks stack in oder of saving:
    R17            (source)    
    R31 ... R30    (scratch and load switcher SP)
    SREG
    R29 ... R28    (scratch and SP)
    [disable switcher IRQs]
    (RAMPY)
    [--- switch stack]
    R27 ... R18
    R1..R0
    (RAMPX)
    (RAMPZ)
    (RAMPD)
    (EIND)

    [call SwitcherImplCore]
    
  Only saved when actually switching to other task:
    R16 ... R2
    
    [call SwitcherStackCheck]
*/

// expects:
//          register r17 is saved on stack
//          r17 is loaded with switching source info
//          interrupts are disabled
void SwitcherImpl()
{                                                
  asm volatile ("push r31         \n\t"  // save Z
                "push r30         \n\t"
                
                "in r31,__SREG__  \n\t"  // saving SREG
                "push r31         \n\t"
                
                "push r29         \n\t"  // save Y
                "push r28         \n\t"                
                );

  SWITCHER_ASM_DISABLE_SWITCHING_IRQS();

#ifdef RAMPY  // if present save RAMPY and clear it before using Y, uses r31
  asm volatile ("in r31,%[rampy]  \n\t"
                "push r31         \n\t"
                "clr r31          \n\t"
                "out %[rampy],r31"
                :: [rampy] "I"(_SFR_IO_ADDR(RAMPY)));
#endif

  asm volatile ("in r28,__SP_L__           \n\t"  // load SP into Y
                "in r29,__SP_H__           \n\t"

                "ldi r31,hi8(%[swrStack])  \n\t"  // load switcher stack address in Z
                "ldi r30,lo8(%[swrStack])  \n\t"

                "out __SP_H__,r31          \n\t"  // store switcher stack address from Z to SP and ...
                "sei                       \n\t"  // ... enable interrupts
                "out __SP_L__,r30          \n\t"
                
                // save rest of registers
                "st y,r27                  \n\t"
                "st -y,r26                 \n\t"
                "st -y,r25                 \n\t"
                "st -y,r24                 \n\t"
                "st -y,r23                 \n\t"                
                "st -y,r22                 \n\t"
                "st -y,r21                 \n\t"
                "st -y,r20                 \n\t"
                "st -y,r19                 \n\t"
                "st -y,r18                 \n\t"                
                "st -y,r1                  \n\t"
                "st -y,r0                  \n\t"

                "clr __zero_reg__          \n\t"  // clear zero reg
                :: [swrStack] "i"(g_SwitcherStackData + (sizeof(g_SwitcherStackData) - 1)));

                // save and clear extension registers RAMPX, RAMPZ, RAMPD and EIND if present
#ifdef RAMPX
  asm volatile ("in r0,%[rampx]             \n\t"
                "st -y,__zero_reg__         \n\t"
                "out %[rampx], __zero_reg__"
                :: [rampx] "I"(_SFR_IO_ADDR(RAMPX)));
#endif

#ifdef RAMPZ
  asm volatile ("in r0,%[rampz]             \n\t"
                "st -y,__zero_reg__         \n\t"
                "out %[rampz], __zero_reg__"
                :: [rampz] "I"(_SFR_IO_ADDR(RAMPZ)));
#endif

#ifdef RAMPD
  asm volatile ("in r0,%[rampd]             \n\t"
                "st -y,__zero_reg__         \n\t"
                "out %[rampd], __zero_reg__"
                :: [rampd] "I"(_SFR_IO_ADDR(RAMPD)));
#endif

#ifdef EIND
  asm volatile ("in r0,%[eind]              \n\t"
                "st -y,__zero_reg__         \n\t"
                "out %[eind], __zero_reg__"
                :: [eind] "I"(_SFR_IO_ADDR(EIND)));
#endif

  asm volatile ("mov r24,r17            \n\t"  // setup parameters for SwitcherImplCore, source in r24 ...
                
#ifdef  __AVR_HAVE_MOVW__
                "movw r22,r28           \n\t"  // ... current task's stack pointer in r22/r23
#else
                "mov r22,r28            \n\t"
                "mov r23,r29            \n\t"
#endif                
                "call SwitcherCore      \n\t"  // call SwitcherCore function
                
                "cp r28,r22             \n\t"  // check if old and new stack pointer are equal
                "cpc r29,r23            \n\t"
                "breq skipfullswitch    \n\t"
                
                // save remaining full switch only registers
                "st -y,r16              \n\t"  // r17 already saved
                "st -y,r15              \n\t"
                "st -y,r14              \n\t"
                "st -y,r13              \n\t"
                "st -y,r12              \n\t"
                "st -y,r11              \n\t"
                "st -y,r10              \n\t"
                "st -y,r9               \n\t"
                "st -y,r8               \n\t"
                "st -y,r7               \n\t"
                "st -y,r6               \n\t"
                "st -y,r5               \n\t"
                "st -y,r4               \n\t"
                "st -y,r3               \n\t"
                "st -y,r2               \n\t"

                // TODO: call SwitcherStackCheck if stack check is enabled!
                 
#ifdef  __AVR_HAVE_MOVW__
                "movw r28,r24           \n\t"  // move returned new stack pointer into Y
#else
                "mov r28,r24            \n\t"
                "mov r29,r25            \n\t"
#endif
                
                // restore full switch only registers
                "ld r2,y+               \n\t"
                "ld r3,y+               \n\t"
                "ld r4,y+               \n\t"
                "ld r5,y+               \n\t"
                "ld r6,y+               \n\t"
                "ld r7,y+               \n\t"
                "ld r8,y+               \n\t"
                "ld r9,y+               \n\t"
                "ld r10,y+              \n\t"
                "ld r11,y+              \n\t"
                "ld r12,y+              \n\t"
                "ld r13,y+              \n\t"
                "ld r14,y+              \n\t"
                "ld r15,y+              \n\t"
                "ld r16,y+              \n\t"
                
                "skipfullswitch:        \n\t"

                );
                
                // restore extension registers if present
#ifdef EIND
  asm volatile ("ld r0,y+ \n\t"
                "out %[eind],r0"
                :: [eind] "I"(_SFR_IO_ADDR(EIND)));
#endif

#ifdef RAMPD
  asm volatile ("ld r0,y+ \n\t"
                "out %[rampd],r0"
                :: [rampd] "I"(_SFR_IO_ADDR(RAMPD)));
#endif

#ifdef RAMPZ
  asm volatile ("ld r0,y+ \n\t"
                "out %[rampz],r0"
                :: [rampz] "I"(_SFR_IO_ADDR(RAMPZ)));
#endif

#ifdef RAMPX
  asm volatile ("ld r0,y+ \n\t"
                "out %[rampx],r0"
                :: [rampx] "I"(_SFR_IO_ADDR(RAMPX)));
#endif

                // restore bulk of registers
  asm volatile ("ld r0,y+                      \n\t"
                "ld r1,y+                      \n\t"                
                "ld r18,y+                     \n\t"  // r17 is restored later
                "ld r19,y+                     \n\t"
                "ld r20,y+                     \n\t"
                "ld r21,y+                     \n\t"
                "ld r22,y+                     \n\t"
                "ld r23,y+                     \n\t"
                "ld r24,y+                     \n\t"
                "ld r25,y+                     \n\t"
                "ld r26,y+                     \n\t"
                "ld r27,y                      \n\t"

                "lds r30,%[currentTask]        \n\t"  // load address of current task into Z
                "lds r31,%[currentTask] + 1    \n\t"

                "cli                           \n\t"  // disable interrupts
                
                "out __SP_H__,r29              \n\t"  // load stack pointer from Y into SP
                "out __SP_L__,r28              \n\t"


                "ldd r29,z + %[counterOffset]  \n\t"  // load m_PauseSwitchingCounter into r29
                                
                "cpi r29,0                     \n\t"  // if counter is not 0 ...
                "brne skipRestoreSwitching     \n\t"  // jump to skipRestoreSwitching
                
                :: [currentTask] "i"(&g_CurrentTask), 
                   [counterOffset] "I"(offsetof(Task, m_PauseSwitchingCounter)));

                
                SWITCHER_ASM_ENABLE_SWITCHING_IRQS();  // re-enable switcher IRQs

  asm volatile ("skipRestoreSwitching:");

#ifdef RAMPY
  asm volatile (// restore RAMPY
                "pop r30 \n\t"
                "out %[rampy],r30"
                :: [rampy] "I"(_SFR_IO_ADDR(RAMPY)))
                );                
#endif

  asm volatile ("pop r28                  \n\t"  // restore Y
                "pop r29                  \n\t"

                "pop r31                  \n\t"  // SREG in r31, SREG_I is 0!
                
                "pop r30                  \n\t"  // restore R30
                
                "cpi r17,%[yielded]       \n\t"  // test if source is Yielded
                "breq ret_return          \n\t"  // branch to ret_return if equal

                "cpi r17,%[terminateTask] \n\t"  // test if source is TerminatingTask
                "breq ret_return          \n\t"  // branch to ret_return if equal

                "out __SREG__,r31         \n\t"  // restore SREG
                
                "pop r31                  \n\t"  // restore R31
                "pop r17                  \n\t"  // restore R17
                
                "reti                     \n\t"

              "ret_return:                \n\t"
                
                "out __SREG__,r31         \n\t"  // restore SREG
                
                "pop r31                  \n\t"  // restore R31
                "pop r17                  \n\t"  // restore R17
                     
                "sei                      \n\t"  // we always return with interrupts enabled!
                
                "ret"
                :: [yielded] "i"(Yielded), [terminateTask] "i"(TerminatingTask));
}

// <stackPointer> is 16 bytes off for full state switch, 
// we always store the SP pointing to the last saved byte not the first free slot!
// this avoids incrementing and/or decrementing the SP during saving and restoring registers
SwitcherResult SwitcherCore(SwitchingSource source, void* stackPointer)
{
  SwitcherResult result = {.m_PreviousTask = g_CurrentTask};
  Task* nextTask = g_CurrentTask;
  Bool done = FALSE;

  if (source != SwitcherTick)  
  {
    ResetPreemptiveSwitchTimer();
  }

  while (!done)
  {
    if (source == SwitcherTick)
    {
      Task* nextTaskCandidate = SwitcherTickCore();
      if (nextTask->m_Priority < nextTaskCandidate->m_Priority)
      {
        nextTask = nextTaskCandidate;
      }        
    }
    else
    {
      Task* taskListEntry = g_CurrentTask->m_pTaskListNext;
    
      while (taskListEntry != g_CurrentTask)
      {
        SWITCHER_DISABLE_INTERRUPTS();
      
        if (taskListEntry->m_SleepCount == 0)
        {
          if ((nextTask == g_CurrentTask) || (nextTask->m_Priority < taskListEntry->m_Priority))
          {
            nextTask = taskListEntry;
          }
        }
      
        SWITCHER_ENABLE_INTERRUPTS();
      
        taskListEntry = taskListEntry->m_pTaskListNext;
      }  // while
    }  // else
    
    if (source == TerminatingTask)
    {
      // TODO: assert(result.m_PreviousTask = g_CurrentTask)
      // TODO: assert(nextTask != g_CurrentTask)
      
      // remove task from task list
      Task* taskListEntry = g_CurrentTask->m_pTaskListNext;
      
      while (taskListEntry->m_pTaskListNext != g_CurrentTask)
      {
        taskListEntry = taskListEntry->m_pTaskListNext;
      }      
      // TODO: assert(taskListEntry->m_pTaskListNext == g_CurrentTask)
      
      taskListEntry->m_pTaskListNext = g_CurrentTask->m_pTaskListNext;
      
      g_Tasks--;
      
      // do not check for pending switcher IRQs
      done = TRUE;
      continue;
    }

    // check for pending switch IRQs, avoid exit and immediate re-entry of switcher implementation
    if (IsSwitcherTickPending())
    {
      ResetSwitcherTickIrqFlag();
      source = SwitcherTick;      
    }
    else if (IsPreemptiveSwitchPending())
    {
      ResetPreemptiveSwitchIrqFlag();
      source = PreemptiveSwitch;
    }
    else if (IsForcedSwitchPending())
    {
      ResetForcedSwitchIrqFlag();
      source = ForcedSwitch;
    }
    else
    {
      done = TRUE; 
    }
  }  // while

  if (nextTask != g_CurrentTask)
  {
    g_CurrentTask->m_pStackPointer = stackPointer - 15;
    
    SWITCHER_DISABLE_INTERRUPTS();
    g_CurrentTask = nextTask;
    SWITCHER_ENABLE_INTERRUPTS();
    
    result.m_NewSP = g_CurrentTask->m_pStackPointer;
  }
  else
  {
    result.m_NewSP = stackPointer;
  }    
    
  return result;
}

Task* SwitcherTickCore()
{
  // 64 bit arithmetic produces horrible code (especially within an ISR!)
  // doing it manually produces denser AND faster code ...
  g_TickCount.m_TickCountLow++;
  
  if (g_TickCount.m_TickCountLow == 0)
  {
    g_TickCount.m_TickCountHigh++;
  }
  
  Task* taskListEntry = g_CurrentTask->m_pTaskListNext;
  Task* nextTask = g_CurrentTask;
  
  while (taskListEntry != g_CurrentTask)
  {
    SWITCHER_DISABLE_INTERRUPTS();
    
    if ((taskListEntry->m_SleepCount > 0) && (taskListEntry->m_SleepCount < TimeoutInfinite))
    {
      taskListEntry->m_SleepCount--;
      
      if (taskListEntry->m_SleepCount == 0)
      {
        g_ActiveTasks++;        
        
        if (taskListEntry->m_Priority > nextTask->m_Priority)
        {
          nextTask = taskListEntry;
        }          
      }
    }
    
    SWITCHER_ENABLE_INTERRUPTS();

    taskListEntry = taskListEntry->m_pTaskListNext;
  }
    
  return nextTask; 
}

void Yield()
{
  SWITCHER_DISABLE_INTERRUPTS();
  
  if (g_CurrentTask->m_SleepCount < TimeoutMaximum)
  {
    g_CurrentTask->m_SleepCount++;
  }
  
  g_ActiveTasks--;
  
  asm volatile ("call YieldImpl" ::: "memory");  // saves two instructions, push/pop
  //YieldImpl();
  
  SWITCHER_ENABLE_INTERRUPTS();
}

void TerminateTask()
{  
  SWITCHER_DISABLE_INTERRUPTS();
    
  g_CurrentTask->m_SleepCount = TimeoutInfinite;
  g_ActiveTasks--;
  
  PauseSwitching();
  
  SWITCHER_ENABLE_INTERRUPTS();
 
  // iterate over all tasks waiting for us to terminate
  Task* waitingList = g_CurrentTask->m_pWaitingList;
  
  while (waitingList != NULL)
  {
    SWITCHER_DISABLE_INTERRUPTS();
    
    waitingList->m_SleepCount = 0;
    g_ActiveTasks++;
    
    SWITCHER_ENABLE_INTERRUPTS();
    
    waitingList = waitingList->m_pWaitingListNext;
  }
  
  cli();
  
  TerminateTaskImpl();  // TODO: generates rcall, expected rjmp !?!?!?!?
}

void Sleep(Timeout timeout)
{
  if (timeout > TimeoutNone)
  {    
    SWITCHER_DISABLE_INTERRUPTS();
  
    g_CurrentTask->m_SleepCount = timeout;
    
    Yield();
    
    SWITCHER_ENABLE_INTERRUPTS();
  }
  else
  {
    Yield();
  }    
}

void PauseSwitchingImpl()
{
  asm volatile ("lds r30,%[currentTask]        \n\t"  // load address of current task into Z
                "lds r31,%[currentTask] + 1    \n\t"

                "in r18,__SREG__               \n\t"  // saving SREG
                "cli                           \n\t"  // disable interrupts
                                 
                "ldd r19,z + %[counterOffset]  \n\t"  // load m_PauseSwitchingCounter into r19
                 
                // TODO: add test for overrun in debug builds
                "inc r19                       \n\t"  // increment m_PauseSwitchingCounter
                
                "std z + %[counterOffset],r19  \n\t"  // save m_PauseSwitchingCounter
                
                "cpi r19,1                     \n\t"  // if counter did not change from 0 to 1 ...
                "brne pauseDone                \n\t"  // ... jump to pauseDone
                :: [currentTask] "i"(&g_CurrentTask), 
                   [counterOffset] "I"(offsetof(Task, m_PauseSwitchingCounter)));
                
  SWITCHER_ASM_DISABLE_SWITCHING_IRQS();

  asm volatile ("pauseDone:        \n\t"
                
                "out __SREG__,r18  \n\t"              // restore SREG
                                
                "ret               \n\t"
                );
}

void ResumeSwitchingImpl()
{
  asm volatile ("lds r30,%[currentTask]        \n\t"  // load address of current task into Z
                "lds r31,%[currentTask] + 1    \n\t"

                "in r18,__SREG__               \n\t"  // saving SREG
                "cli                           \n\t"  // disable interrupts

                "ldd r19,z + %[counterOffset]  \n\t"  // load m_PauseSwitchingCounter into r19
                
                // TODO: add test for underruns in debug builds 
                "dec r19                       \n\t"  // decrement m_PauseSwitchingCounter
                
                "std z + %[counterOffset],r19  \n\t"  // save m_PauseSwitchingCounter
                
                "cpi r19,0                     \n\t"  // if counter did not change from 1 to 0 ...
                "brne resumeDone               \n\t"  // ... jump to resumeDone
                :: [currentTask] "i"(&g_CurrentTask),
                   [counterOffset] "I"(offsetof(Task, m_PauseSwitchingCounter)));
  
  SWITCHER_ASM_ENABLE_SWITCHING_IRQS();
  
  asm volatile ("resumeDone:       \n\t"
  
                "out __SREG__,r18  \n\t"              // restore SREG

                "ret               \n\t"
                );
}

static void IdleTask(void* param)
{
  (void)param;
  
  while (TRUE)
  {
    // TODO: implement ...
    Yield();
  }
}

SwitcherError Initialize(Task* mainTask)
{
  if (mainTask == NULL)
  {
    return SwitcherInvalidParameter;
  }

  // add main task
  memset(mainTask, 0x00, sizeof(*mainTask));
  
  mainTask->m_BasePriority = PriorityNormal;
  mainTask->m_Priority = PriorityNormal;
  mainTask->m_pTaskListNext = mainTask;
  
  SWITCHER_DISABLE_INTERRUPTS();  
        
  g_CurrentTask = mainTask;
  
  g_Tasks = 1;
  g_ActiveTasks = 1;
  
  PauseSwitching();
  
  SWITCHER_ENABLE_INTERRUPTS();

  // add idle task
  AddTask(&g_IdleTask, g_IdleTaskStackBuffer, sizeof(g_IdleTaskStackBuffer), &IdleTask, NULL, PriorityIdle);

  ResumeSwitching();
  
  return SwitcherNoError;
}

SwitcherError AddTask(Task* task, 
                      void* stackBuffer, size_t stackSize, 
                      TaskFunction taskFunction, void* taskParameter, 
                      Priority priority)
{
  // initial task state is setup to allow starting a new task by adding it to the tasklist and start it
  // by simply have the task switcher switch to it
  //
  // initial task state contains a full task state where the all registers are 0 and the return address points to
  // the TaskStartup function, additionally the task functions address and its parameter are on the stack
  typedef struct  
  {
    uint8_t m_Registers[SWITCHER_TASK_STATE_SIZE - SWITCHER_RETURN_ADDR_SIZE];

#if SWITCHER_RETURN_ADDR_SIZE > 2
    uint8_t m_ReturnAddressPadding[SWITCHER_RETURN_ADDR_SIZE - 2];
#endif
    
    void (*m_ReturnAddress)(void);  // return address from initial "task switch" to task startup function
    void* m_TaskFunctionParameter;
    TaskFunction m_TaskFunctionAddress;
  } InitialTaskState;
  
  // TODO: assert(stackSize >= SWITCHER_TASK_STATE_MIN_STACK_SIZE)
  //       assert(sizeof(TaskState) == SWITCHER_TASK_STATE_MIN_STACK_SIZE)

  if ((task == NULL) ||
      (stackBuffer == NULL) ||
      (taskFunction == NULL) ||
      ((priority < PriorityLowest) && (task != &g_IdleTask)) ||
      (stackSize < sizeof(InitialTaskState)))
  {
    return SwitcherInvalidParameter;
  }
  
  if (g_CurrentTask == NULL)
  {
    return SwitcherNotInitialized;
  }
  
  if (g_Tasks == MaxNumberOfTasks)
  {
    return SwitcherTooManyTasks;
  }
  
  InitialTaskState* pInitialTaskState = stackBuffer + ((stackSize - 1) - sizeof(InitialTaskState));
    
  memset(pInitialTaskState, 0x00, sizeof(InitialTaskState));
  
  pInitialTaskState->m_ReturnAddress = &TaskStartup;
  pInitialTaskState->m_TaskFunctionAddress = taskFunction;
  pInitialTaskState->m_TaskFunctionParameter = taskParameter;

  task->m_pStackPointer = pInitialTaskState;
  task->m_BasePriority = priority;
  task->m_Priority = priority;
  task->m_StackBuffer = stackBuffer;
  task->m_StackSize = stackSize;    
  task->m_pWaitingListNext = NULL;
  task->m_pWaitingList = NULL;
  task->m_SleepCount = 0;
  task->m_PauseSwitchingCounter = 0;

  PauseSwitching();
  
  task->m_pTaskListNext = g_CurrentTask->m_pTaskListNext;
  g_CurrentTask->m_pTaskListNext = task;
  
  g_Tasks++;
  
  SWITCHER_DISABLE_INTERRUPTS();
  g_ActiveTasks++;
  SWITCHER_ENABLE_INTERRUPTS();
  
  ResumeSwitching();
        
  // TODO: Yield() ?!?!?!?
  
  return SwitcherNoError;
}

void TaskStartup()
{
  TaskFunction taskFunction;
  void* taskParameter;
  
  // get task function address and parameter from stack
  asm volatile ("pop %B[param] \n\t"
                "pop %A[param] \n\t"
                "pop %B[func]  \n\t"
                "pop %A[func]  \n\t"
                : [param] "=e" (taskParameter), [func] "=e" (taskFunction));

  taskFunction(taskParameter);  // call task function
  
  TerminateTask();
    
  // WE SHOULD NEVER EVER REACH THIS POINT!
}

SwitcherError JoinTask(Task* task, Timeout timeout)
{
  PauseSwitching();
  
  if (IsKnownTask(task))
  {
    if (task == g_CurrentTask)
    {
      ResumeSwitching();
      
      return SwitcherInvalidParameter;
    }
    
    // add us to the tasks waiting list
    g_CurrentTask->m_pWaitingListNext = task->m_pWaitingList;
    task->m_pWaitingList = g_CurrentTask;
    
    Sleep(timeout);
    
    if (IsKnownTask(task))
    {
      ResumeSwitching();
      
      return SwitcherTimeout;
    }
  }
  
  ResumeSwitching();
  
  return SwitcherNoError;
}

TickCount GetSwitcherTickCount()
{
  TickCount count;
  
  SWITCHER_DISABLE_INTERRUPTS();
  
  count = g_TickCount;
  
  SWITCHER_ENABLE_INTERRUPTS();
  
  return count;
}

Bool IsKnownTask(const Task* task)
{
  Bool found = FALSE;
  
  PauseSwitching();
  
  Task* taskList = g_CurrentTask;
  
  do 
  {
    if (taskList == task)
    {
      found = TRUE;
      break;
    }
    
    taskList = taskList->m_pTaskListNext;
  } while (taskList != g_CurrentTask);
  
  ResumeSwitching();
  
  return found;
}
