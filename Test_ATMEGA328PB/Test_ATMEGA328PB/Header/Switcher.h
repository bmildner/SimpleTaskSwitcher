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

typedef void (*TaskFunction)(void*);

// 32 register + SREG + extension registers + return address 
#define SWITCHER_TASK_STATE_SIZE (32 + 1 + SWITCHER_EXTENSION_REGS_SIZE + SWITCHER_RETURN_ADDR_SIZE)

// task state + call into task function + address of task function + parameter for task function
#define SWITCHER_TASK_STATE_MIN_STACK_SIZE (SWITCHER_TASK_STATE_SIZE + sizeof(TaskFunction) + sizeof(void*))


#define Yield() asm volatile ("call YieldImpl" ::: "memory")

// TODO: test if this causes more or less code then using ABI!
#define PauseSwitching() asm volatile ("call PauseSwitchingImpl" ::: "r18", "r30", "r31", "memory")

#define ResumeSwitching() asm volatile ("call ResumeSwitchingImpl" ::: "r18", "r30", "r31", "memory")

// <stackBuffer> points to the beginning of the stack memory, 
//               first stack location will be (stackBuffer + (stackSize - 1))!
Bool AddTask(void* stackBuffer, size_t stackSize, TaskFunction taskFunction, void* taskParameter);

TickCount GetSwitcherTickCount();

#endif /* SWITCHER_H_ */
