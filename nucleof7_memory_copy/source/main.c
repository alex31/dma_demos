#include <ch.h>
#include <hal.h>
#include "stdutil.h"
#include "ttyConsole.h"
#include "hal_stm32_dma.h"
#include "benchTime.h"
#include <string.h>

/*
  designed to run on a NUCLEO-F767ZI board
 */

/*
  Resultats en compilation optimisée :
DMA [4096 bytes] total=3112, mean=32, min=31, max=31 tput = 125.52 Mo/s
MEMCPY [4096 bytes] total=1016, mean=11, min=10, max=10 tput = 384.47 Mo/s


DMA [2048 bytes] total=1717, mean=18, min=17, max=17 tput = 113.75 Mo/s
MEMCPY [2048 bytes] total=514, mean=6, min=6, max=6 tput = 379.98 Mo/s


DMA [1024 bytes] total=1036, mean=11, min=10, max=11 tput = 94.26 Mo/s
MEMCPY [1024 bytes] total=270, mean=3, min=3, max=3 tput = 361.68 Mo/s


DMA [512 bytes] total=673, mean=7, min=7, max=7 tput = 72.55 Mo/s
MEMCPY [512 bytes] total=142, mean=2, min=2, max=2 tput = 343.86 Mo/s


DMA [256 bytes] total=498, mean=5, min=5, max=5 tput = 49.02 Mo/s
MEMCPY [256 bytes] total=80, mean=1, min=1, max=1 tput = 305.17 Mo/s


DMA [128 bytes] total=415, mean=5, min=4, max=4 tput = 29.41 Mo/s
MEMCPY [128 bytes] total=49, mean=1, min=1, max=1 tput = 249.12 Mo/s


DMA [64 bytes] total=412, mean=5, min=4, max=4 tput = 14.81 Mo/s
MEMCPY [64 bytes] total=29, mean=1, min=1, max=1 tput = 210.46 Mo/s


DMA [32 bytes] total=417, mean=5, min=4, max=4 tput = 7.31 Mo/s
MEMCPY [32 bytes] total=25, mean=1, min=1, max=1 tput = 122.07 Mo/s


DMA [16 bytes] total=406, mean=5, min=4, max=4 tput = 3.75 Mo/s
MEMCPY [16 bytes] total=18, mean=1, min=1, max=1 tput = 84.77 Mo/s


DMA [8 bytes] total=406, mean=5, min=4, max=4 tput = 1.87 Mo/s
MEMCPY [8 bytes] total=19, mean=1, min=1, max=1 tput = 40.15 Mo/s


DMA [4 bytes] total=408, mean=5, min=4, max=4 tput = 0.93 Mo/s
MEMCPY [4 bytes] total=15, mean=1, min=1, max=1 tput = 25.43 Mo/s


*/


// cf tables 42 and 43, page 309 of stm32f4 reference manuel : RM090
#define STM32_M2M_DMA_STREAM                STM32_DMA_STREAM_ID(2, 6)
#define STM32_M2M_DMA_CHANNEL               0

#define AXIM_TO_ICTM(x) ((uint8_t *) x - (0x08000000 - 0x00200000))

static void benchMemcpyWrapper(void *userData);
static void benchDmacpyWrapper(void *userData);
static void showWhere(const char* name, const void *adr);

#define SW(x) showWhere(#x, x)

// dma configuration
static const DMAConfig dmaConfig = {
       .stream = STM32_M2M_DMA_STREAM,
       .channel = STM32_M2M_DMA_CHANNEL,
       .dma_priority =  3, // low prio (from low:0 to high:3)
       .irq_priority =  2, // low prio (from low:15 to high:2 [0,1 reserved])
       .direction = DMA_DIR_M2M, // memory to peripheral
       .activate_dcache_sync = false,
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
    palToggleLine(LINE_LD1_GREEN);
    chThdSleepMilliseconds(1000);
  }
}

#define BENCH_REPEAT 100
#define MEMORY_LEN 1024U
static const uint32_t source[MEMORY_LEN] = {[0 ... MEMORY_LEN-1] = 0xFF00};
//uint32_t dest[MEMORY_LEN] = {0}; // works (.ram3 by default)
uint32_t dest[MEMORY_LEN] __attribute__ ((section(".ram0"))); // cached ram : do not work
//uint32_t dest[MEMORY_LEN] __attribute__ ((section(".ram3"))); // also works



_Static_assert(sizeof(source) == sizeof(dest),
	       "source and destination header size should be equal");

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
  size_t sizeInWord = MEMORY_LEN;
  SW(source);
  SW(dest);
  SW(&main);

  while (true) {
    // DMA size is in number of transaction, but memcpy size is in
    // number of bytes
    const size_t sizeInByte = sizeInWord * sizeof(uint32_t);

    // wait for EXTI IRQ  (when level goes from high to low)
    memset(dest, 42, sizeInByte);
    const benchResults brDma = doBench(benchDmacpyWrapper, BENCH_REPEAT, (void *) sizeInWord);
    
 
    // verify that source and dest buffer are equals. If error the MCU will stop :
    // LED heartbeat stops blinking. Attaching program to debugger permits to
    // dump the callstack and understand why it fails
    if (memcmp(source, dest, sizeInByte) != 0) {
      DebugTrace("memory copy verification has failed : DMA cache issue ?");
      SW(dest);
      break;
    } else {
      chDbgAssert(dest[0] == 0xFF00, "probable direction mismatch");
      DebugTrace("DMA [%u bytes] total=%lu, mean=%lu, min=%lu, max=%lu tput = %.2f Mo/s",
		 sizeInByte,
		 brDma.totalMicroSeconds,
		 brDma.meanMicroSeconds, 
		 brDma.minMicroSeconds,  
		 brDma.maxMicroSeconds,
		 (sizeInByte * BENCH_REPEAT) * 1e6d /  (brDma.totalMicroSeconds * 1024 * 1024.0d)); 
    }
    // bench DMA transfert from memory to memory
    memset(dest, 42, sizeInByte);
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

  chThdSleep(TIME_INFINITE);
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
  dmaTransfertTimeout(&dmap, (void *) source, dest, (size_t) userData, TIME_INFINITE);
#if STM32_DMA_USE_MUTUAL_EXCLUSION
  dmaReleaseBus(&dmap);
#endif
}




typedef struct {
  const char* name;
  uint32_t org;
  size_t  len;
} MemorySection;

const size_t k = 1024U;
const size_t M = 1024U * k;
const MemorySection memorySection[] = {
				     {"FLASH (AXIM)", 0x08000000, 2*M},
				     {"FLASH (ITCM)", 0x00200000, 2*M},  
				     {"SRAM1 (CACHED)", 0x20020000, 368*k},
				     {"SRAM2 (CACHED)", 0x2007C000, 16*k}, 
				     {"SRAM  (DTCM)", 0x20000000, 128*k},
				     {"SRAM  (ICTM)", 0x00000000, 16*k}, 
				     {"SRAM  (BACKUP)", 0x40024000, 4*k},
				     {NULL, 0, 0}
};

static void showWhere(const char* name, const void *adr)
{
  for (size_t i=0; memorySection[i].name != NULL; i++) {
    if (((uint32_t) adr >= memorySection[i].org) &&
      ((uint32_t) adr < (memorySection[i].org + memorySection[i].len))) {
      DebugTrace("%-10s = 0x%08lx in memory section %s",
		 name, (uint32_t) adr, memorySection[i].name);
    }
  }
}
