/*
 * KRONE
 * Firmware versão 1.0b
 * Julho/2007
 *
 * Autor: Leandro A. F. Pereira		<leandro@tia.mat.br>
 *
 * Rotinas para UART. Para recepção, usa interrupções e um buffer circular.
 * Para transmissão, faz por polling. O tamanho do buffer pode ser configurado
 * em UART_RX_BUF_SIZE. Tamanho em bytes.
 *
 * Para transmissão, há uma sorte de rotinas auxiliares para impressão de
 * caracteres, strings e até mesmo números.
 */

#include "uart.h"
#include "config.h"
#include "printf.h"
#include <avr/interrupt.h>
#include <avr/io.h>
#include <inttypes.h>

static uint8_t rx_buf[UART_RX_BUF_SIZE];
static uint8_t rx_buf_wp, rx_buf_rp;

static void (*user_isr_routine)(void) = (void *)0;

void uart_init(void *user_isr)
{
#if F_CPU < 2000000UL && defined(U2X)
    UCSRA = _BV(U2X);
    UBRRL = (F_CPU / (8UL * UART_BAUD)) - 1;
#else
    UBRRL = (F_CPU / (16UL * UART_BAUD)) - 1;
#endif
    /*
     * - interrupções de recebimento
     * - ativar recebimento e transmissão
     */
    UCSRB = (1 << RXEN) | (1 << TXEN) | (1 << RXCIE);

    rx_buf_wp = rx_buf_rp = 0;
    user_isr_routine = user_isr;

    sei(); /* habilita interrupções */
}

void uart_putchar(char c)
{
    if (c == '\n')
        uart_putchar('\r');

    loop_until_bit_is_set(UCSRA, UDRE);
    UDR = c;
}

static inline void __uart_put_uint(unsigned int i)
{
    uart_putchar(i / 1000 + '0');
    i %= 1000;
    uart_putchar(i / 100 + '0');
    i %= 100;
    uart_putchar(i / 10 + '0');
    i %= 10;
    uart_putchar(i + '0');
}

void uart_putuint(unsigned int i) { __uart_put_uint(i); }

void uart_putint(int i)
{
    if (i < 0) {
        uart_putchar('-');
        i = -i;
    }

    __uart_put_uint((unsigned int)i);
}

void uart_puts(char *s)
{
    for (; *s; s++)
        uart_putchar(*s);
}

int uart_is_ready_send() { return bit_is_set(UCSRA, UDRE); }

SIGNAL(SIG_UART_RECV)
{
    uint8_t ch = UDR;
    uint8_t wp = (rx_buf_wp + 1) % UART_RX_BUF_SIZE;

    if (wp != rx_buf_rp) {
        rx_buf[rx_buf_wp] = ch;
        rx_buf_wp = wp;

        if (user_isr_routine && (ch == '\n' || ch == '\r'))
            user_isr_routine();
    }
}

int uart_is_ready_recv() { return (rx_buf_rp != rx_buf_wp); }

char uart_peekchar()
{
    if (rx_buf_rp != rx_buf_wp) {
        return rx_buf[rx_buf_rp];
    }

    return -1;
}

char uart_getchar()
{
    uint8_t ch = -1;

    cli();
    if (rx_buf_rp != rx_buf_wp) {
        ch = rx_buf[rx_buf_rp];
        rx_buf_rp = (rx_buf_rp + 1) % UART_RX_BUF_SIZE;
    }
    sei();

    return ch;
}

char uart_getchar_wait()
{
    uint8_t ch;
    void (*user_isr)(void) = user_isr_routine;

    user_isr_routine = (void *)0;

    sei();
    while (rx_buf_rp == rx_buf_wp)
        ;
    ch = uart_getchar();

    user_isr_routine = user_isr;

    return ch;
}
