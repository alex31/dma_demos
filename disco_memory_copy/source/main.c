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
  Resultats en compilation optimisée :


  **********
  système non chargé
  **********
DMA [4096 bytes] total=2957, mean=30, min=30, max=30 tput = 132.10 Mo/s
MEMCPY [4096 bytes] total=3378, mean=34, min=33, max=35 tput = 115.63 Mo/s

DMA [2048 bytes] total=1662, mean=17, min=17, max=17 tput = 117.51 Mo/s
MEMCPY [2048 bytes] total=1699, mean=17, min=17, max=17 tput = 114.95 Mo/s

DMA [1024 bytes] total=1014, mean=11, min=11, max=11 tput = 96.30 Mo/s
MEMCPY [1024 bytes] total=862, mean=9, min=9, max=9 tput = 113.29 Mo/s

DMA [512 bytes] total=690, mean=7, min=7, max=7 tput = 70.76 Mo/s
MEMCPY [512 bytes] total=442, mean=5, min=5, max=5 tput = 110.47 Mo/s

DMA [256 bytes] total=529, mean=6, min=6, max=6 tput = 46.15 Mo/s
MEMCPY [256 bytes] total=231, mean=3, min=3, max=3 tput = 105.68 Mo/s

DMA [128 bytes] total=458, mean=5, min=5, max=5 tput = 26.65 Mo/s
MEMCPY [128 bytes] total=103, mean=2, min=1, max=1 tput = 118.51 Mo/s

DMA [64 bytes] total=449, mean=5, min=5, max=5 tput = 13.59 Mo/s
MEMCPY [64 bytes] total=62, mean=1, min=1, max=1 tput = 98.44 Mo/s

DMA [32 bytes] total=445, mean=5, min=5, max=5 tput = 6.85 Mo/s
MEMCPY [32 bytes] total=44, mean=1, min=1, max=1 tput = 69.35 Mo/s

DMA [16 bytes] total=439, mean=5, min=5, max=5 tput = 3.47 Mo/s
MEMCPY [16 bytes] total=33, mean=1, min=1, max=1 tput = 46.23 Mo/s

DMA [8 bytes] total=437, mean=5, min=5, max=5 tput = 1.74 Mo/s
MEMCPY [8 bytes] total=29, mean=1, min=1, max=1 tput = 26.30 Mo/s

DMA [4 bytes] total=437, mean=5, min=5, max=5 tput = 0.87 Mo/s
MEMCPY [4 bytes] total=26, mean=1, min=1, max=1 tput = 14.67 Mo/s
 
  **********
  système très chargé
  **********

DMA [4096 bytes] total=10241, mean=103, min=30, max=121 tput = 38.14 Mo/s
MEMCPY [4096 bytes] total=245302, mean=2454, min=2401, max=2600 tput = 1.59 Mo/s


DMA [2048 bytes] total=10378, mean=104, min=104, max=104 tput = 18.81 Mo/s
MEMCPY [2048 bytes] total=123398, mean=1234, min=1201, max=1300 tput = 1.58 Mo/s


DMA [1024 bytes] total=10378, mean=104, min=104, max=104 tput = 9.40 Mo/s
MEMCPY [1024 bytes] total=62398, mean=624, min=601, max=699 tput = 1.56 Mo/s


DMA [512 bytes] total=10378, mean=104, min=104, max=104 tput = 4.70 Mo/s
MEMCPY [512 bytes] total=31799, mean=318, min=301, max=399 tput = 1.53 Mo/s


DMA [256 bytes] total=11476, mean=115, min=104, max=203 tput = 2.12 Mo/s
MEMCPY [256 bytes] total=16798, mean=168, min=101, max=400 tput = 1.45 Mo/s


DMA [128 bytes] total=504, mean=6, min=6, max=6 tput = 24.22 Mo/s
MEMCPY [128 bytes] total=8998, mean=90, min=2, max=100 tput = 1.35 Mo/s


DMA [64 bytes] total=493, mean=5, min=5, max=5 tput = 12.38 Mo/s
MEMCPY [64 bytes] total=5387, mean=54, min=1, max=100 tput = 1.13 Mo/s


DMA [32 bytes] total=588, mean=6, min=5, max=104 tput = 5.19 Mo/s
MEMCPY [32 bytes] total=2898, mean=29, min=1, max=100 tput = 1.05 Mo/s


DMA [16 bytes] total=879, mean=9, min=5, max=104 tput = 1.73 Mo/s
MEMCPY [16 bytes] total=1798, mean=18, min=1, max=99 tput = 0.84 Mo/s


DMA [8 bytes] total=781, mean=8, min=5, max=104 tput = 0.97 Mo/s
MEMCPY [8 bytes] total=1301, mean=14, min=1, max=99 tput = 0.58 Mo/s


DMA [4 bytes] total=876, mean=9, min=5, max=104 tput = 0.43 Mo/s
MEMCPY [4 bytes] total=1298, mean=13, min=1, max=99 tput = 0.29 Mo/s

*/


// cf tables 42 and 43, page 309 of stm32f4 reference manuel : RM090
#define STM32_M2M_DMA_STREAM                STM32_DMA_STREAM_ID(2, 6)
#define STM32_M2M_DMA_CHANNEL               0



