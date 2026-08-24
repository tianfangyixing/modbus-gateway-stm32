#include "debug_log.h"
#include "SEGGER_RTT.h"
#include <stdio.h>

void debug_log_init(void)
{
    SEGGER_RTT_Init();
}

int fputc(int c, FILE *stream)
{
    unsigned char ch = (unsigned char)c;

    return debug_log_write(&ch, 1U) == 1U ? ch : EOF;
}
