#include <ch.h>
#include <hal.h>
#include "stdutil.h"
#include "ttyConsole.h"
#include "hal_stm32_dma.h"
#include <string.h>

/*
  designed to run on a STM32F407G-DISC1 board
 */

#define STM32_UART_USART1_RX_DMA_STREAM       STM32_DMA_STREAM_ID(2, 5)
#define STM32_UART_USART1_RX_DMA_CHANNEL      4
#define STM32_UART_USART3_TX_DMA_STREAM	      STM32_DMA_STREAM_ID(1, 3)
#define STM32_UART_USART3_TX_DMA_CHANNEL      4U

static void dmaReceiveCb(DMADriver *dmapRead, void *buffer, const size_t n);

static const SerialConfig uartReadConfig =  {
				       .speed = 115200,
				       .cr1 = 0,
				       .cr2 = USART_CR2_STOP1_BITS,
				       // generate signal when USART
				       // has received a byte
				       .cr3 = USART_CR3_DMAR
};
static const SerialConfig uartWriteConfig =  {
				       .speed = 230400,
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
       .op_mode = DMA_CONTINUOUS_HALF_BUFFER,
       .end_cb = &dmaReceiveCb
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

// DMA works with ram and flash memory
// in this case, since the pattern is not going to change
// we leave it in flash.
// remove the const keyword and the memory buffer will be in ram
// dma buffer should be at least 32 bits aligned, and even more
// when burst is used
#define DMA_BUFFER_SIZE	64U
#define HALF_DMA_BUFFER_SIZE (DMA_BUFFER_SIZE / 2U)
#define QUEUE_LEN	32U
static  uint8_t usartBuf[DMA_BUFFER_SIZE] __attribute__((aligned(4)));

// theses objects will manage a stream of a dma controller
DMADriver dmapRead, dmapWrite;

static uint8_t readWriteBuffer[HALF_DMA_BUFFER_SIZE * QUEUE_LEN];
static input_queue_t readWriteQueue;

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

  
  iqObjectInit(&readWriteQueue, readWriteBuffer,
	       sizeof usartBuf ,
	       NULL, NULL);
  
  // start the UART associated to the interractive shell
  consoleInit();

  // start the heartbeat task
  chThdCreateStatic(waBlinker, sizeof(waBlinker), NORMALPRIO, &blinker, NULL);

  sdStart(&SD1, &uartReadConfig);
  sdStart(&SD3, &uartWriteConfig);

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

  // launch continous DMA transfert from memory to GPIO peripheral
  dmaStartTransfert(&dmapRead, &SD1.usart->DR, usartBuf, DMA_BUFFER_SIZE);

  // start the interractive shell
  consoleLaunch();

  // read the input queue and send stream at different baud rate in thread
  // context
  uint8_t rb[HALF_DMA_BUFFER_SIZE];
  while (true) {
    palToggleLine(LINE_LED1);
    const msg_t n = iqReadTimeout(&readWriteQueue, rb, sizeof(rb),
				  TIME_INFINITE);
    chDbgAssert(n != 0, "ibqReadTimeout");
    dmaTransfert(&dmapWrite, &SD3.usart->DR, rb, sizeof(rb));
  }
}

static void dmaReceiveCb(DMADriver *dmap, void *buffer, const size_t n)
{
  (void) dmap;

  SD1.usart->DR &= ~USART_CR3_DMAR;
  chSysLockFromISR();

  for (size_t i=0; i<n; i++)
    iqPutI(&readWriteQueue, ((uint8_t *)buffer)[i]);
  
  chSysUnlockFromISR();
}

