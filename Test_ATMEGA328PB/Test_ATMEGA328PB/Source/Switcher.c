/*
 * Switcher.c
 *
 * Created: 15.02.2019 11:50:37
 *  Author: Berti
 */ 

#include "Switcher.h"

#include <inttypes.h>
#include <string.h>

#include <avr/interrupt.h>

#include "SwitcherConfig.h"


#define SWITCHER_STACK_SIZE 32

typedef enum {Yielded = 0, PreemptiveSwitch, ForcedSwitch} SwitchingSource;

static TickCount g_TickCount;

static uint8_t g_SwitcherStackData[SWITCHER_STACK_SIZE];


__attribute__((naked, used))
void YieldImpl();

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
static void* SwitcherImplCore(SwitchingSource source, void* stackPointer);

__attribute__((noinline, used))
static void SwitcherTickCore();


// Common asm code every switcher entry function (Yield and ISRs) starts with
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

void YieldImpl()
{
  cli();
  SwitcherEntryImpl(Yielded);
}

/*
  Layout of full task state on the tasks stack in oder of saving:
    R17
    R31 ... R30
    SREG
    R29 ... R28
    (RAMPZ)
    R27 ... R18
    R16 ... R0
    (RAMPX)
    (RAMPY)
    (RAMPD)
    (EIND)
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

#ifdef RAMPZ  // if present save RAMPZ and clear it before using Z, uses r28
  asm volatile ("in r28,%[rampz]  \n\t"
                "push r28         \n\t"
                "clr r28          \n\t"
                "out %[rampz],r28"
                :: [rampz] "I"(_SFR_IO_ADDR(RAMPZ)));
#endif

  asm volatile ("in r30,__SP_L__           \n\t"  // load SP into Z
                "in r31,__SP_H__           \n\t"

                "ldi r29, hi8(%[swrStack]) \n\t"  // load switcher stack address in Y
                "ldi r28, lo8(%[swrStack]) \n\t"

                "out __SP_H__,r29          \n\t"  // store switcher stack address from Y to SP and ...
                "sei                       \n\t"  // ... enable interrupts
                "out __SP_L__,r28          \n\t"

                "adiw r30,1                \n\t"  // increment SP in Z by one, we only have pre-decremnt store
                
                // save rest of registers
                "st -z,r27                 \n\t"
                "st -z,r26                 \n\t"
                "st -z,r25                 \n\t"
                "st -z,r24                 \n\t"
                "st -z,r23                 \n\t"                
                "st -z,r22                 \n\t"
                "st -z,r21                 \n\t"
                "st -z,r20                 \n\t"
                "st -z,r19                 \n\t"
                "st -z,r18                 \n\t"
                "st -z,r16                 \n\t"  // r17 already saved
                "st -z,r15                 \n\t"
                "st -z,r14                 \n\t"
                "st -z,r13                 \n\t"
                "st -z,r12                 \n\t"
                "st -z,r11                 \n\t"
                "st -z,r10                 \n\t"
                "st -z,r9                  \n\t"
                "st -z,r8                  \n\t"
                "st -z,r7                  \n\t"
                "st -z,r6                  \n\t"
                "st -z,r5                  \n\t"
                "st -z,r4                  \n\t"
                "st -z,r3                  \n\t"
                "st -z,r2                  \n\t"
                "st -z,r1                  \n\t"
                "st -z,r0                  \n\t"

                "clr __zero_reg__          \n\t"  // clear zero reg
                :: [swrStack] "i"(g_SwitcherStackData + (sizeof(g_SwitcherStackData) - 1)));

                // save and clear extension registers RAMPX, RAMPY, RAMPD and EIND if present
#ifdef RAMPX
  asm volatile ("in r0,%[rampx]             \n\t"
                "st -z,r0                   \n\t"
                "out %[rampx], __zero_reg__"
                :: [rampx] "I"(_SFR_IO_ADDR(RAMPX)));
#endif

#ifdef RAMPY
  asm volatile ("in r0,%[rampy]             \n\t"
                "st -z,r0                   \n\t"
                "out %[rampy], __zero_reg__"
                :: [rampy] "I"(_SFR_IO_ADDR(RAMPY)));
#endif

#ifdef RAMPD
  asm volatile ("in r0,%[rampd] \n\t"
                "st -z,r0       \n\t"
              "out %[rampd], __zero_reg__"
                :: [rampd] "I"(_SFR_IO_ADDR(RAMPD)));
#endif

#ifdef EIND
  asm volatile ("in r0,%[eind] \n\t"
                "st -z,r0      \n\t"
              "out %[eind], __zero_reg__"
                :: [eind] "I"(_SFR_IO_ADDR(EIND)));
#endif

  asm volatile ("sbiw r30,1             \n\t"  // decrement SP in Z by one
                
                "mov r24,r17            \n\t"  // setup parameters for SwitcherImplCore, source in r24 ...
                
#ifdef  __AVR_HAVE_MOVW__
                "movw r22,r30           \n\t"  // ... current task's stack pointer in r22/r23
#else
                "mov r22,r30            \n\t"
                "mov r23,r31            \n\t"
#endif                
                "call SwitcherImplCore  \n\t"  // call SwitcherImplCore
                
// TODO: implement partial task switch for (current task) == (next task), only save ABI releveant registers
//       unify with implementation of SWITCHER_TICK_VECTOR !
#if 0                
                "cp r28,r24             \n\t"  // check if old and now stack pointer are equal
                "cpc r29,r25            \n\t"
                "breq skipfullswitch    \n\t"
                
                "skipfullswitch:        \n\t"
#endif                
                
#ifdef  __AVR_HAVE_MOVW__                
                "movw r30,r24           \n\t"  // move returned new stack pointer into Z
#else
                "mov r30,r24            \n\t"
                "mov r31,r25            \n\t"
#endif
                
                "adiw r30,1             \n\t"  // increment SP in Z by one
                );
                
                // restore extension registers if present
#ifdef EIND
  asm volatile ("ld r0,z+ \n\t"
                "out %[eind],r0"
                :: [eind] "I"(_SFR_IO_ADDR(EIND)));
#endif

#ifdef RAMPD
  asm volatile ("ld r0,z+ \n\t"
                "out %[rampd],r0"
                :: [rampd] "I"(_SFR_IO_ADDR(RAMPD)));
#endif

#ifdef RAMPY
  asm volatile ("ld r0,z+ \n\t"
                "out %[rampy],r0"
                :: [rampy] "I"(_SFR_IO_ADDR(RAMPY)));
#endif

#ifdef RAMPX
  asm volatile ("ld r0,z+ \n\t"
                "out %[rampx],r0"
                :: [rampx] "I"(_SFR_IO_ADDR(RAMPX)));
#endif

                // restore bulk of registers
  asm volatile ("ld r0,z+  \n\t"
                "ld r1,z+  \n\t"
                "ld r2,z+  \n\t"
                "ld r3,z+  \n\t"
                "ld r4,z+  \n\t"
                "ld r5,z+  \n\t"
                "ld r6,z+  \n\t"
                "ld r7,z+  \n\t"
                "ld r8,z+  \n\t"
                "ld r9,z+  \n\t"
                "ld r10,z+ \n\t"
                "ld r11,z+ \n\t"
                "ld r12,z+ \n\t"
                "ld r13,z+ \n\t"
                "ld r14,z+ \n\t"
                "ld r15,z+ \n\t"
                "ld r16,z+ \n\t"
                "ld r18,z+ \n\t"  // r17 is restored later
                "ld r19,z+ \n\t"
                "ld r20,z+ \n\t"
                "ld r21,z+ \n\t"
                "ld r22,z+ \n\t"
                "ld r23,z+ \n\t"
                "ld r24,z+ \n\t"
                "ld r25,z+ \n\t"
                "ld r26,z+ \n\t"
                "ld r27,z+ \n\t"
                "ld r28,z+ \n\t"
                "ld r29,z  \n\t"

                "cli       \n\t"  // disable interrupts
                
                "out __SP_H__,r31 \n\t"  // load stack pointer from Z into SP
                "out __SP_L__,r30 \n\t"
                );
                
                SWITCHER_ASM_ENABLE_SWITCHING_IRQS();  // re-enable switcher IRQs

#ifdef RAMPZ
  asm volatile (// restore RAMPZ
                "pop r30 \n\t"
                "out %[rampz],r30"
                :: [rampz] "I"(_SFR_IO_ADDR(RAMPZ)))
                );                
#endif

  asm volatile ("pop r31            \n\t"  // SREG in r31, SREG_I is 0!
                
                "pop r30            \n\t"
                
                "cpi r17,%[yielded] \n\t"  // check if source is Yielded
                "brne reti_return   \n\t"  // branch to reti return if not

                "out __SREG__,r31   \n\t"  // restore SREG
                
                "pop r31            \n\t"
                "pop r17            \n\t"
                
                "sei                \n\t"  // we always return with interrupts enabled!
                
                "ret                \n\t"

              "reti_return:         \n\t"
                
                "out __SREG__,r31   \n\t"  // restore SREG
                
                "pop r31            \n\t"
                "pop r17            \n\t"
                                
                "reti"
                :: [yielded] "i"(Yielded));
}

void* SwitcherImplCore(SwitchingSource source, void* stackPointer)
{
  // TODO: ...
  
  ResetPreemptiveSwitchTimer();
  
  return stackPointer;
}

/*
  Layout of partial task state on the tasks stack in oder of saving:
    R31 ... R30    
    SREG
    (RAMPZ)
    R27 ... R18
    R1..R0
    (RAMPX)
    (RAMPY)
    (RAMPD)
    (EIND)
    
  Not saved registers due to ABI:
    R29 ... R28
    R16 ... R2
*/

