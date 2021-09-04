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

#include "printf.h"
#include "uart.h"
#include <avr/pgmspace.h>
#include <stdarg.h>
#include <stdlib.h>

/*-----------------28.08.99 22:49-------------------
 *   Simple printf function (no fp, and strings)
 *--------------------------------------------------*/

PROGMEM const char hex[] = "0123456789ABCDEF";

int ___printf_P(const char *format, ...)
{
    char format_flag;
    unsigned int u_val, div_val, base;
    va_list ap;

    va_start(ap, format);
    for (;;) {
        while ((format_flag = pgm_read_byte(format++)) !=
               '%') { // Until '%' or '\0'
            if (!format_flag) {
                va_end(ap);
                return (0);
            }
            uart_putchar(format_flag);
        }

        switch (format_flag = pgm_read_byte(format++)) {
        case 'c':
            format_flag = va_arg(ap, int);
        default:
            uart_putchar(format_flag);
            continue;
        case 's':
            uart_puts(va_arg(ap, char *));
            continue;
        case 'S':
            ___printf_P(va_arg(ap, char *));
            continue;
        case 'd':
            base = 10;
            div_val = 10000;
            goto CONVERSION_LOOP;
        case 'x':
            base = 16;
            div_val = 0x1000;

        CONVERSION_LOOP:
            u_val = va_arg(ap, int);
            if (format_flag == 'd') {
                // negative values
                if (((int)u_val) < 0) {
                    u_val = -u_val;
                    uart_putchar('-');
                }
                // eliminate left zeros
                while (div_val > 1 && div_val > u_val)
                    div_val /= 10;
            }
            do {
                uart_putchar(pgm_read_byte(&hex[u_val / div_val]));
                u_val %= div_val;
                div_val /= base;
            } while (div_val);
        }
    }
}
