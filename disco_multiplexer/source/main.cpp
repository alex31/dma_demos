#include <ch.h>
#include <hal.h>
#include <cstdio>
#include <array>
#include "stdutil.h"		// necessaire pour initHeap
#include "ttyConsole.hpp"       // fichier d'entête du shell
#include "hal_stm32_dma.h"


/*
  Câbler une LED sur la broche C0


  ° connecter B6 (uart1_tx) sur PROBE+SERIAL Rx AVEC UN JUMPER
  ° connecter B7 (uart1_rx) sur PROBE+SERIAL Tx AVEC UN JUMPER
  ° connecter C0 sur led0 
  ° connecter PE9 sur 1/ entrée A2 et analyseur logique voie 1
  ° connecter PB2 sur analyseur logique voie 2
  ° connecter PC2 sur le potentiometre à bouton

 */

#define STM32_M2M_DMA_STREAM                STM32_DMA_STREAM_ID(2, 6)
#define STM32_M2M_DMA_CHANNEL               0
#define DMA_TRANSACTION_LEN		    65534U

static void end_cb(DMADriver *dmap, void *buffer, const size_t n);
volatile static rtcnt_t dmaTransactionDuration = 0U;

uint8_t * const muxInput1 = (uint8_t *) &GPIOB->IDR;
uint8_t * const muxInput2 = ((uint8_t *) &GPIOB->IDR) + 1;
uint8_t * const muxOutput = ((uint8_t *) &GPIOD->ODR) + 1;

const std::array muxInputs = {muxInput1, muxInput2};

static const DMAConfig dmaConfig = {
       .stream = STM32_M2M_DMA_STREAM,
       .channel = STM32_M2M_DMA_CHANNEL,
       .inc_peripheral_addr = false, // always update ODR and no other registers
       .inc_memory_addr = false, // always read same IDR
       .op_mode = DMA_CONTINUOUS_HALF_BUFFER,
       .end_cb = &end_cb,
       .error_cb = nullptr,
       .direction = DMA_DIR_M2M, // memory to peripheral
       .dma_priority =  3, // low prio (from low:0 to high:3)
       .irq_priority = 3, // low prio (from low:15 to high:2 [0,1 reserved])
       .psize = 1, // byte mode mirror 8 bits of the port
       .msize = 1, // obviously same as psize
       .pburst = 0,
       .mburst = 0,
       .fifo = 0,
       .periph_inc_size_4 = false,
       .transfert_end_ctrl_by_periph = false
};


static size_t currentIndex;

static THD_WORKING_AREA(waBlinker, 304);
static void blinker (void *arg)		
{
  (void)arg;				
  chRegSetThreadName("blinker");	
  
  while (true) {			
    palToggleLine(LINE_BLK_1);	
    chThdSleepMilliseconds(500);
    const uint32_t dtdUs = RTC2US(STM32_SYSCLK, dmaTransactionDuration);
    const float copyBySecond =  (1e6/dtdUs) * DMA_TRANSACTION_LEN;
    DebugTrace("%.2f * 10^6 copy/s", copyBySecond / 1e6);
  }
}

static THD_WORKING_AREA(waBlinker2, 304);
static void blinker2 (void *arg)		
{
  (void)arg;				
  chRegSetThreadName("blinker2");	
  
  while (true) {			
    palToggleLine(LINE_BLK_2);	
    chThdSleepMilliseconds(100);
  }
}
   
void _init_chibios() __attribute__ ((constructor(101)));
void _init_chibios() {
  halInit();
  chSysInit();
  initHeap ();
}


int main (void)
{
  DMADriver dmap;
  dmaObjectInit(&dmap);
  dmaStart(&dmap, &dmaConfig);

  consoleInit();	// initialisation des objets liés au shell

  consoleLaunch();  // lancement du shell

  currentIndex = 0;
  dmaStartTransfert(&dmap, muxInputs[currentIndex], (void*) muxOutput, 65534);
  chThdCreateStatic(waBlinker, sizeof(waBlinker), NORMALPRIO+2, &blinker, NULL);
  chThdCreateStatic(waBlinker2, sizeof(waBlinker2), NORMALPRIO+2, &blinker2, NULL);

  // main thread does nothing
  chThdSleep(TIME_INFINITE);
}


static void end_cb(DMADriver *dmadp, void *buffer, const size_t n)
{
  (void) buffer;
  (void) n;
  static rtcnt_t ts;
  const rtcnt_t now = chSysGetRealtimeCounterX();
  dmaTransactionDuration = rtcntDiff(now, ts);
  ts = now;

  chSysLockFromISR();
  const size_t index = palReadLine(LINE_BLUE_BUTTON) == PAL_LOW ? 0 : 1;
   
  if (index != currentIndex) {
    currentIndex = index;
    dmaStopTransfertI(dmadp);
    dmaStartTransfertI(dmadp, muxInputs[currentIndex], (void*) muxOutput, 65534);
  }
  chSysUnlockFromISR();
}