ISR(SWITCHER_TICK_VECTOR, ISR_NAKED)
{
  asm volatile ("push r31         \n\t"  // save Z
                "push r30         \n\t"
                
                "in r31,__SREG__  \n\t"  // saving SREG
                "push r31         \n\t"                
                );
                
  SWITCHER_ASM_DISABLE_SWITCHING_IRQS();
  
#ifdef RAMPZ  // if present save RAMPZ and clear it before using Z, uses r31
  asm volatile ("in r31,%[rampz]  \n\t"
                "push r31         \n\t"
                "clr r31          \n\t"
                "out %[rampz],r31"
                :: [rampz] "I"(_SFR_IO_ADDR(RAMPZ)));
#endif

  asm volatile ("push r27                  \n\t"  // save X
                "push r26                  \n\t"

                "in r30,__SP_L__           \n\t"  // load original SP into Z
                "in r31,__SP_H__           \n\t"

                "ldi r27, hi8(%[swrStack]) \n\t"  // load switcher stack address in X
                "ldi r26, lo8(%[swrStack]) \n\t"

                "out __SP_H__,r27          \n\t"  // store switcher stack address from Y to SP and ...
                "sei                       \n\t"  // ... enable interrupts
                "out __SP_L__,r26          \n\t"
                
                "adiw r30,1                \n\t"  // increment SP in Z by one, we only have pre-decremnt store
                
                // save rest of registers
                "st -z,r25                 \n\t"
                "st -z,r24                 \n\t"
                "st -z,r23                 \n\t"
                "st -z,r22                 \n\t"
                "st -z,r21                 \n\t"
                "st -z,r20                 \n\t"
                "st -z,r19                 \n\t"
                "st -z,r18                 \n\t"
                "st -z,r1                  \n\t"
                "st -z,r0                  \n\t"                
                                
                "clr __zero_reg__          \n\t"  // clear zero reg
                :: [swrStack] "i"(g_SwitcherStackData + (sizeof(g_SwitcherStackData) - 1)));

#ifdef RAMPX
  asm volatile ("in r0,%[rampx]             \n\t"
                "st -z,r0                   \n\t"
                "out %[rampx], __zero_reg__"
                :: [rampx] "I"(_SFR_IO_ADDR(RAMPX)));
#endif

#ifdef RAMPY
  asm volatile ("in r0,%[rampy]             \n\t"
                "st -z,r0                   \n\t"
                "out %[rampy], __zero_reg__"
                :: [rampy] "I"(_SFR_IO_ADDR(RAMPY)));
#endif

#ifdef RAMPD
  asm volatile ("in r0,%[rampd]             \n\t"
                "st -z,r0                   \n\t"
                "out %[rampd], __zero_reg__"
                :: [rampd] "I"(_SFR_IO_ADDR(RAMPD)));
#endif

#ifdef EIND
  asm volatile ("in r0,%[eind]             \n\t"
                "st -z,r0                  \n\t"
                "out %[eind], __zero_reg__"
                :: [eind] "I"(_SFR_IO_ADDR(EIND)));
#endif

  asm volatile ("push r31                  \n\t"  // save original SP on stack
                "push r30                  \n\t"

                "call SwitcherTickCore     \n\t"  // call SwitcherTickCore C function
                
                "pop r30                   \n\t"  // restore original SP in Z
                "pop r31                   \n\t"
                );

#ifdef EIND
  asm volatile ("ld r0,z+ \n\t"
                "out %[eind],r0"
                :: [eind] "I"(_SFR_IO_ADDR(EIND)));
#endif

#ifdef RAMPD
  asm volatile ("ld r0,z+ \n\t"
                "out %[rampd],r0"
                :: [rampd] "I"(_SFR_IO_ADDR(RAMPD)));
#endif
                
#ifdef RAMPY
  asm volatile ("ld r0,z+ \n\t"
                "out %[rampy],r0"
                :: [rampy] "I"(_SFR_IO_ADDR(RAMPY)));
#endif

#ifdef RAMPX
  asm volatile ("ld r0,z+ \n\t"
                "out %[rampx],r0"
                :: [rampx] "I"(_SFR_IO_ADDR(RAMPX)));
#endif

                // restore bulk of registers                
  asm volatile ("ld r0,z+                  \n\t"
                "ld r1,z+                  \n\t"
                "ld r18,z+                 \n\t"
                "ld r19,z+                 \n\t"
                "ld r20,z+                 \n\t"
                "ld r21,z+                 \n\t"
                "ld r22,z+                 \n\t"
                "ld r23,z+                 \n\t"
                "ld r24,z+                 \n\t"
                "ld r25,z+                 \n\t"
                
                "ld r26,z+                 \n\t"  // restore X
                "ld r27,z                  \n\t"
                
                "cli                       \n\t"  // disable interrupts
                
                "out __SP_H__,r31          \n\t"  // load stack pointer from Z into SP
                "out __SP_L__,r30          \n\t"                
                );

#ifdef RAMPZ
  asm volatile (// restore RAMPZ
                "pop r30 \n\t"
                "out %[rampz],r30"
                :: [rampz] "I"(_SFR_IO_ADDR(RAMPZ)))
                );                
