/*
    FreeRTOS V8.1.2 - Copyright (C) 2014 Real Time Engineers Ltd.
    All rights reserved
*/

//

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include"board_clk.h"
/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * THESE PARAMETERS ARE DESCRIBED WITHIN THE 'CONFIGURATION' SECTION OF THE
 * FreeRTOS API DOCUMENTATION AVAILABLE ON THE FreeRTOS.org WEB SITE.
 *
 * See http://www.freertos.org/a00110.html.
 *----------------------------------------------------------*/
#if(HS_CONFIG_ARCH_MIPS == 1)
#define configUSE_NEWLIB_REENTRANT                  1
#else
#define configUSE_NEWLIB_REENTRANT                  0
#endif
#define configUSE_PREEMPTION                        1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION     1
#define configCPU_CLOCK_HZ                          (HS_BOARD_CLK_CPU )                    //current cpu real clock.
#define configTIMER_RATE_HZ                         ( ( TickType_t ) (HS_BOARD_CLK_CPU/2)) //configCPU_CLOCK_HZ
#define configTICK_RATE_HZ                          ( 100 )
#define configUSE_16_BIT_TICKS                      0
#define configMAX_PRIORITIES                        ( 4 )
#define configMINIMAL_STACK_SIZE                    ( 1024 )
#define configISR_STACK_SIZE                        ( 512 )
#define configTOTAL_HEAP_SIZE                        ( ( size_t ) 0x400000)
#define configMAX_TASK_NAME_LEN                     ( 32 )
#define configUSE_TIMERS                              1

#define configTIMER_TASK_PRIORITY                     3
#define configTIMER_QUEUE_LENGTH                      36
#define configTIMER_TASK_STACK_DEPTH                  512
#define configUSE_TIME_SLICING                        1

#define configUSE_MUTEXES                             1
#define configUSE_RECURSIVE_MUTEXES                   1
#define configCHECK_FOR_STACK_OVERFLOW                1

#define configUSE_CO_ROUTINES                         0
/* Hook functions */
#define configUSE_IDLE_HOOK                           0
#define configUSE_TICK_HOOK                           0

#define configUSE_TRACE_FACILITY                       1

#define configSUPPORT_DYNAMIC_ALLOCATION                1
#define configUSE_COUNTING_SEMAPHORES                   1

/* The interrupt priority of the RTOS kernel */
#define configKERNEL_INTERRUPT_PRIORITY             0x01

/* The maximum priority from which API functions can be called */
#define configMAX_API_CALL_INTERRUPT_PRIORITY       0x03

/* The runtime statics switcher */
#define configGENERATE_RUN_TIME_STATS                   1
#if (configGENERATE_RUN_TIME_STATS == 1)
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() 
#define portGET_RUN_TIME_COUNTER_VALUE()         xTaskGetTickCount()
#endif

/* Prevent assert code from being used in assembly files */


/* Optional functions */



#define INCLUDE_vTaskPrioritySet                    1
#define INCLUDE_uxTaskPriorityGet                    1
#define INCLUDE_vTaskDelayUntil                        1
#define INCLUDE_vTaskDelay                            1

#define INCLUDE_uxTaskGetStackHighWaterMark         0
#define INCLUDE_xTaskGetCurrentTaskHandle           1

#define INCLUDE_vTaskDelete                            1
#define INCLUDE_vTaskSuspend                        0

#define INCLUDE_xTaskGetSchedulerState              1
#define INCLUDE_eTaskGetState                       1
#define INCLUDE_xSemaphoreGetMutexHolder            1
#define INCLUDE_xTimerPendFunctionCall              1

#if defined(ENABLE_TRACE)
#include "trace.h"
#endif

#endif    /* FREERTOSCONFIG_H */

