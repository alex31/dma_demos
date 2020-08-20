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

// cf tables 42 and 43, page 309 of stm32f4 reference manuel : RM0090
#define STM32_TIM1_UP_DMA_STREAM                STM32_DMA_STREAM_ID(2, 5)
#define STM32_TIM1_UP_DMA_CHANNEL               6

#define STRIP_NB_LEDS 8U // depend of your strip, mine has 8 leds

typedef struct  {
  uint16_t g[8]; // color is to be sent in this order : 
  uint16_t r[8]; // g,r,b
  uint16_t b[8];
} Ws2812LedFrame;

typedef struct {
  uint16_t start[2]; // work around DMA latency on two first transfert
  Ws2812LedFrame leds[STRIP_NB_LEDS]; // each bit is represented by duration coded with two bytes
  uint16_t end[2]; // always 0 to terminate a frame
} Ws2812StripFrame;

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
_Static_assert(sizeof(Ws2812StripFrame) == ((24 * STRIP_NB_LEDS) + 4) * 2);

// fonction that generate dma frame from rgb color for one led
static void setColors(Ws2812StripFrame *ledStrip,
		      const uint8_t ledIdx,
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
static const PWMConfig  pwmCfg = {
	    .frequency = 84U * 1000U * 1000U, 
	    .period    = WS_PERIOD, 
	    .callback  = NULL,             
	    .channels  = {
			  {.mode = PWM_OUTPUT_ACTIVE_HIGH, .callback = NULL},
			  {.mode = PWM_OUTPUT_DISABLED, .callback = NULL},
			  {.mode = PWM_OUTPUT_DISABLED, .callback = NULL},
			  {.mode = PWM_OUTPUT_DISABLED, .callback = NULL},
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
  Ws2812StripFrame ledStrip = {0};
  const Color	colorArraySource[STRIP_NB_LEDS] =
    {red, green, blue, yellow, white, cyan, purple, dimmed};

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
  
  // activate channel 0 where led strip is plugged
  // until first frame is sent, only 0 bit are sent
  // to lightoff the strip
  pwmEnableChannel(&PWMD1, 0, WS_T0H);

  // start the interractive shell
  consoleLaunch();  
  
  // loop through animation
  float angle=0.0f;
  while (true) {
    Color colorArray[STRIP_NB_LEDS];
    memcpy(&colorArray, &colorArraySource, sizeof(colorArraySource));
    float sina = sinf(angle);
    sina *= sina; // always positive range from 0 to 1 
    // dimming the leds together following a sine curve
    for (size_t i=0; i<STRIP_NB_LEDS; i++) {
      colorArray[i].r *= sina;
      colorArray[i].g *= sina;
      colorArray[i].b *= sina;
      setColors(&ledStrip, i, colorArray[i]);
    }

    // send the frame to the led strip
    dmaTransfert(&dmap, &PWMD1.tim->CCR, &ledStrip,
		 sizeof(ledStrip) / sizeof(uint16_t));
    chThdSleepMilliseconds(10); // 100 hz
    angle = fmod(angle += 0.05f, 3.141592f);
  }
 }



static void setColor(const uint8_t col,
		     uint16_t * const bitArray)
{
  // each bit is serialised with pulde width
  // MSB is serialised first
  for (size_t i=0; i<8; i++)
    bitArray[i] = col & (1U<<(7U-i)) ?  WS_T1H : WS_T0H;
}

static void setColors(Ws2812StripFrame *ledStrip,
		     const uint8_t ledIdx,
		     const Color rgb)
{
  Ws2812LedFrame * const led = &ledStrip->leds[ledIdx];
  setColor(rgb.g, led->g);
  setColor(rgb.r, led->r);
  setColor(rgb.b, led->b);
}

