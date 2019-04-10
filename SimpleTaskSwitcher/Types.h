/*
 * Types.h
 *
 * Created: 27.03.2019 10:03:47
 *  Author: Berti
 */ 

#pragma once

#ifndef STS_TYPES_H_
#define STS_TYPES_H_

#include <inttypes.h>

typedef uint8_t Byte;

#ifdef TRUE
# undef TRUE
#endif
#define TRUE 1

#ifdef FALSE
# undef FALSE
#endif
#define FALSE 0

typedef Byte Bool;

#endif /* STS_TYPES_H_ */
