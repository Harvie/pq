#include "pq.h"

/// PQ callback example
int pq_demo_cb(pq_handle *pqh, void *cb_arg) {
  LOG(LL_INFO,((char *)cb_arg));
  mgos_msleep(900); //Dramatic pause

  //Note that if you again enqueue yourself to the front in here,
  //no other callback will ever be able to break your loop (=possibly unwanted infinite loop)
  pq_invoke_cb(pqh, pq_demo_cb, NULL, cb_arg, false, false);

  //You can use return value to re-enqueue the callback,
  //but there is no need to do that again in this case, since you've already called pq_invoke_cb() manualy
  //return false; //Enqueue yourself to the back again
  return true; //Do not enqueue yourself again
}

/// PQ callback example using return code to automaticaly re-enqueue
int pq_demo_return_cb(pq_handle *pqh, void *cb_arg) {
  LOG(LL_INFO,((char *)cb_arg));
  return false; //Enqueue yourself to the back again
  //return true; //Do not enqueue yourself again
}

/**
 * Example of PQ idle callback.
 *
 * This will be called immediately when queue becomes idle and then again at specified intervals.
 * Callback will know how long it's been since it became idle and can act upon it.
 * In this case it will suspend itself after several idling loops using `return true;`.
 *
 * In real-life scenarios it is advisable to execute time demanding jobs
 * only after queue has been idle for some time. otherwise latency of loop might get a lot worse.
 */
int pq_demo_idle_cb(pq_handle *pqh, void *cb_arg) {
  if(pqh->idle_count == 0) {
  	LOG(LL_INFO,("Queue %s just became idle, i am not doing anything yet", (char *)cb_arg));
  	//Not smart to do time intensive tasks here yet.
	return false; //Might as well just skip the first idle loop immediately in order to keep queue nice and responsive
  }
  LOG(LL_INFO,("Queue %s is in idle state for %zu ms", (char *)cb_arg, (pqh->idle_count * pqh->idle_interval_ms)));
  //Lets wait until we are idle for long enough

  if(pqh->idle_count >= 5) {
  	LOG(LL_INFO,("Queue %s has been idling for long enough, we can do some chores and then suspend.", (char *)cb_arg));
  	//We are bored. Might do some time intensive housekeeping tasks now.
  	//Once all chores are done, we don't need idling loop to be ticking anymore.
  	return true; //Suspend idling until queue gets full and then empty again
  }

  return false; //Keep idling periodicaly until queue has more jobs for us
}

/**
 * PQ callback which drops queue contents once called, effectively causing event loop to go idle (after timeout).
 *
 * (It is good idea to drop queue from inside callback to ensure there is no other callback simultaneously adding stuff to queue.)
 *
 * Note that this is purely for demonstration purposes and it does not usualy make sense to drop the queue.
 * Therefore there is no such functionality currently implemented in pq API.
 */
int pq_demo_empty_cb(pq_handle *pqh, void *cb_arg) {
  LOG(LL_INFO,("Dequeueing demo events (going back to idle)"));
  xQueueReset(pqh->queue);
  return true;
}

/// Handle of pq event loop used in this demo
pq_handle demo_pqh; ///< This has to be accessible from other threads!

/// Demo code for pq. Shows creation of new queue, enqueueing of new events and idle tasks.
/// It is loging the progress using mongoose LOG(LL_INFO,())) facility...
void pq_demo(void) {
  //Debug info
  LOG(LL_INFO,("FreeRTOS is running at %d Hz (ticks per second)", PQ_MS_TO_TICKS(1000)));

  /// Set some default queue parameters
  pq_set_defaults(&demo_pqh);
  demo_pqh.name = "PqTask"; //Human readable name of FreeRTOS task for debug purposes (up to 15 characters)
  demo_pqh.idle_interval_ms = 1000; //Idling interval in ms (tested 60000ms on esp32 and it worked, probably even more is possible...)
  demo_pqh.idle_cb = pq_demo_idle_cb; //This extra low priority task will run only with empty queue
  demo_pqh.idle_cb_arg = "IDLE ARG";
  demo_pqh.prio = MGOS_TASK_PRIORITY+1;

  /// Start the event loop
  pq_start(&demo_pqh);

  mgos_msleep(8500); //Wait to test idle callback while queue is empty

  /// Enqueue some callbacks
  LOG(LL_INFO,("Enqueueing demo events"));
  pq_invoke_cb(&demo_pqh, pq_demo_cb, NULL, (void *)"pq0    ", false, false);
  pq_invoke_cb(&demo_pqh, pq_demo_cb, NULL, (void *)"pq1    ", false, false);
  pq_invoke_cb(&demo_pqh, pq_demo_cb, NULL, (void *)"pq2    ", false, false);
  pq_invoke_cb(&demo_pqh, pq_demo_cb, NULL, (void *)"pq3 :-)", false, true); //This happy one gets queued to the front, prioritized for immediate execution
  pq_invoke_cb(&demo_pqh, pq_demo_return_cb, NULL, (void *)"pq4    ", false, false); //This will automaticaly re-enqueue itself

  /// After a while enqueue event which will empty whole queue, so we will become idle again.
  /// We are emptying the queue from callback to ensure there is no other callback running and enqueing more callbacks at the same time
  mgos_msleep(9000);
  pq_invoke_cb(&demo_pqh, pq_demo_empty_cb, NULL, NULL, false, true);
  mgos_msleep(8000);

  /// After some timeout continue booting Mongoose OS
  LOG(LL_INFO,("End of pq demo, will continue booting now. Good bye."));
}
