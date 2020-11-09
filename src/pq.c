//Explanation: https://github.com/cesanta/mongoose-os/issues/537#issuecomment-703094198
//TODO: timer support

#include "FreeRTOS.h"
#include "queue.h"

#include "mgos.h"
#include "mgos_utils.h"
#include "mgos_system.h"
#include "mgos_time.h"
#include "mgos_timers.h"
#include "mgos_freertos.h"
#include "pq.h"

#define PQ_RUN_DEMO 0 ///< Should we run the demo code upon module load?

/**
 * Main event loop.
 * For each queue there is FreeRTOS task receiving events from it and executing them.
 * Such events consist of callback and argument and are sent to the queue by #pq_invoke_cb() call.
 *
 * There might also be idle callback run when queue is empty (given it was registered).
 * Idle callback is run immediately when queue gets empty and then periodicaly with specified interval.
 * You can check #pq_handle::idle_count variable to know how many times it has been called since queue became idle.
 * When `pqh->idle_count == 0`, it means that queue became idle right now.
 * You can calculate `(pqh->idle_count * pqh->idle_interval_ms)` to get ROUGH AND IMPRECISE idea of how long we've been idle.
 * If you want to reset the idle count (eg. when you use idle as watchdog and you want to feed it),
 * you can simply enqueue event with NULL callbacks.
 *
 * If callback (both idle or queued) returns true, it will not run again until next event, otherwise it is rescheduled automaticaly.
 */
void pq_task(void *arg) {
  pq_event e;
  pq_handle *pqh = arg;

  while (true) {
    //If there is some job in queue, then received and execute it
    while (xQueueReceive(pqh->queue, &e, 0)) {
      if(e.mgcb) e.mgcb(e.arg); //Backward compatible with mongoose callbacks //TODO: both callbacks as union in single argument???
      if(e.cb) {
        if(!e.cb(pqh, e.arg)) {
          /// If task returned false, then enqueue it again to the back.
          pq_invoke_cb(pqh, e.cb, NULL, e.arg, false, false);
        }
      }
    }

    //Queue is empty, idle until something appears in queue again
    pqh->idle_count = 0;
    do {
      if(!pqh->idle_cb || pqh->idle_cb(pqh, pqh->idle_cb_arg)) {
        /// If idle cb is NULL or it does returned true, then do not idle until next event.
        xQueuePeek(pqh->queue, &e, portMAX_DELAY); //Wait forever for new event in queue
        break;
      }
      pqh->idle_count++;
    } while(!xQueuePeek(pqh->queue, &e, PQ_MS_TO_TICKS(pqh->idle_interval_ms)));

  }
}

/**
  * Return number of events waiting in the queue.
  * This can be used in callbacks to know if there's a rush or not.
  *  @param pqh event loop handle
  */
size_t pq_waiting(pq_handle *pqh) {
	return uxQueueMessagesWaiting(pqh->queue);
}

/**
 * Send event to the queue to be executed
 *  @param pqh event loop handle
 *  @param cb callback of #pq_cb_t type to be executed (or NULL)
 *  @param mgcb backward compatibility for legacy callback of #mgos_cb_t type (or NULL)
 *  @param arg void pointer to be passed to the callback as argument (or NULL)
 *  @param from_isr do you call this from ISR? then set to true, otherwise false.
 *  @param to_front set true to enqueue to the front of the queue overtaking everything else. (be careful with this)
 *  @return true if succesfully enqueued
 */
IRAM bool pq_invoke_cb(pq_handle *pqh, pq_cb_t cb, mgos_cb_t mgcb, void *arg, bool from_isr, bool to_front) {
  //Sanity checks
  if(pqh == NULL) return false;
  if(pqh->queue == NULL) return false;

  //Event data to queue
  pq_event e = {.cb = cb, .mgcb = mgcb, .arg = arg, .pqh = pqh};

  if (from_isr) {
    BaseType_t should_yield = false;
    if(to_front) {
      if (!xQueueSendToFrontFromISR(pqh->queue, &e, &should_yield)) {
        return false;
      }
    } else {
      if (!xQueueSendToBackFromISR(pqh->queue, &e, &should_yield)) {
        return false;
      }
    }
    if(should_yield) portYIELD_FROM_ISR();;
    return true;
  } else {
    if(to_front) {
      return xQueueSendToFront(pqh->queue, &e, 10);
    } else {
      return xQueueSendToBack(pqh->queue, &e, 10);
    }
  }
}

/**
 * Create Queue based on #pq_handle and start #pq_task()
 * in newly created FreeRTOS task.
 * After calling this, everything is ready to use.
 */
bool pq_start(pq_handle *pqh) {
  //Sanity checks
  if(pqh == NULL) return false;
  if(pqh->queue != NULL) {
    LOG(LL_ERROR,("Parallel queue already exists!"));
    return false;
  }
  if(pqh->task != NULL) {
    LOG(LL_ERROR,("Parallel queue task already exists!"));
    return false;
  }

  //Default values
  if(pqh->queue_len == 0) pqh->queue_len = MGOS_TASK_QUEUE_LENGTH;
  if(pqh->stack_size == 0) pqh->stack_size = MGOS_TASK_STACK_SIZE_BYTES / MGOS_TASK_STACK_SIZE_UNIT;

  //Create queue
  pqh->queue = xQueueCreate(pqh->queue_len, sizeof(pq_event));
  if(pqh->queue == NULL) {
    LOG(LL_ERROR,("Cannot create parallel queue!"));
    return false;
  }

  //Start event loop task
  if(xTaskCreate(pq_task, pqh->name, pqh->stack_size, pqh, pqh->prio, &pqh->task) != pdPASS) {
    LOG(LL_ERROR,("Cannot create parallel queue task!"));
    return false;
  }

  return true;
}

/**
 * Fill #pq_handle struct with default values
 */
void pq_set_defaults(pq_handle *pqh) {
  pqh->name = "PQ";
  pqh->idle_interval_ms = PQ_MS_DEFAULT;
  pqh->idle_cb = NULL;
  pqh->idle_cb_arg = NULL;
  pqh->idle_count = 0;
  pqh->prio = MGOS_TASK_PRIORITY;
  pqh->queue_len = 0;
  pqh->stack_size = 0;
  pqh->queue = NULL;
  pqh->task = NULL;
}

/**
 * Mongoose OS module init callback, does virtualy nothing.
 * May run demo code if enabled.
 */
bool mgos_pq_init(void) {

  #if PQ_RUN_DEMO==1
    pq_demo();
  #endif //PQ_RUN_DEMO

  LOG(LL_INFO,("Parallel queue module loaded"));
  return true;

}
