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

// between 1 and 4 : number of strips plugged to different output
// channel of the timer
#define NB_CHANNELS 4U 

// cf tables 42 and 43, page 309 of stm32f4 reference manuel : RM0090
#define STM32_TIM1_UP_DMA_STREAM                STM32_DMA_STREAM_ID(2, 5)
#define STM32_TIM1_UP_DMA_CHANNEL               6

#define STRIP_NB_LEDS 8U // depend of your strip, mine has 8 leds, up to 512
#define DMABUF_NB_LEDS 24U // continuous dma buffer : 1Kb, irq@2800hz
#define DMABUF_HALF_NB_LEDS (DMABUF_NB_LEDS/2) // needed in half buffer ISR
#define INTERFRAME_DURATION 10U // 10 frames = 240 bits @ 1.25µs = 300µs

typedef struct  {
  uint16_t g[8][NB_CHANNELS]; // color is to be sent in this order : 
  uint16_t r[8][NB_CHANNELS]; // g,r,b
  uint16_t b[8][NB_CHANNELS];
} Ws2812LedFrame;


typedef enum  {SEND_COLORS, SEND_INTERFRAME} SerializeState;

typedef struct {
  Ws2812LedFrame frame[DMABUF_NB_LEDS];
  SerializeState state;
  // depending on state, store interFrame index, or led index in the
  // color array
  union {
    uint16_t interFrameSent;
    uint16_t ledIdx;
  };
} Ws2812DmaBuffer;

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} Color;

typedef uint16_t (*ColorBitsArray)[NB_CHANNELS];

// all the context needed in the half buffer ISR is here, user_data pointer
// in the DMAConfig structure only need a pointer on this to fill DMA buffer
// from RGB framebuffer
typedef struct  {
  Color colorArray[NB_CHANNELS][STRIP_NB_LEDS];
  Ws2812DmaBuffer	dmaBuff;
} Ws2812DmaFramebuffer;

/*
 just needed to measure how many CPU cycles are used in ISR :
 4 cycles / bit when compile with -Ofast.
 the througtput is 800_000 bits/second by channel.
 it means 0.35% of total CPU by timer channel.
 driving 12288 leds with 24 channels will use 8.75% cpu
 CPU used just for entering/returning from ISR : 50 cycles
 for 6 timers with 2.8khz ISR each : 0.5% cpu
 so we have a total of 9.25% cpu used by ISR when using all 24 channels
 memory used :
 for dma buffer  : 1152 bytes by channel
 for framebuffer : 3 bytes by led
 example 64512 bytes for 12288 leds which is less that half memory available.
*/
static volatile uint64_t totalNbCycles = 0U;
static volatile uint64_t nbIsr = 0U;

static const Color red = {100, 0, 0};   
static const Color green = {0, 100, 0};   
static const Color blue= {0, 100, 100};   
static const Color yellow = {100, 100, 0};  
static const Color white = {100, 100, 100}; 
static const Color cyan = {0, 100, 100};  
static const Color purple = {100, 0, 100};  
static const Color dimmed = {10, 10, 10};    

// fonction that generate dma frame from rgb color for one led
static void setColors(Ws2812LedFrame *ledFrame,
		      const uint8_t chanIdx,
		      const Color rgb);

// ISR that have to serialize RBG framebuffer to WS2812B protocol
static void dmaHalfCb(DMADriver *dmap, void *buffer, const size_t n);



// WS2811 TIMING
#define WS_T0H    33U  // 0.4 µs
#define WS_T1H    67U  // 0.8 µs
#define WS_PERIOD 105U // 1.25 µs

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
	    // Activate the channels only fs needed
	    // depending on NB_CHANNEL value
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
    DebugTrace("ISR take %ld µs",
	       (uint32_t) RTC2US(STM32_HCLK, totalNbCycles/nbIsr));
  }
}

static Color colorArraySource[NB_CHANNELS][8] =
  {{red, green, blue, yellow, white, cyan, purple, dimmed},
   {green, blue, yellow, green, blue, yellow, green, blue},
   {cyan, purple, cyan, purple, cyan, purple, cyan, purple},
   {white, dimmed, white, dimmed, white, dimmed, white, dimmed}
  };

