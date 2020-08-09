#include <ch.h>
#include <hal.h>
#include "stdutil.h"
#include "ttyConsole.h"


/*
  Utiliser une nucleo DEVBOARDM4
 */




static THD_WORKING_AREA(waBlinker, 384);
static void blinker (void *arg)
{

  (void)arg;
  chRegSetThreadName("blinker");
  
  while (true) { 
    palToggleLine(LINE_LED1); 	
    chThdSleepMilliseconds(100);
  }
}



int main(void) {

    /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */

  halInit();
  chSysInit();
  initHeap();

  consoleInit();
  chThdCreateStatic(waBlinker, sizeof(waBlinker), NORMALPRIO, &blinker, NULL);

  consoleLaunch();  
  
  // main thread does nothing
  chThdSleep(TIME_INFINITE);
}
