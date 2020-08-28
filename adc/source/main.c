#include <ch.h>
#include <hal.h>
#include "stdutil.h"
#include "ttyConsole.h"
#include "adcHelper.h"

/*
  designed to run on a STM32F407G-DISC1 board
 */

// see STM32F4 datasheet page 139, table 73
#define VREF_NOMINAL	3.3f
#define VREF_INT_CALIBRATION    (*((uint16_t *) 0x1FFF7A2A))
#define ADC_GRP1_NUM_CHANNELS   2
#define ADC_GRP1_BUF_DEPTH      128
static adcsample_t samples[ADC_GRP1_NUM_CHANNELS * ADC_GRP1_BUF_DEPTH];

// do one shot adc conversion
static void oneShotAdc(void);

// callback for continuous conversion
static void adcContinuousCB(ADCDriver *adcp);

// give internal core temp from sensors voltage
static float scaleTemp (const int fromTmp, const float _vdda);

// give vdda from internal vref sample
static float vddaFromVref(const int vrefSample);
  
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

ADCConversionGroup adcgrpcfg;
volatile float vddaSetInISR = 0.0f, coreTempSetInISR = 0.0f;
volatile size_t isrCalledCount = 0U;

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

  // helper function that fill ADCConversionGroup : one shot mode
  adcgrpcfg = adcGetConfig(ADC_GRP1_NUM_CHANNELS,
			   ADC_ONE_SHOT,
			   ADC_CHANNEL_SENSOR,
			   ADC_CHANNEL_VREFINT,
			   ADC_END);
 
  // start ADC peripheral
  adcStart(&ADCD1, NULL);
  
  // enable temperature sensor
  adcSTM32EnableTSVREFE();

  // start the interractive shell
  consoleLaunch();  

  // wait until user pushes blue button
  while (palReadLine(LINE_BLUE_BUTTON) == PAL_LOW) {
    chThdSleepMilliseconds(10);
  }

  // then use continuous API
  // helper function that fill ADCConversionGroup : continuous conversion
  adcgrpcfg = adcGetConfig(ADC_GRP1_NUM_CHANNELS,
			   ADC_CONTINUOUS,
			   ADC_CHANNEL_SENSOR,
			   ADC_CHANNEL_VREFINT,
			   &adcContinuousCB,
			   ADC_END);
  
  adcStartConversion(&ADCD1, &adcgrpcfg, samples, ADC_GRP1_BUF_DEPTH);

  // display average value calculated in ISR
  while(TRUE) {
    DebugTrace("[CONTINUOUS][%u] vdda = %.2f, core temp = %.1f",
	       isrCalledCount, vddaSetInISR, coreTempSetInISR);
    chThdSleepMilliseconds(1000);
  }
  
}


static void oneShotAdc(void)
{
  uint32_t vddaAccumSamples = 0U;
  uint32_t coreTempAccumSamples = 0U;
  adcConvert(&ADCD1, &adcgrpcfg, samples, ADC_GRP1_BUF_DEPTH);
  
  // calculate average of the samples for each channel
  for (size_t i=0; i<ADC_GRP1_BUF_DEPTH; i++) {
    vddaAccumSamples += samples[1+(i*ADC_GRP1_NUM_CHANNELS)];
    coreTempAccumSamples += samples[0+(i*ADC_GRP1_NUM_CHANNELS)];
  }
  
  const float vdda = vddaFromVref(vddaAccumSamples / ADC_GRP1_BUF_DEPTH);
  const float coreTemp = scaleTemp(coreTempAccumSamples / ADC_GRP1_BUF_DEPTH,
				   vdda);
  DebugTrace("vdda = %.2f, core temp = %.1f", vdda, coreTemp);
  
  chThdSleepMilliseconds(1000);
}
 


/* 
   details can be found in the reference manuel RM0090 p413
   and in datasheet p138 table 5.3.22
*/
static float scaleTemp (const int sensorSample, const float _vdda)
{
  const float sampleNorm = (float) sensorSample / 4095.0f;
  const float sampleVolt = sampleNorm * _vdda;
  const float deltaVolt = sampleVolt - 0.76f;
  const float deltaCentigrade = deltaVolt / 2.5e-3f;
  const float temp = 25.0f + deltaCentigrade;

  return temp;
}

static float vddaFromVref(const int vrefSample)
{
  return VREF_NOMINAL * vrefSample / VREF_INT_CALIBRATION;
}

static void adcContinuousCB(ADCDriver *adcp)
{
  // depending on half buffer that has just been filled
  adcsample_t *buffer = samples + (adcIsBufferComplete(adcp) ?
				   ADC_GRP1_BUF_DEPTH / 2 : 0);
  uint32_t vddaAccumSamples = 0U;
  uint32_t coreTempAccumSamples = 0U;

  isrCalledCount++;
  
  for (size_t i=0; i<ADC_GRP1_BUF_DEPTH/2; i++) {
    vddaAccumSamples += buffer[1+(i*ADC_GRP1_NUM_CHANNELS)];
    coreTempAccumSamples += buffer[0+(i*ADC_GRP1_NUM_CHANNELS)];
  }
  
  vddaSetInISR = vddaFromVref(vddaAccumSamples / (ADC_GRP1_BUF_DEPTH/2));
  coreTempSetInISR = scaleTemp(coreTempAccumSamples /
			   (ADC_GRP1_BUF_DEPTH/2),
			   vddaSetInISR);
}
