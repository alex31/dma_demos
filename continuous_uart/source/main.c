#include <ch.h>
#include <hal.h>
#include "stdutil.h"
#include "ttyConsole.h"
#include "hal_stm32_dma.h"

/*
  designed to run on a STM32F407G-DISC1 board
 */

#define STM32_UART_USART2_RX_DMA_STREAM  STM32_DMA_STREAM_ID(1, 5)
#define STM32_UART_USART2_RX_DMA_CHANNEL 4U

void dmaReceiveCb(DMADriver *dmap, void *buffer, const size_t n);

static const SerialConfig uartConfig =  {
				       .speed = 115200,
				       .cr1 = 0,
				       .cr2 = USART_CR2_STOP1_BITS,
				       .cr3 = USART_CR3_DMAR
};
  

// dma configuration
static const DMAConfig dmaConfig = {
       .stream = STM32_UART_USART2_RX_DMA_STREAM,
       .channel = STM32_UART_USART2_RX_DMA_CHANNEL,
       .dma_priority =  3, // low priority
       .irq_priority = 12, // low priority 
       .direction = DMA_DIR_P2M, // memory to peripheral
       .psize = 1, // GPIO ODR register is 16 bits wide
       .msize = 1, // so we use same size for words in memory
       .inc_peripheral_addr = false, // always update ODR and no other registers
       .inc_memory_addr = true, // increment memory pointer
       .circular = true, // continuous transaction
       .end_cb = &dmaReceiveCb
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
#define DMA_BUFFER_LEN	4U
static  uint8_t usartBuf[DMA_BUFFER_LEN] __attribute__((aligned(4)));


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

  sdStart(&SD2, &uartConfig);

  SD2.usart->CR1 &= ~(USART_CR1_PEIE | USART_CR1_RXNEIE | USART_CR1_TXEIE |
		      USART_CR1_TCIE);
  
  // initialize dma object
  dmaObjectInit(&dmap);

  // start dma peripheral
  dmaStart(&dmap, &dmaConfig);

  // launch continous DMA transfert from memory to GPIO peripheral
  dmaStartTransfert(&dmap, &SD2.usart->DR, usartBuf, DMA_BUFFER_LEN);

  // start the interractive shell
  consoleLaunch();  

  // wait until user pushes blue button
  while (palReadLine(LINE_BLUE_BUTTON) == PAL_HIGH) {
    chThdSleepMilliseconds(10);
  }

  chThdSleep(TIME_INFINITE);
}

void dmaReceiveCb(DMADriver *dmap, void *buffer, const size_t n)
{
  (void) dmap;
  (void) buffer;
  (void) n;
  palToggleLine(LINE_LED1);
  SD2.usart->DR &= ~USART_CR3_DMAR;
}