#endif

  SWITCHER_ASM_ENABLE_SWITCHING_IRQS();  // re-enable switcher IRQs
    
  asm volatile ("pop r30                   \n\t"  // restore SREG
                "out __SREG__,r30          \n\t"
                
                "pop r30                   \n\t"  // restore Z
                "pop r31                   \n\t"
  
                "reti");
}

void SwitcherTickCore()
{
  // 64 bit arithmetic produces horrible code (especially within an ISR!)
  // doing it manually produces denser AND faster code ...
  g_TickCount.m_TickCountLow++;
  
  if (g_TickCount.m_TickCountLow == 0)
  {
    g_TickCount.m_TickCountHigh++;
  }
  
  // TODO: switcher tick handling
  //         walk task list (start with current task) and decrement all sleep counters if >0
  //         find next highest priority task that has timed out (sleep counter went from 1 to 0)
  //         if this task has a higher priority then the current task, trigger an forced task switch  
}

void PauseSwitchingImpl()
{
  asm volatile ("in r18,__SREG__  \n\t"  // saving SREG
  
                "cli              \n\t"  // disable interrupts
                );
                
  SWITCHER_ASM_DISABLE_SWITCHING_IRQS();

  asm volatile ("out __SREG__,r18 \n\t"  // restore SREG
                
                "ret              \n\t"
                );
}

