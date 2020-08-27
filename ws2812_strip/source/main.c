#include <ch.h>
#include <hal.h>
#include "stdutil.h"
#include "ttyConsole.h"
#include "hal_stm32_dma.h"
#include "math.h"
#include <string.h>

/*
  designed to run on a STM32F407G-DISC1 board
 */

/* Timers with 4 channels that are DMA capable:
   T1, T2, T3, T4:5, T5, T8
   24 possible channels with 512 leds each : 12288 leds !
 */


#define NB_CHANNELS 4U // between 1 and 4

// cf tables 42 and 43, page 309 of stm32f4 reference manuel : RM0090
#define STM32_TIM1_UP_DMA_STREAM                STM32_DMA_STREAM_ID(2, 5)
#define STM32_TIM1_UP_DMA_CHANNEL               6

#define STRIP_NB_LEDS 8U // depend of your strip, mine has 8 leds, up to 512

typedef struct  {
  uint16_t g[8][NB_CHANNELS]; // color is to be sent in this order : 
  uint16_t r[8][NB_CHANNELS]; // g,r,b
  uint16_t b[8][NB_CHANNELS];
} Ws2812LedFrame;

typedef struct {
  // always 0 to work around DMA latency silicon bug on first world
  uint16_t start[NB_CHANNELS];
  // each bit is represented  by duration coded with two bytes
  Ws2812LedFrame leds[STRIP_NB_LEDS];
  // always 0 to terminate a frame
  uint16_t end[NB_CHANNELS]; 
} Ws2812StripFrame;

typedef enum  {SEND_COLORS, SEND_INTERFRAME} SerializeState;

typedef struct {
  Ws2812StripFrame frame;
  SerializeState state;
  uint16_t	 stateCnt;
} Ws2812Strip;

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} Color;


static const Color red = {100, 0, 0};   
static const Color green = {0, 100, 0};   
static const Color blue= {0, 100, 100};   
static const Color yellow = {100, 100, 0};  
static const Color white = {100, 100, 100}; 
static const Color cyan = {0, 100, 100};  
static const Color purple = {100, 0, 100};  
static const Color dimmed = {10, 10, 10};    

// be sure that compiler do not insert padding
_Static_assert(sizeof(Ws2812StripFrame) ==
	       ((24 * STRIP_NB_LEDS * NB_CHANNELS) + NB_CHANNELS * 2) * 2);

// fonction that generate dma frame from rgb color for one led
static void setColors(Ws2812StripFrame *ledStrip,
		      const uint8_t ledIdx,
		      const uint8_t chanIdx,
		      const Color rgb);

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
       .circular = false // one shot transaction
};


// WS2811 TIMING
#define WS_T0H    33U
#define WS_T1H    67U
#define WS_PERIOD 105U
#define WS_INTERFRAME_GAP_MILLISECOND 1U

// pwm configuration
/* DBL: this 5-bit vector defines the number of DMA transfers 
   (the timer detects a burst transfer when a read or a write access 
   to the TIMx_DMAR register address is performed).
   We want to update CCR1 to CCRx depending on the number of 
   timer channel we use
*/
#define    DCR_DBL       ((NB_CHANNELS-1) << 8)

/* DBA: This 5-bits vector defines the base-address for DMA transfers 
   (when read/write access are done through the TIMx_DMAR address). 
   DBA is defined as an offset starting from the
   address of the TIMx_CR1 register.
   first register to set is CCR1
*/
#define DCR_DBA(pwmd)    (((uint32_t *) (&pwmd.tim->CCR) - \
			  ((uint32_t *) (&pwmd.tim->CR1))))

static const PWMConfig  pwmCfg = {
	    .frequency = 84U * 1000U * 1000U, 
	    .period    = WS_PERIOD, 
	    .callback  = NULL,             
	    .channels  = {
			  {.mode = PWM_OUTPUT_ACTIVE_HIGH, .callback = NULL},
			  {.mode =  NB_CHANNELS > 1U ? PWM_OUTPUT_ACTIVE_HIGH :
			   PWM_OUTPUT_DISABLED,
			   .callback = NULL},
			  {.mode =  NB_CHANNELS > 2U ? PWM_OUTPUT_ACTIVE_HIGH :
			   PWM_OUTPUT_DISABLED,
			   .callback = NULL},
			  {.mode =  NB_CHANNELS > 3U ? PWM_OUTPUT_ACTIVE_HIGH :
			   PWM_OUTPUT_DISABLED,
			   .callback = NULL}
			  },
	    .dier =  STM32_TIM_DIER_UDE // Update DMA request enable
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


int main(void) {

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  Ws2812Strip ledStrip = {
			  .frame = {{0}},
			  .state = SEND_COLORS,
			  .stateCnt = 0};
  
  const Color	colorArraySource[NB_CHANNELS][STRIP_NB_LEDS] =
    {{red, green, blue, yellow, white, cyan, purple, dimmed},
     {green, blue, yellow, green, blue, yellow, green, blue},
     {cyan, purple, cyan, purple, cyan, purple, cyan, purple},
     {white, dimmed, white, dimmed, white, dimmed, white, dimmed}
    };

  halInit();
  chSysInit();

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

  // start timer in pwm mode
  pwmStart(&PWMD1, &pwmCfg);
  
  // enable bloc register DMA transaction through DMAR register
  PWMD1.tim->DCR = DCR_DBL | DCR_DBA(PWMD1); 

  // activate enabled channels where led strips are plugged
  // until first frame is sent, only 0 bit are sent
  // to lightoff the strips
  for (size_t i=0; i< NB_CHANNELS; i++)
    pwmEnableChannel(&PWMD1, i, WS_T0H);

  // start the interractive shell
  consoleLaunch();  
  
  // loop through animation
  float angle=0.0f;
  while (true) {
    float sina = sinf(angle);
    sina *= sina; // always positive range from 0 to 1 
    // dimming the leds together following a sine curve
    for (size_t i=0; i<STRIP_NB_LEDS; i++) {
      for (size_t j=0; j<NB_CHANNELS; j++) {
	Color col = colorArraySource[j][i];
	col.r *= sina;
	col.g *= sina;
	col.b *= sina;
	setColors(&ledStrip.frame, i, j, col);
      }
    }
    
    // send the frame to the led strip
    dmaTransfert(&dmap, &PWMD1.tim->DMAR, ledStrip.frame.start,
		 sizeof(ledStrip.frame) / sizeof(uint16_t));
    chThdSleepMilliseconds(10); // 100 hz
    angle = fmod(angle += 0.05f, 3.141592f);
  }
 }


typedef uint16_t (*ColorBitsArray)[NB_CHANNELS];
static void setColor(const uint8_t col,
		     const uint8_t chanIdx,
		     ColorBitsArray const bitArray)
{
  // each bit is serialised with pulde width
  // MSB is serialised first
  //  DebugTrace("bitArray@%p = %u", bitArray, col);
  for (size_t i=0; i<8; i++)
    bitArray[i][chanIdx] = col & (1U<<(7U-i)) ?  WS_T1H : WS_T0H;
}

static void setColors(Ws2812StripFrame *ledStrip,
		      const uint8_t ledIdx,
		      const uint8_t chanIdx,
		      const Color rgb)
{
  Ws2812LedFrame * const led = &ledStrip->leds[ledIdx];
  setColor(rgb.g, chanIdx, led->g);
  setColor(rgb.r, chanIdx, led->r);
  setColor(rgb.b, chanIdx, led->b);
}

