#include <ch.h>
#include <hal.h>
#include "stdutil.h"
#include "ttyConsole.h"
#include "adcHelper.h"
#include "hal_stm32_dma.h"

/*
  designed to run on a STM32F407G-DISC1 board
 */

// see STM32F4 datasheet page 139, table 73
#define ADC_GRP1_NUM_CHANNELS   1
#define DMA_TRANSACTION_SIZE    512

static void  dmaReceiveCb(DMADriver *dmapRead, void *buffer, const size_t n);
static void* dmaAskNextBufferCb(DMADriver *dmap, const size_t n);


// dma configuration
static const DMAConfig dmaConfigAdc1 = {
       .stream =  STM32_ADC_ADC1_DMA_STREAM,
       .channel = STM32_ADC1_DMA_CHANNEL,
       .dma_priority =  0, // low priority
       .irq_priority = 12, // low priority 
       .direction = DMA_DIR_P2M, // memory to peripheral
       .psize = 2, // adc sample are 16 bits wide
       .msize = 2, // so we use same size for words in memory
       .inc_peripheral_addr = false, // point to ADC register containing last sample
       .inc_memory_addr = true, // increment memory pointer
       .op_mode = DMA_CONTINUOUS_DOUBLE_BUFFER,
       .end_cb = &dmaReceiveCb,
       .next_cb = &dmaAskNextBufferCb,
};

// an heartbeat task which prove that the system is alive
static THD_WORKING_AREA(waBlinker, 384);
static void blinker (void *arg)
{

  (void)arg;
  chRegSetThreadName("blinker");
  
  while (true) {
    palToggleLine(LINE_LED2);
    chThdSleepMilliseconds(1000);
  }
}

ADCConversionGroup adcgrpcfg;

// theses objects will manage a stream of a dma controller
DMADriver dmapAdc1;

/*
  32 bytes transfert bloc : 2 DMA interrupts (one at receive stage, one at transmit stage)
  each 32 bytes instead of 2 interrupt by byte without dma mode
 */
#define DMA_FIFO_SIZE 16UL  // queue of the fifo (512 bytes)

/*
  declaration of object fifo queue
 */
typedef struct  {
  adcsample_t dmabuf[DMA_TRANSACTION_SIZE];
} fifo_dmaBuf_t;

fifo_dmaBuf_t IN_DMA_SECTION_NOINIT(dmaobj_pool[DMA_FIFO_SIZE]);
msg_t msg_fifo[DMA_FIFO_SIZE];
objects_fifo_t fifo;


static void findVoltageSpike(const fifo_dmaBuf_t *samples);
static void initAdc1(const ADCConversionGroup *grpp);

int main(void)
{

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  // initialise fifo object that is needed by dma
  chFifoObjectInit(&fifo, sizeof(fifo_dmaBuf_t),  DMA_FIFO_SIZE,  dmaobj_pool, msg_fifo);

  // start the UART associated to the interractive shell
  consoleInit();

  // start the heartbeat task
  chThdCreateStatic(waBlinker, sizeof(waBlinker), NORMALPRIO, &blinker, NULL);

  // helper function that fill ADCConversionGroup : one shot mode
  adcgrpcfg = adcGetConfig(ADC_GRP1_NUM_CHANNELS,
			   ADC_CHANNEL_IN11, ADC_CYCLES_480,
			   ADC_END);


  initAdc1(&adcgrpcfg);
  dmaObjectInit(&dmapAdc1);
  dmaStart(&dmapAdc1, &dmaConfigAdc1);
  dmaStartTransfert(&dmapAdc1, &ADC1->DR, NULL, DMA_TRANSACTION_SIZE);
  
 
  // start the interractive shell
  consoleLaunch();  


  fifo_dmaBuf_t *dmaBuf;
  while (true) {
    palToggleLine(LINE_LED1);
    chFifoReceiveObjectTimeout(&fifo, (void **) &dmaBuf,  TIME_INFINITE);
    // examine adc samples here
    findVoltageSpike(dmaBuf);
    chFifoReturnObject(&fifo, dmaBuf);
  }

}



static void dmaReceiveCb(DMADriver *dmap, void *buffer, const size_t n)
{
  (void) dmap;
  chDbgAssert(n == DMA_TRANSACTION_SIZE, "internal dma buffer error");

  chSysLockFromISR();
  chFifoSendObjectI(&fifo, buffer);
  chSysUnlockFromISR();
}

/*
  this callback can be called from 2 kind of locked states :
  1/ at start of the transfert, it is called 2 times from a regular locked state to 
     initially populate MEM0p and MEM1p
   then 
  2/ from ISR context each time a buffer is completed. 

  Since we cannot know the context, we do not use chSysLock or chSysLockFromISR
  but chSysGetStatusAndLockX which can be called from any locked context

  in the case we send at a lower baudrate then we receive, one will eventually run out of buffer and 
  chFifoTakeObjectI will return NULL. dma library handle this and some of the received data 
  will be lost, a counter of overwrite situation will be incremented. It can  
  be handled at the application level to log an error.  
 */
static void* dmaAskNextBufferCb(DMADriver *dmap, const size_t n)
{
  (void) dmap;
  chDbgAssert(n == DMA_TRANSACTION_SIZE, "internal dma buffer error");
  syssts_t sts = chSysGetStatusAndLockX();
  void *obj  = chFifoTakeObjectI(&fifo);
  chSysRestoreStatusX(sts);

  return obj;
}


static void findVoltageSpike(const fifo_dmaBuf_t *samples)
{
  uint32_t sum=0;
  static uint32_t count=0;
  for (size_t i=0; i< DMA_TRANSACTION_SIZE; i++) {
    sum += samples->dmabuf[i];
  }
  sum /= DMA_TRANSACTION_SIZE;
  if ((count++  % 100) == 0)
    DebugTrace("M(s) = %lu", sum);
}




static void initAdc1(const ADCConversionGroup *grpp)
{
  uint32_t cr2;
  rccEnableADC1(true);
  ADC->CCR = (ADC->CCR & (ADC_CCR_TSVREFE | ADC_CCR_VBATE)) |
    (STM32_ADC_ADCPRE << 16);
  
  /* ADC setup.*/
  ADC1->CR1 = 0;
  ADC1->CR2 = 0;
  ADC1->CR2 = ADC_CR2_ADON;
  ADC1->SR    = 0;
  ADC1->SMPR1 = grpp->smpr1;
  ADC1->SMPR2 = grpp->smpr2;
  ADC1->HTR   = grpp->htr;
  ADC1->LTR   = grpp->ltr;
  ADC1->SQR1  = grpp->sqr1 | ADC_SQR1_NUM_CH(grpp->num_channels);
  ADC1->SQR2  = grpp->sqr2;
  ADC1->SQR3  = grpp->sqr3;
  /* ADC configuration and start.*/
  ADC1->CR1   = grpp->cr1 | ADC_CR1_OVRIE | ADC_CR1_SCAN;

  /* Enforcing the mandatory bits in CR2.*/
  cr2 = grpp->cr2 | ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_ADON;

  /* The start method is different dependign if HW or SW triggered, the
     start is performed using the method specified in the CR2 configuration.*/
  if ((cr2 & ADC_CR2_SWSTART) != 0) {
    /* Initializing CR2 while keeping ADC_CR2_SWSTART at zero.*/
    ADC1->CR2 = (cr2 | ADC_CR2_CONT) & ~ADC_CR2_SWSTART;

    /* Finally enabling ADC_CR2_SWSTART.*/
    ADC1->CR2 = (cr2 | ADC_CR2_CONT);
  }
  else
    ADC1->CR2 = cr2;
}


