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

// timer configuration
static const GPTConfig gptcfg = {
       // timer counter will increment @ 10khz
       // with this value, ChibiOS HAL will calculate a 16 bit predivider
       // from the source clock (168Mhz), so in this case
       // predivider will be set to 16800
       .frequency    = 10000,
       
       // callback, if set is called when counter
       // reach auto reload value (ARR) and then update (count again from 0)
       .callback     = NULL,
       
       // dier : Dma Interrupt Enable Register is the link between timer and DMA
       // the bit UDE : Update Dma request Enable ask timer to emit
       // update signal when counter reach auto reload value.
      .dier =  STM32_TIM_DIER_UDE 
};

// dma configuration
static const DMAConfig dmaConfig = {
       .stream = STM32_TIM1_UP_DMA_STREAM,
       .channel = STM32_TIM1_UP_DMA_CHANNEL,
       .dma_priority =  0, // low prio (from low:0 to high:3)
       .irq_priority = 12, // low prio (from low:15 to high:2 [0,1 reserved])
       .direction = DMA_DIR_M2P, // memory to peripheral
       .psize = 2, // GPIO ODR register is 16 bits wide
       .msize = 2, // so we use same size for words in memory
       .inc_peripheral_addr = false, // always update ODR and no other registers
       .inc_memory_addr = true, // increment memory pointer
       .circular = true // continuous transaction
};

// an heartbeat task which prove that the system is alive
static THD_WORKING_AREA(waBlinker, 384);
static void blinker (void *arg)
{

  (void)arg;
  chRegSetThreadName("blinker");
  
  while (true) {
    // briefly flash the 4 LEDs as an heartbeat signal
    GPIOD->ODR ^= 0b1111000000000000;
    chThdSleepMilliseconds(20);
    GPIOD->ODR ^= 0b1111000000000000;
    chThdSleepMilliseconds(980);
  }
}

// DMA works with ram and flash memory
// in this case, since the pattern is not going to change
// we leave it in flash.
// remove the const keyword and the memory buffer will be in ram
// dma buffer should be at least 32 bits aligned, and even more
// when burst is used
#define PATTERN_LEN	6U
static const uint16_t pattern[PATTERN_LEN] __attribute__((aligned(4))) = { 
			0b1000000000000000, 
			0b0100000000000000, 
			0b0010000000000000, 
			0b0001000000000000, 
			0b0010000000000000, 
			0b0100000000000000 
}; 

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

  // this object will manage a stream of a dma controller
  DMADriver dmap;

  // start the UART associated to the interractive shell
  consoleInit();

  // start the heartbeat task
  chThdCreateStatic(waBlinker, sizeof(waBlinker), NORMALPRIO, &blinker, NULL);

  // start timer peripheral
  gptStart(&GPTD1, &gptcfg);

  // make timer count
  gptStartContinuous(&GPTD1, 5000); // timer will update two times each second

  // initialize dma object
  dmaObjectInit(&dmap);

  // start dma peripheral
  dmaStart(&dmap, &dmaConfig);

  // launch continous DMA transfert from memory to GPIO peripheral
  dmaStartTransfert(&dmap, &GPIOD->ODR, (void *) pattern, PATTERN_LEN);

  // start the interractive shell
  consoleLaunch();  

  // wait until user pushes blue button
  while (palReadLine(LINE_BLUE_BUTTON) == PAL_LOW) {
    chThdSleepMilliseconds(10);
  }

  // halt the scheduler and the CPU : heartbeat stop to blink,
  // but DMA driven LEDs continues to move
  chSysHalt("dma continue to work");
}
