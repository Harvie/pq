#ifndef _MGOS_PQ_H
#define _MGOS_PQ_H

#include "stdint.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "mgos.h"
#include "mgos_freertos.h"
#include "mgos_core_dump.h" //mgos_cd_printf() prototype
#include "common/cs_dbg.h"

#ifdef __cplusplus
  extern "C" {
#endif

typedef struct _pq_handle pq_handle;

/**
  * Type of callback pointer to be registered by #pq_invoke_cb() .
  * Similar to legacy #mgos_cb_t type used by original Mongoose OS event queue
  */
typedef int (*pq_cb_t)(pq_handle *pqh, void *arg);


/**
 * Struct that holds all configuration needed for
 * queue creation, use and maintenance.
 * Pointer to this is used as queue handle.
 */
typedef struct _pq_handle {
  QueueHandle_t queue;		///< Handle to FreeRTOS queue created by #pq_start()
  TaskHandle_t task;		///< Handle to FreeRTOS task created by #pq_start()
  pq_cb_t idle_cb;		///< NULL or idle callback (eg.: for some ultra low-priority housekeeping tasks)
  void *idle_cb_arg;    	///< NULL or idle callback user argument pointer
  int32_t idle_interval_ms;	///< Interval to wait between idling cycles. @see #PQ_MS_TO_TICKS()
				///< (Tested 10 minutes = 60000ms, but even more should be possible)
  size_t idle_count;		///< How many times idle interval elapsed, since we've entered idle state (read-only)
  UBaseType_t prio;		///< FreeRTOS priority for event loop task
  size_t stack_size;		///< FreeRTOS stack size for event loop task
  UBaseType_t queue_len;	///< Length of event loop queue (how many callbacks/events can fit in)
  const char *name;		///< FreeRTOS task name, User specified Null terminated PQ name (~15 chars max), might be shown in log messages
} pq_handle;

/**
 * Internally used struct that is enqueued by #pq_invoke_cb() and received by #pq_task()
 */
typedef struct _pq_event {
  pq_cb_t cb;
  mgos_cb_t mgcb;
  pq_handle *pqh;
  void *arg;
} pq_event;

void pq_set_defaults(pq_handle *pqh);
bool pq_start(pq_handle *pqh);
IRAM bool pq_invoke_cb(pq_handle *pqh, pq_cb_t cb, mgos_cb_t mgcb, void *arg, bool from_isr, bool to_front);
size_t pq_waiting(pq_handle *pqh);
void pq_demo(void);

//Constants for PQ_MS_TO_TICKS() macro
#define PQ_MS_DEFAULT		 1000	///< By default wait 1 second between idle cycles
#define PQ_MS_NOWAIT		 0	///< Idle without delay (less than 3ms on esp32)
#define PQ_MS_FOREVER		-1	///< Wait forever
#define PQ_MS_SINGLE_TICK	-2	///< Wait for single tick (~10ms when using default 100Hz FreeRTOS ticks)

/**
  * Convert time interval in ms to freertos ticks.
  * This is used to set interval of idling loop.
  *
  * Can also use following special PQ_MS_* values instead of number of milliseconds
  * @see PQ_MS_NOWAIT @see PQ_MS_FOREVER @see PQ_MS_SINGLE_TICK
  */
#define PQ_MS_TO_TICKS(ms) ((ms) == PQ_MS_SINGLE_TICK ? 1 : ((ms) == PQ_MS_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(ms)))

/*
#define LOG(l, x)                                     \
  do {                                                \
    if (cs_log_print_prefix(l, __FILE__, __LINE__)) { \
      cs_log_printf x;                                \
    }                                                 \
  } while (0)
*/

#ifndef __FILENAME__
/// We don't want to log full paths, so we only use the filename
#define __FILENAME__ (strrchr("/" __FILE__, '/') + 1)
#endif //__FILENAME__

/**
 * Experimental loging macro. Don't use this, might be removed in future.
 *
 * It is highly reccomended to postpone loging till the end of critical section or ISR context
 * and then use mgos macro `LOG()` for loging instead of this.
 *
 * Unlike original `LOG()` this macro can log from critical section,
 * after #mgos_ints_disable() and in ISR context.
 *
 * Unfortunately it can currently log only to UART debug...
 * As a bonus this also prints the name of the FreeRTOS task.
 *
 * @deprecated use #LOG() instead!
 */
#define PQ_LOG(l, x)                                  \
  do {                                                \
    if(cs_log_level >= l)  {                          \
      mgos_cd_printf("%.24s:%d:%.16s:\t\t", __FILENAME__, __LINE__, pcTaskGetTaskName(NULL)); \
      mgos_cd_printf x;                               \
      mgos_cd_printf("\n");                           \
    }                                                 \
  } while (0)


/*
 * Following defines should be included from mgos_freertos.h
 * In Mongoose OS 2.18.0 this is not available yet, but it should be exported in upcoming release
 * https://github.com/cesanta/mongoose-os/issues/554
 * https://github.com/mongoose-os-libs/freertos/pull/2
 */

#ifndef MGOS_TASK_PRIORITY
#define MGOS_TASK_PRIORITY 5
#endif

#ifndef MGOS_TASK_STACK_SIZE_UNIT
#define MGOS_TASK_STACK_SIZE_UNIT 1
#endif

#ifndef MGOS_TASK_QUEUE_LENGTH
#define MGOS_TASK_QUEUE_LENGTH 32
#endif

#ifndef MGOS_TASK_STACK_SIZE_BYTES
#define MGOS_TASK_STACK_SIZE_BYTES 8192
#endif

#ifdef __cplusplus
  }
#endif

#endif //_MGOS_PQ_H
