/*
 * KRONE
 * Firmware versão 1.0b
 * Julho/2007
 *
 * Autor: Leandro A. F. Pereira		<leandro@tia.mat.br>
 */

#include "config.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adc.h"
#include "uart.h"

/*
 * Prototipos (rotinas de tratamento de modo)
 */

int mode_ping(void);
int mode_id(void);
int mode_sensor(void);
int mode_sensor_stop(void);
int mode_motor_left(void);
int mode_motor_right(void);
int mode_motor_stop(void);
int mode_motor_forward(void);
int mode_motor_backward(void);
int mode_motor_turn_left(void);
int mode_motor_turn_right(void);
int mode_krone_help(void);

/*
 * Definicoes e estruturas
 */
enum {
    MODE_IDLE,
    MODE_PING,
    MODE_ID,
    MODE_SENSOR,
    MODE_SENSOR_STOP,
    MODE_MOTOR_LEFT,
    MODE_MOTOR_RIGHT,
    MODE_SERVO_O,
    MODE_SERVO_P,
    MODE_SERVO_Q,
    MODE_SERVO_R,
    MODE_SOUND,
    MODE_SOUND_STOP,
    MODE_MOTOR_STOP,
    MODE_MOTOR_FORWARD,
    MODE_MOTOR_BACKWARD,
    MODE_MOTOR_TURN_LEFT,
    MODE_MOTOR_TURN_RIGHT,
    MODE_KRONE_HELP,
    N_MODES
};

void *modes[N_MODES] = {
    NULL,                  /* MODE_IDLE */
    mode_ping,             /* MODE_PING */
    mode_id,               /* MODE_ID */
    mode_sensor,           /* MODE_SENSOR */
    mode_sensor_stop,      /* MODE_SENSOR_STOP */
    mode_motor_left,       /* MODE_MOTOR_LEFT */
    mode_motor_right,      /* MODE_MOTOR_RIGHT */
    NULL,                  /* MODE_SERVO_O */
    NULL,                  /* MODE_SERVO_P */
    NULL,                  /* MODE_SERVO_Q */
    NULL,                  /* MODE_SERVO_R */
    NULL,                  /* MODE_SOUND */
    NULL,                  /* MODE_SOUND_STOP */
    mode_motor_stop,       /* MODE_MOTOR_STOP */
    mode_motor_forward,    /* MODE_MOTOR_FORWARD */
    mode_motor_backward,   /* MODE_MOTOR_BACKWARD */
    mode_motor_turn_left,  /* MODE_MOTOR_TURN_LEFT */
    mode_motor_turn_right, /* MODE_MOTOR_TURN_RIGHT */
    mode_krone_help,       /* MODE_KRONE_HELP */
};

typedef struct _Sensors Sensors;

struct _Sensors {
    char digital[4];
    ADC_Sample_t analog[4];
    int battery_cpu;
    int battery_motor;
    char extra[2];
};

/*
 * Variáveis globais
 */
int mode = MODE_IDLE, mode_old = MODE_IDLE;
int parameter = 0, parameter_old = 0;
char timer_intr_enabled = 0;
Sensors sensors, sensors_old;

/*
 * Funções auxiliares
 */
int parameter_get(void)
{
    char buffer[5] = "", b;
    int i = 0;

    do {
        b = uart_getchar_wait();

        if (b >= '0' && b <= '9') {
            buffer[i++] = b;
            buffer[i] = '\0';
        } else if (i > 0 && (b == '\n' || b == '\r')) {
            break;
        }
    } while (i < 5);

    return atoi(buffer);
}

