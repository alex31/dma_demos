#include <ch.h>
#include <hal.h>
#include "stdutil.h"
#include "ttyConsole.h"
#include "hal_stm32_dma.h"

/*
  designed to run on a STM32F407G-DISC1 board
 */

// cf tables 42 and 43, page 309 of stm32f4 reference manuel : RM090
#define STM32_TIM1_UP_DMA_STREAM                STM32_DMA_STREAM_ID(2, 5)
#define STM32_TIM1_UP_DMA_CHANNEL               6


// dma configuration
static const DMAConfig dmaConfig = {
       .stream = STM32_TIM1_UP_DMA_STREAM,
       .channel = STM32_TIM1_UP_DMA_CHANNEL,
       .dma_priority =  0, // low prio (from low:0 to high:3)
       .irq_priority = 12, // low prio (from low:15 to high:2 [0,1 reserved])
       .direction = DMA_DIR_M2M, // memory to peripheral
       .psize = 4, // GPIO ODR register is 16 bits wide
       .msize = 4, // so we use same size for words in memory
       .inc_peripheral_addr = true, // always update ODR and no other registers
       .inc_memory_addr = true, // increment memory pointer
       .op_mode = DMA_CONTINUOUS_HALF_BUFFER
};

// an heartbeat task which prove that the system is alive
static THD_WORKING_AREA(waBlinker, 384);
static void blinker (void *arg)
{

  (void)arg;
  chRegSetThreadName("blinker");
  
  while (true) {
    palToggleLine(LINE_LED1);
    chThdSleepMilliseconds(1000);
  }
}


#define MEMORY_LEN 1024U

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

  uint32_t source[MEMORY_LEN] = {0};
  uint32_t dest[MEMORY_LEN] = {1};

  // this object will manage a stream of a dma controller
  DMADriver dmap;

  // start the UART associated to the interractive shell
  consoleInit();

  // start the heartbeat task
  chThdCreateStatic(waBlinker, sizeof(waBlinker), NORMALPRIO, &blinker, NULL);

  // initialize dma object
  dmaObjectInit(&dmap);

  // start dma peripheral
  dmaStart(&dmap, &dmaConfig);

  // launch oneShot DMA transfert from memory to GPIO peripheral
  dmaTransfertTimeout(&dmap, dest, (void *) source, MEMORY_LEN, TIME_INFINITE);

  // start the interractive shell
  consoleLaunch();  

  // wait until user pushes blue button
  while (palReadLine(LINE_BLUE_BUTTON) == PAL_LOW) {
    chThdSleepMilliseconds(10);
  }

  chThdSleep(TIME_INFINITE);
}