void ResumeSwitchingImpl()
{
  asm volatile ("in r18,__SREG__  \n\t"  // saving SREG
                
                "cli              \n\t"  // disable interrupts
                );
  
  SWITCHER_ASM_ENABLE_SWITCHING_IRQS();
  
  asm volatile ("out __SREG__,r18 \n\t"  // restore SREG
                
                "ret              \n\t"
                );
}

Bool AddTask(void* stackBuffer, size_t stackSize, TaskFunction taskFunction, void* taskParameter)
{
  // initial task state is setup to allow starting a new task by adding it to the tasklist and start it
  // by simply have the task switcher, switch to it
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

  if ((stackBuffer == NULL) ||
      (taskFunction == NULL) ||
      (stackSize < sizeof(InitialTaskState)))
  {
    return FALSE;
  }
    
  InitialTaskState* pInitialTaskState = stackBuffer + ((stackSize - 1) - sizeof(InitialTaskState));
    
  memset(pInitialTaskState, 0x00, sizeof(InitialTaskState));
  
  pInitialTaskState->m_ReturnAddress = &TaskStartup;
  pInitialTaskState->m_TaskFunctionAddress = taskFunction;
  pInitialTaskState->m_TaskFunctionParameter = taskParameter;
  
  // TODO: initialize task structure
  //       add new task into to tasklist, switcher will start it when switching to it
  
  return TRUE;
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

  taskFunction(taskParameter);
  
  PauseSwitching();  
  // TODO: remove task from tasklist  
  Yield();
  
  // WE SHOULD NEVER EVER REACH THIS POINT!
}

TickCount GetSwitcherTickCount()
{
  // TODO: disable interrupts!!!!
  return g_TickCount;
}