void serial_my_isr(void)
{
    uint8_t buffer;

    if (uart_peekchar() == '\n' || uart_peekchar() == '\r') {
        do {
            (void)uart_getchar();
        } while (uart_peekchar() == '\n' || uart_peekchar() == '\r');
        return;
    }

    buffer = uart_getchar();

    mode_old = mode;
    parameter_old = parameter;

    if (buffer == 'M') {
        switch (uart_getchar()) {
        case 'n':
            mode = MODE_ID;
            break;
        case 's':
            mode = MODE_SENSOR;
            break;
        case 'f':
            mode = MODE_SENSOR_STOP;
            break;
        case 'e':
            mode = MODE_MOTOR_LEFT;
            parameter = parameter_get();
            break;
        case 'd':
            mode = MODE_MOTOR_RIGHT;
            parameter = parameter_get();
            break;
        case 'o':
            mode = MODE_SERVO_O;
            parameter = parameter_get();
            break;
        case 'p':
            mode = MODE_SERVO_P;
            parameter = parameter_get();
            break;
        case 'q':
            mode = MODE_SERVO_Q;
            parameter = parameter_get();
            break;
        case 'r':
            mode = MODE_SERVO_R;
            parameter = parameter_get();
            break;
        case 'M':
            mode = MODE_SOUND;
            parameter = parameter_get();
            break;
        case 'm':
            mode = MODE_SOUND_STOP;
            break;
        default:
            goto mode_idle;
        }
    } else if (buffer == 'K') {
        switch (uart_getchar()) {
        case 's':
            mode = MODE_MOTOR_STOP;
            break;
        case 'f':
            mode = MODE_MOTOR_FORWARD;
            parameter = parameter_get();
            break;
        case 'b':
            mode = MODE_MOTOR_BACKWARD;
            parameter = parameter_get();
            break;
        case 'r':
            mode = MODE_MOTOR_TURN_RIGHT;
            parameter = parameter_get();
            break;
        case 'l':
            mode = MODE_MOTOR_TURN_LEFT;
            parameter = parameter_get();
            break;
        default:
            goto mode_idle;
        }
    } else if (buffer == 'p') {
        if (uart_getchar() == 'i' && uart_getchar() == 'n' &&
            uart_getchar() == 'g') {
            mode = MODE_PING;
        }
    } else if (buffer == 'h') {
        if (uart_getchar() == 'e' && uart_getchar() == 'l' &&
            uart_getchar() == 'p') {
            mode = MODE_KRONE_HELP;
        }
    } else {
    mode_idle:
        mode = MODE_IDLE;
    }

    /* Limpa o buffer da serial. */
    while (uart_is_ready_recv() &&
           (uart_peekchar() != '\r' || uart_peekchar() != '\n')) {
        (void)uart_getchar();
    }
}

ISR(TIMER0_OVF_vect)
{
    /*
     * A leitura dos ADCs efetua uma média dos últimos CFG_ADC_AVERAGE
     * valores. A leitura é feita por interrupção, portanto eles estarão
     * sempre prontos no buffer, mesmo que uma conversão esteja sendo
     * efetuada neste momento.
     */
    sensors.analog[0] = adc_read(0);
    sensors.analog[1] = adc_read(1);
    sensors.analog[2] = adc_read(2);
    sensors.analog[3] = adc_read(3);
    sensors.battery_cpu = adc_read(4);
    sensors.battery_motor = adc_read(5);

    /*
     * A leitura dos sinais digitais é feita por polling, já que não há
     * a necessidade de utilizar interrupção. É bom lembrar que o ATMega8
     * não possui, também, entradas digitais com interrupções suficientes
     * para nosso caso.
     */
    sensors.digital[0] = !bit_is_clear(PINB, PB4);
    sensors.digital[1] = !bit_is_clear(PINB, PB5);
    sensors.digital[2] = !bit_is_clear(PINB, PB6);
    sensors.digital[3] = !bit_is_clear(PINB, PB7);

    sensors.extra[0] = sensors.extra[1] = 0;
}

void timer_interrupt_enable(void)
{
    if (!timer_intr_enabled) {
        TCNT0 = 0x00;
        TCCR0 |= (1 << CS02);
        TIMSK ^= (1 << TOIE0);

        timer_intr_enabled = 1;
    }
}

void timer_interrupt_disable(void)
{
    if (timer_intr_enabled) {
        TIMSK ^= (1 << TOIE0);
        timer_intr_enabled = 0;
    }
}

