/*************************************************************************
Title:    Very simple printf
Author:   Brajer Vlado, vlado.brajer@kks.s-net.net
          Daniel Quintero, danielqg@infonegocio.com
Date:     7-2001
Software: AVR-GCC with GNU binutiils
Hardware: any
License : This code is distributed under the terms and conditions of the
          GNU General Public License
**************************************************************************/

#ifndef _PRINTF_H_
#define _PRINTF_H_

#include <avr/pgmspace.h>

int ___printf_P(const char *format, ...);

#define printf_P(f, ...) ___printf_P(PSTR(f), #__VA_ARGS__)

#endif /* _PRINTF_H_ */
