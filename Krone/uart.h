#ifndef __UART_H__
#define __UART_H__

#define UART_BAUD 9600
#define UART_RX_BUF_SIZE 8
#define UART_RX_INT_ENABLE

void uart_init(void *user_isr);
void uart_putchar(char c);
void uart_puts(char *s);
void uart_putint(int i);
void uart_putuint(unsigned int i);

int uart_is_ready_recv();
int uart_is_ready_send();

char uart_getchar();
char uart_getchar_wait();
char uart_peekchar();

#define uart_putsP(s) uart_puts(PSTR(s))

#endif /* __UART_H__ */
