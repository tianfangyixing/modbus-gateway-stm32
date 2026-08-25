/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
*                           www.segger.com                           *
**********************************************************************
*                                                                    *
*        SEGGER RTT * Real Time Transfer for embedded targets        *
*                  https://github.com/SEGGERMicro/RTT                *
*                                                                    *
**********************************************************************

---------------------------END-OF-HEADER------------------------------
Purpose : User configuration file for RTT.
          For available configuration,
          refer to SEGGER_RTT_ConfDefaults.h.

----------------------------------------------------------------------
*/

#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H


/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

#include "FreeRTOSConfig.h"

#define SEGGER_RTT_MAX_NUM_UP_BUFFERS (1)
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS (1)
#define SEGGER_RTT_PRINTF_BUFFER_SIZE (128)
  #define BUFFER_SIZE_UP                            (4096)
#define SEGGER_RTT_MAX_INTERRUPT_PRIORITY configMAX_SYSCALL_INTERRUPT_PRIORITY

#endif
/*************************** End of file ****************************/