int main(void) {

  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */

  // could also be a global variable.
  // if we want to use several timers to raise number of leds,
  // a little rework is needed to share the RGB framebuffer between timers
  Ws2812DmaFramebuffer frameBuffer;

  // dma configuration : one by timer
  const DMAConfig dmaConfig = {
       .stream = STM32_TIM1_UP_DMA_STREAM,
       .channel = STM32_TIM1_UP_DMA_CHANNEL,
       .dma_priority =  0, // low prio (from low:0 to high:3)
       .irq_priority = 12, // low prio (from low:15 to high:2 [0,1 reserved])
       .direction = DMA_DIR_M2P, // memory to peripheral
       .psize = 2, // GPIO ODR register is 16 bits wide
       .msize = 2, // so we use same size for words in memory
       .inc_peripheral_addr = false, // always update ODR and no other registers
       .inc_memory_addr = true, // increment memory pointer
       .op_mode = DMA_CONTINUOUS_HALF_BUFFER,
       .end_cb = &dmaHalfCb, // call each time half buffer has been sent
       .user_data = &frameBuffer // context, to share ISR among timers
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
  memset(&frameBuffer.dmaBuff, 0, sizeof(frameBuffer.dmaBuff));
  frameBuffer.dmaBuff.state = SEND_COLORS;
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

  // send the frame to the led strip in continuous mode
  dmaStartTransfert(&dmap, &PWMD1.tim->DMAR, frameBuffer.dmaBuff.frame,
		    sizeof(frameBuffer.dmaBuff.frame) / sizeof(uint16_t));
  
  
  // loop through animation in thread mode
  float angle=0.0f;
  while (true) {
    // a loop for the one who want to plug big ledstrip initialised from small
    // buffer of color repeated to fill the ledstrip
    char *ptr = (char *) frameBuffer.colorArray;
    char * const endPtr =  (char *) frameBuffer.colorArray + sizeof(frameBuffer.colorArray);
    while (ptr < endPtr) {
      // here one could use memory to memory DMA copy to save CPU cycles
      memcpy(ptr, colorArraySource, MIN((int) sizeof(colorArraySource), endPtr-ptr));
      ptr += sizeof(colorArraySource);
    }
    float sina = sinf(angle);
    sina *= sina; // always positive range from 0 to 1 
    // dimming the leds together following a sine curve
    for (size_t i=0; i<STRIP_NB_LEDS; i++) {
      for (size_t j=0; j<NB_CHANNELS; j++) {
	Color *col = &frameBuffer.colorArray[j][i];
	col->r *= sina;
	col->g *= sina;
	col->b *= sina;
      }
    }
    
    chThdSleepMilliseconds(10); // 100 hz
    angle = fmod(angle += 0.05f, 3.141592f);
  }
 }

// fill a rgb frame (24 bits) with silent to split frames
static void setInterFrameSilent(Ws2812LedFrame* aFrame)
{
  memset(aFrame, 0, sizeof(Ws2812LedFrame));
}

// fill the 8 pulses length for one color of one led
static void setColor(const uint8_t col,
		     const uint8_t chanIdx,
		     ColorBitsArray const bitArray)
{
  // each bit is serialised with pulse width
  // MSB is serialised first
  for (size_t i=0; i<8; i++)
    bitArray[i][chanIdx] = col & (1U<<(7U-i)) ?  WS_T1H : WS_T0H;
}

// fill the 24 pulses length for the 3 colors of one led
static void setColors(Ws2812LedFrame *ledFrame,
		      const uint8_t chanIdx,
		      const Color rgb)
{
  setColor(rgb.g, chanIdx, ledFrame->g);
  setColor(rgb.r, chanIdx, ledFrame->r);
  setColor(rgb.b, chanIdx, ledFrame->b);
}

// fill neither colors or interframe silent in the next half buffer
// to be sent
static void dmaHalfCb(DMADriver *dmap, void *buffer, const size_t n)
{
  (void) n; // we already statically know the length
  uint32_t cntStamp =  chSysGetRealtimeCounterX();
  Ws2812LedFrame * const halfFrame =  (Ws2812LedFrame *) buffer;
  // get context
  Ws2812DmaFramebuffer *fb = (Ws2812DmaFramebuffer *) dmap->config->user_data;
  
  for (size_t i=0U; i < DMABUF_HALF_NB_LEDS; i++) {
    if (fb->dmaBuff.state == SEND_INTERFRAME) {
      setInterFrameSilent(&halfFrame[i]);
      if (++fb->dmaBuff.interFrameSent >= INTERFRAME_DURATION) {
	fb->dmaBuff.state = SEND_COLORS;
	fb->dmaBuff.ledIdx = 0U;
      } 
    } else {
      for (size_t j=0; j<NB_CHANNELS; j++) {
	Color col = fb->colorArray[j][fb->dmaBuff.ledIdx];
      	setColors(&halfFrame[i], j, col);
      }
      if (++fb->dmaBuff.ledIdx >= STRIP_NB_LEDS) {
	fb->dmaBuff.state = SEND_INTERFRAME;
	fb->dmaBuff.interFrameSent = 0U;
      }
    }
  }
  totalNbCycles += (chSysGetRealtimeCounterX() - cntStamp);
  nbIsr++;
}
