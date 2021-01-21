#include <ch.h>
#include <hal.h>
#include "stdutil.h"
#include "ttyConsole.h"
#include "hal_stm32_dma.h"
#include <string.h>

/*
  designed to run on a STM32F407G-DISC1 board
 */

/*
  reception of data on USART1_RX, transmission on UART2T_X
 */
#define STM32_UART_USART1_RX_DMA_STREAM       STM32_DMA_STREAM_ID(2, 5)
#define STM32_UART_USART1_RX_DMA_CHANNEL      4
#define STM32_UART_USART3_TX_DMA_STREAM	      STM32_DMA_STREAM_ID(1, 3)
#define STM32_UART_USART3_TX_DMA_CHANNEL      4U

static void  dmaReceiveCb(DMADriver *dmapRead, void *buffer, const size_t n);
static void* dmaAskNextBufferCb(DMADriver *dmap, const size_t n);

static const SerialConfig uartReadConfig =  {
				       .speed = 115200,
				       .cr1 = 0,
				       .cr2 = USART_CR2_STOP1_BITS,
				       // generate signal when USART
				       // has received a byte
				       .cr3 = USART_CR3_DMAR
};
static const SerialConfig uartWriteConfig =  {
				       .speed = 9600,
				       .cr1 = 0,
				       .cr2 = USART_CR2_STOP1_BITS,
				       // generate signal when USART
				       // is clear to send
				       .cr3 = USART_CR3_DMAT
};
  

// dma configuration
static const DMAConfig dmaConfigRead = {
       .stream = STM32_UART_USART1_RX_DMA_STREAM,
       .channel = STM32_UART_USART1_RX_DMA_CHANNEL,
       .dma_priority =  0, // low priority
       .irq_priority = 12, // low priority 
       .direction = DMA_DIR_P2M, // memory to peripheral
       .psize = 1, // usart manage bytes
       .msize = 1, // so we use same size for words in memory
       .inc_peripheral_addr = false, // always update ODR and no other registers
       .inc_memory_addr = true, // increment memory pointer
       .op_mode = DMA_CONTINUOUS_DOUBLE_BUFFER,
       .end_cb = &dmaReceiveCb,
       .next_cb = &dmaAskNextBufferCb,
};
static const DMAConfig dmaConfigWrite = {
       .stream = STM32_UART_USART3_TX_DMA_STREAM,
       .channel = STM32_UART_USART3_TX_DMA_CHANNEL,
       .dma_priority =  0, // low priority
       .irq_priority = 12, // low priority 
       .direction = DMA_DIR_M2P, // memory to peripheral
       .psize = 1, // usart manage bytes
       .msize = 1, // so we use same size for words in memory
       .inc_peripheral_addr = false, // always update ODR and no other registers
       .inc_memory_addr = true, // increment memory pointer
       .op_mode = DMA_ONESHOT,
       .end_cb = NULL
};

// an heartbeat task which prove that the system is alive
static THD_WORKING_AREA(waBlinker, 384);
static void blinker (void *arg)
{

  (void)arg;
  chRegSetThreadName("blinker");
  
  while (true) {
    // briefly flash 2 LEDs as an heartbeat signal
    GPIOD->ODR ^= 0b1100000000000000;
    chThdSleepMilliseconds(20);
    GPIOD->ODR ^= 0b1100000000000000;
    chThdSleepMilliseconds(980);
  }
}




// theses objects will manage a stream of a dma controller
DMADriver dmapRead, dmapWrite;

/*
  32 bytes transfert bloc : 2 DMA interrupts (one at receive stage, one at transmit stage)
  each 32 bytes instead of 2 interrupt by byte without dma mode
 */
#define DMA_TRANSACTION_SIZE	32UL
#define DMA_FIFO_SIZE 16UL  // queue of the fifo (512 bytes)


/*
  declaration of object fifo queue
 */
typedef struct  {
  uint8_t dmabuf[DMA_TRANSACTION_SIZE];
} fifo_dmaBuf_t;

fifo_dmaBuf_t IN_DMA_SECTION_NOINIT(dmaobj_pool[DMA_FIFO_SIZE]);
msg_t msg_fifo[DMA_FIFO_SIZE];
objects_fifo_t fifo;


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

  // initialise fifo object that is needed by dma
  chFifoObjectInit(&fifo, sizeof(fifo_dmaBuf_t),  DMA_FIFO_SIZE,  dmaobj_pool, msg_fifo);

  // start the UART associated to the interractive shell
  consoleInit();

  // start the heartbeat task
  chThdCreateStatic(waBlinker, sizeof(waBlinker), NORMALPRIO, &blinker, NULL);

  // start receive and sending USART
  sdStart(&SD1, &uartReadConfig);
  sdStart(&SD3, &uartWriteConfig);

  /* disable interrupt which was set by sdStart for byte by byte send/receive
     and which are no more necessary in DMA mode
   */
  SD1.usart->CR1 &= ~(USART_CR1_PEIE | USART_CR1_RXNEIE | USART_CR1_TXEIE |
		      USART_CR1_TCIE);
  SD3.usart->CR1 &= ~(USART_CR1_PEIE | USART_CR1_RXNEIE | USART_CR1_TXEIE |
		      USART_CR1_TCIE);
  
  // initialize dma object
  dmaObjectInit(&dmapRead);
  dmaObjectInit(&dmapWrite);

  // start dma peripherals
  dmaStart(&dmapRead, &dmaConfigRead);
  dmaStart(&dmapWrite, &dmaConfigWrite);

  /* launch continous DMA transfert from USART to memory pool fifo
     In double buffer mode, the memory location is no given to dmaStartTransfert
     but dynamically given to DMA at his demand via the next_cb callback 
     We enforce that by giving NULL for the memory address
  */
  dmaStartTransfert(&dmapRead, &SD1.usart->DR, NULL, DMA_TRANSACTION_SIZE);

  // start the interractive shell
  consoleLaunch();

  // read the fifo object queue and send stream at different baud rate in thread
  // context
  fifo_dmaBuf_t *dmaBuf;
  while (true) {
    palToggleLine(LINE_LED1);
    chFifoReceiveObjectTimeout(&fifo, (void **) &dmaBuf,  TIME_INFINITE);
    dmaTransfert(&dmapWrite, &SD3.usart->DR, dmaBuf->dmabuf, sizeof(dmaBuf->dmabuf));
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
