/*
 * TaskSwitcher.h
 *
 * Created: 27.03.2019 10:06:41
 *  Author: Berti
 */ 

#pragma once

#ifndef STS_TASKSWITCHER_H_
#define STS_TASKSWITCHER_H_

#include <avr/interrupt.h>

#include "SimpleTaskSwitcher/Types.h"


#define STS_DISABLE_INTERRUPTS()              \
  __attribute__ ((unused)) Byte sreg = SREG;  \
  __asm__ volatile ("cli" ::: "memory")

#define SOT_ENABLE_INTERRUPTS()               \
  SREG = sreg;                                \
  __asm__ volatile ("" ::: "memory")


void Foo();

#endif /* STS_TASKSWITCHER_H_ */
