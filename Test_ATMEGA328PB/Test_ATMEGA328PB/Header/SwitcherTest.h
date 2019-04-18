/*
 * SwitcherTest.h
 *
 * Created: 16.03.2019 13:23:32
 *  Author: Berti
 */ 

#ifndef SWITCHERTEST_H_
#define SWITCHERTEST_H_

#include "Switcher.h"

void TestTestSupport();

void YieldTest();

void PauseSwitchingTest();

void ResumeSwitchingTest();

void AddTaskTest();


void SwitcherTickISRTest();

void PreemptiveSwitchISRTest();

void ForcedSwitchISRTest();

#endif /* SWITCHERTEST_H_ */