static void startBench(void);
static void benchMemcpyWrapper(void *userData);
static void benchDmacpyWrapper(void *userData);
static void simulateCpuLoad(GPTDriver *gptd);

// heavy load simulation timer configuration
static const GPTConfig gptcfg = {
       // timer counter will increment @ 10khz
       // with this value, ChibiOS HAL will calculate a 16 bit predivider
       // from the source clock (168Mhz), so in this case
       // predivider will be set to 16800
       .frequency    = 100000,
       
       // callback, if set is called when counter
       // reach auto reload value (ARR) and then update (count again from 0)
       .callback     = &simulateCpuLoad,
       
       // dier : Dma Interrupt Enable Register is the link between timer and DMA
       // the bit UDE : Update Dma request Enable ask timer to emit
       // update signal when counter reach auto reload value.
      .dier =  STM32_TIM_DIER_UDE 
};


// dma configuration
static const DMAConfig dmaConfig = {
       .stream = STM32_M2M_DMA_STREAM,
       .dma_priority =  3, // high prio (from low:0 to high:3)
       .irq_priority = 2, // high prio (from low:15 to high:2 [0,1 reserved])
       .direction = DMA_DIR_M2M, // memory to memory
       .msize = 4, // fastest 
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

#define BENCH_REPEAT 100
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


  palEnableLineEvent(LINE_BLUE_BUTTON, PAL_EVENT_MODE_FALLING_EDGE);

  // first bench without any cpu load
  palWaitLineTimeout(LINE_BLUE_BUTTON, TIME_INFINITE);
  startBench();

  // then bench with heavy load done under interruption
  palWaitLineTimeout(LINE_BLUE_BUTTON, TIME_INFINITE);
  gptStart(&GPTD1, &gptcfg);
  gptStartContinuous(&GPTD1, 10); // ISR triggred @ 10khz
  startBench();
  gptStopTimer(&GPTD1);

  
  chThdSleep(TIME_INFINITE);
}



static void startBench(void)
{
 // bench DMA transfert from memory to memory
  size_t sizeInWord = MEMORY_LEN;

  while (true) {
    const size_t sizeInByte = sizeInWord * sizeof(uint32_t);
    memset(dest, 42, sizeInByte);
    const benchResults brDma = doBench(benchDmacpyWrapper, BENCH_REPEAT, (void *) sizeInWord);
    
    DebugTrace("DMA [%u bytes] total=%lu, mean=%lu, min=%lu, max=%lu tput = %.2f Mo/s",
	       sizeInByte,
	       brDma.totalMicroSeconds,
	       brDma.meanMicroSeconds, 
	       brDma.minMicroSeconds,  
	       brDma.maxMicroSeconds,
	       (sizeInByte * BENCH_REPEAT) * 1e6d /  (brDma.totalMicroSeconds * 1024 * 1024.0d)); 
    chDbgAssert(memcmp(source, dest, sizeInByte) == 0, "Dma Copy failed");
    chDbgAssert(dest[0] == 0xFF00, "probable direction mismatch");
    
    // bench DMA transfert from memory to memory
    memset(dest, 42, sizeof(dest));
    const benchResults brMcpy = doBench(benchMemcpyWrapper, BENCH_REPEAT, (void *) sizeInByte);
    
    DebugTrace("MEMCPY [%u bytes] total=%lu, mean=%lu, min=%lu, max=%lu tput = %.2f Mo/s\n\n",
	       sizeInByte,
	       brMcpy.totalMicroSeconds,
	       brMcpy.meanMicroSeconds, 
	       brMcpy.minMicroSeconds,  
	       brMcpy.maxMicroSeconds,
	       (sizeInByte * BENCH_REPEAT) * 1e6d /  (brMcpy.totalMicroSeconds * 1024 * 1024.0d)); 
    chDbgAssert(memcmp(source, dest, sizeInByte) == 0, "Memcpy Copy failed");
    chDbgAssert(dest[0] == 0xFF00, "probable direction mismatch");

    if (sizeInWord == 1)
      break;
    else
      sizeInWord /= 2;
  }
}

static void benchMemcpyWrapper(void *userData)
{
  memcpy(dest, source, (size_t) userData);
}

/*
  set STM32_DMA_USE_MUTUAL_EXCLUSION to TRUE in halconf.h to make dmaTransfert reentrant.
  If set to FALSE, it's a little bit faster, but nothing will prevent multiple threads to call the function 
  concurrently and trigger a hardware fault.
 */
static void benchDmacpyWrapper(void *userData)
{
#if STM32_DMA_USE_MUTUAL_EXCLUSION
  dmaAcquireBus(&dmap);
#endif
  dmaTransfert(&dmap, (void *) source, dest, (size_t) userData);
#if STM32_DMA_USE_MUTUAL_EXCLUSION
  dmaReleaseBus(&dmap);
#endif
}

static volatile float result = 1.0f;
static void simulateCpuLoad(GPTDriver *gptd)
{
  (void) gptd;

  float l = result;
  for (int i=0; i<400; i++) {
    l *= 1.1f;
  }
  result = l;
}
