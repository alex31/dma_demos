/*
  Nom(s), prénom(s) du ou des élèves : 

  QUESTION 1 : influence de la suppression du tableau prendDeLaPlaceSurLaPile ?

 */
#include <ch.h>
#include <hal.h>
#include "globalVar.h"
#include "stdutil.h"
#include "ttyConsole.h"


/*
  Câbler une LED sur la broche C0


  ° connecter B6 (uart1_tx) sur PROBE+SERIAL Rx AVEC UN JUMPER
  ° connecter B7 (uart1_rx) sur PROBE+SERIAL Tx AVEC UN JUMPER
  ° connecter C0 sur led0

 */




static THD_WORKING_AREA(waBlinker, 384);
static void blinker (void *arg)
{

  (void)arg;
  chRegSetThreadName("blinker");
  int prendDeLaPlaceSurLaPile[40] __attribute__((unused)); 
  
  while (true) { 
    palToggleLine (LINE_A05_LED_GREEN); 	
    chThdSleepMilliseconds (100);
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
  chThdSleep (TIME_INFINITE);
}


