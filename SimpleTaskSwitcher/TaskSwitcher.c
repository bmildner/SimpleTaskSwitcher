/*
 * TaskSwitcher.c
 *
 * Created: 27.03.2019 10:06:26
 *  Author: Berti
 */ 

#include <avr/io.h>

#include "TaskSwitcher.h"

volatile int i = 0;

void Foo()
{
  PORTB = 0x00;
  i = i + 1;
}