void io_init(void)
{
    DDRB = _BV(DDB1) | _BV(DDB2); /* saidas PWM */
    DDRD = _BV(DDD6)              /* motor 1 A */
           | _BV(DDD7)            /* motor 1 B */
           | _BV(DDD4)            /* motor 2 A */
           | _BV(DDD5);           /* motor 2 B */

    TCCR1A = _BV(WGM10) | _BV(COM1A1) | _BV(COM1B1);
    TCCR1B = _BV(CS11) | _BV(CS10) | _BV(WGM12);

    /* prescaler = 64
       isso dara uma frequencia de 125KHz

       se usar prescaler 8, teremos 1MHz, que eh mto
       para motores. */

    mode_motor_stop();
}

void motor_select_speed(int motor_no, int speed)
{
    int i1, i2;

    /*
     * Escolhe os pinos de controle do motor de acordo com uma
     * tabela. A segunda tabela, abaixo, identifica quais ações
     * são tomadas pelo driver do motor (L293D ou compatível)
     * de acordo com suas entradas (retirado do datasheet).
     */
    if (motor_no == 0) {
        i1 = _BV(DDD6);
        i2 = _BV(DDD7);

        OCR1A = (speed * 255) / 10;
    } else {
        i1 = _BV(DDD4);
        i2 = _BV(DDD5);

        OCR1B = (speed * 255) / 10;
    }

    /*
     *  --------------------
     *	En I1  I2  Action
     *  --------------------
     *	1  1   0   Right
     *	1  0   1   Left
     *	1  0/1 0/1 Fast Stop
     *	0  X   X   Slow Stop
     */

    if (speed == 11) {
        PORTD |= i1 | i2;
    } else if (speed >= 0 && speed <= 10) {
        PORTD |= i1;
        PORTD &= ~i2;
    } else {
        PORTD |= i2;
        PORTD &= ~i1;
    }
}

int mode_ping(void)
{
    uart_puts("pong\n");

    return mode_old;
}

int mode_id(void)
{
    uart_puts("rKrone\n");
    uart_puts("t100\n");
    uart_puts("sC007\n");

    return mode_old;
}

int mode_krone_help(void)
{
    uart_puts("\xd5\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
              "\xcd\xcd\xcd\xcd Krone \xcd\xcd\xcd\xcd"
              "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xb8\n");
    uart_puts("\xb3       Firmware versao 1.0      \xb3\n");
    uart_puts("\xb3 (C) 2007 Leandro A. F. Pereira \xb3\n");
    uart_puts("\xd4\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
              "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
              "\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd\xcd"
              "\xcd\xcd\xcd\xbe\n\n");

    uart_puts("Comandos do Krone [marcados com '*' necessitam parametro]\n\n");
    uart_puts(" Mn          - identificacao\n");
    uart_puts(" Ms          - entra no modo de leitura de sensores\n");
    uart_puts(" Mf          - sai do modo de leitura de sensores\n");
    uart_puts("*Me,Md       - aciona motor esquerdo, direito\n");
    uart_puts("*Mo,Mp,Mq,Mr - aciona servos O, P, Q, R\n");
    uart_puts("*MM          - aciona som\n");
    uart_puts(" Ms          - para o som\n\n");
    uart_puts(" Ks          - para os motores\n");
    uart_puts("*Kf,Kb       - aciona motores para frente, tras\n");
    uart_puts("*Kr,Kl       - vira para direita, esquerda\n\n");
    uart_puts(" help        - esta ajuda\n");
    uart_puts(" ping        - responde com \"pong\"\n\n");

    return mode_old;
}

