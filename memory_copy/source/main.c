#include <ch.h>
#include <hal.h>
#include "stdutil.h"
#include "ttyConsole.h"
#include "hal_stm32_dma.h"
#include "benchTime.h"
#include <string.h>

/*
  designed to run on a STM32F407G-DISC1 board
 */

/*
** compile optimisée, 4096 octets ** 
DMA total=2956, mean=30, min=30, max=30 => 132Mo/s
MEMCPY total=3381, mean=34, min=33, max=33 => 115Mo/s

** compile optimisée, 128 octets **
DMA total=460, mean=5, min=5, max=5  => 26Mo/s
MEMCPY total=129, mean=2, min=2, max=2 => 94Mo/s
*/


// cf tables 42 and 43, page 309 of stm32f4 reference manuel : RM090
#define STM32_M2M_DMA_STREAM                STM32_DMA_STREAM_ID(2, 6)
#define STM32_M2M_DMA_CHANNEL               0



static void benchMemcpyWrapper(void *userData);
static void benchDmacpyWrapper(void *userData);

// dma configuration
static const DMAConfig dmaConfig = {
       .stream = STM32_M2M_DMA_STREAM,
       .channel = STM32_M2M_DMA_CHANNEL,
       .dma_priority =  0, // low prio (from low:0 to high:3)
       .irq_priority = 12, // low prio (from low:15 to high:2 [0,1 reserved])
       .direction = DMA_DIR_M2M, // memory to peripheral
       .msize = 4, // faster mode
       .psize = 4, // obviously same as msize in memory 2 memory mode
       .inc_peripheral_addr = true, // always update ODR and no other registers
       .inc_memory_addr = true, // increment memory pointer
       .op_mode = DMA_ONESHOT
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
static const uint32_t source[MEMORY_LEN] = {[0 ... MEMORY_LEN-1] = 0xFF00};
static uint32_t IN_DMA_SECTION(dest[MEMORY_LEN]);

// this object will manage a stream of a dma controller
DMADriver dmap;

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

 
  // start the UART associated to the interractive shell
  consoleInit();

  // start the heartbeat task
  chThdCreateStatic(waBlinker, sizeof(waBlinker), NORMALPRIO, &blinker, NULL);

  // initialize dma object
  dmaObjectInit(&dmap);

  // start dma peripheral
  dmaStart(&dmap, &dmaConfig);

  // start the interractive shell
  consoleLaunch();  


  // bench DMA transfert from memory to memory
  while (palReadLine(LINE_BLUE_BUTTON) == PAL_HIGH) {
    memset(dest, 42, sizeof(dest));
    const benchResults brDma = doBench(benchDmacpyWrapper, 100, (void *) MEMORY_LEN);
    
    DebugTrace("DMA total=%lu, mean=%lu, min=%lu, max=%lu",
	       brDma.totalMicroSeconds,
	       brDma.meanMicroSeconds, 
	       brDma.minMicroSeconds,  
	       brDma.maxMicroSeconds); 
    chDbgAssert(memcmp(source, dest, sizeof(source)) == 0, "Dma Copy failed");
    chDbgAssert(dest[0] == 0xFF00, "probable direction mismatch");
    
    // bench DMA transfert from memory to memory
    memset(dest, 42, sizeof(dest));
    const benchResults brMcpy = doBench(benchMemcpyWrapper, 100, (void *) sizeof(source));
    
    DebugTrace("MEMCPY total=%lu, mean=%lu, min=%lu, max=%lu",
	       brMcpy.totalMicroSeconds,
	       brMcpy.meanMicroSeconds, 
	       brMcpy.minMicroSeconds,  
	       brMcpy.maxMicroSeconds); 
    chDbgAssert(memcmp(source, dest, sizeof(source)) == 0, "Memcpy Copy failed");
    chDbgAssert(dest[0] == 0xFF00, "probable direction mismatch");
    chThdSleepSeconds(2);
  }

  chThdSleep(TIME_INFINITE);
}



static void benchMemcpyWrapper(void *userData)
{
  memcpy(dest, source, (size_t) userData);
}

static void benchDmacpyWrapper(void *userData)
{
  dmaTransfertTimeout(&dmap, (void *) source, dest, (size_t) userData, TIME_INFINITE);
}