int mode_sensor(void)
{
    timer_interrupt_enable();

    /*
     * Esta macro compara o valor do sensor com o anteriormente enviado,
     * e só manda os valores que efetivamente foram alterados. Como são
     * forçosamente inicializados com valores diferentes quando o AVR é
     * ligado, na primeira vez todos os sensores são mostrados.
     */
#if 0
#define SEND_SENSOR_VALUE(s, var)                                              \
    do {                                                                       \
        if (sensors_old.var != sensors.var) {                                  \
            sensors_old.var = sensors.var;                                     \
            uart_putchar(s);                                                   \
            uart_putint(sensors.var);                                          \
            uart_puts("\n");                                                   \
        }                                                                      \
    } while (0)
#else
#define SEND_SENSOR_VALUE(s, var)                                              \
    do {                                                                       \
        uart_putchar(s);                                                       \
        uart_putuint(sensors.var);                                             \
        uart_puts("\n");                                                       \
    } while (0)
#endif

    SEND_SENSOR_VALUE('a', digital[0]);
    SEND_SENSOR_VALUE('b', digital[1]);
    SEND_SENSOR_VALUE('c', analog[0]);
    SEND_SENSOR_VALUE('d', analog[1]);
    SEND_SENSOR_VALUE('e', digital[2]);
    SEND_SENSOR_VALUE('f', digital[3]);
    SEND_SENSOR_VALUE('g', analog[2]);
    SEND_SENSOR_VALUE('h', analog[3]);
    SEND_SENSOR_VALUE('i', battery_cpu);
    SEND_SENSOR_VALUE('j', battery_motor);
    SEND_SENSOR_VALUE('k', extra[0]);
    SEND_SENSOR_VALUE('l', extra[1]);

#undef SEND_SENSOR_VALUE

    return MODE_SENSOR;
}

int mode_sensor_stop(void)
{
    timer_interrupt_disable();

    return MODE_IDLE;
}

int mode_motor_left(void)
{
    motor_select_speed(0, parameter);

    return mode_old;
}

int mode_motor_right(void)
{
    motor_select_speed(1, parameter);

    return mode_old;
}

int mode_motor_stop(void)
{
    motor_select_speed(0, 11);
    motor_select_speed(1, 11);

    return mode_old;
}

int mode_motor_forward(void)
{
    motor_select_speed(0, parameter);
    motor_select_speed(1, parameter);

    return mode_old;
}

int mode_motor_backward(void)
{
    motor_select_speed(0, -parameter);
    motor_select_speed(1, -parameter);

    return mode_old;
}

int mode_motor_turn_right(void)
{
    motor_select_speed(0, 0);
    motor_select_speed(1, parameter);

    return mode_old;
}

int mode_motor_turn_left(void)
{
    motor_select_speed(0, parameter);
    motor_select_speed(1, 0);

    return mode_old;
}

int main(void)
{
    int (*mode_handler)(void) = NULL;

    /*
     * Inicializa os periféricos do microcontrolador.
     * 1) Inicializa as portas de I/O, definindo quais pinos são de
     * saída/entrada 2) Inicializa o ADC, com prescalar 32 (F_CPU / 32 entre os
     * valores obtidos no DS) 3) Muda o modo de espera para standby
     */
    io_init();
    uart_init(serial_my_isr);
    adc_init(ADC_PRESCALAR_AUTO);
    set_sleep_mode(SLEEP_MODE_STANDBY);

    /*
     * Inicializa as estruturas de sensores com valores diferentes, para que
     * sejam exibidos todos os sensores da primeira vez.
     */
    memset(&sensors_old, 0xff, sizeof(sensors_old));
    memset(&sensors, 0, sizeof(sensors));

    /*
     * Loop principal. Infinito até que o mundo acabe (onde mundo = bateria).
     *
     * Entra em modo de espera quando não há nada a fazer, economizando energia.
     * É acordado quando ocorre uma interrupção (no momento: recebeu da UART,
     * conversão do ADC terminada ou estouro do timer).
     *
     * Se o modo de funcionamento do Krone não for alterado, volta a dormir.
     *
     * Caso contrário, desliga as interrupções e executa a rotina de tratamento
     * de modo apropriado. Liga as interrupções em seguida. O próximo modo é
     * ditado por esta rotina (geralmente volta a ser MODE_IDLE, para que o
     * processador durma até que outro evento o acorde, ou mode_old, para que
     * volte ao modo anterior).
     *
     * Se não existir uma rotina de tratamento de modo, ignora o modo atual e
     * apenas manda o processador dormir.
     */
    while (1) {
        if (mode == MODE_IDLE) {
            sleep_cpu();
        } else if (modes[mode]) {
            mode_handler = modes[mode];
            cli();
            mode = mode_handler();
            sei();
        } else {
            mode = MODE_IDLE;
        }
    }
}
