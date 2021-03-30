#define _GNU_SOURCE // enable strcasestr

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include "ch.h"
#include "hal.h"
#include "microrl/microrlShell.h"
#include "stdutil.h"
#include "printf.h"
#include "ttyConsole.hpp"


#ifdef CONSOLE_DEV_SD

/*===========================================================================*/
/* START OF EDITABLE SECTION                                           */
/*===========================================================================*/

// declaration des prototypes de fonction
// ces declarations sont necessaires pour remplir le tableau commands[] ci-dessous
using cmd_func_t =  void  (BaseSequentialStream *lchp, int argc,const char * const argv[]);
static cmd_func_t cmd_mem, cmd_uid, cmd_restart, cmd_param;
#if CH_DBG_THREADS_PROFILING
static cmd_func_t cmd_threads;
#endif



static const ShellCommand commands[] = {
  {"mem", cmd_mem},		// affiche la mémoire libre/occupée
#if  CH_DBG_THREADS_PROFILING
  {"threads", cmd_threads},	// affiche pour chaque thread le taux d'utilisation de la pile et du CPU
#endif
  {"uid", cmd_uid},		// affiche le numéro d'identification unique du MCU
  {"param", cmd_param},		// fonction à but pedagogique qui affiche les
				//   paramètres qui lui sont passés

  {"restart", cmd_restart},	// reboot MCU
  {NULL, NULL}			// marqueur de fin de tableau
};



/*
  definition de la fonction cmd_param asociée à la commande param (cf. commands[])
  cette fonction a but pédagogique affiche juste les paramètres fournis, et tente
  de convertir les paramètres en entier et en flottant, et affiche le resultat de
  cette conversion. 
  une fois le programme chargé dans la carte, essayer de rentrer 
  param toto 10 10.5 0x10
  dans le terminal d'eclipse pour voir le résultat 
 */
static void cmd_param(BaseSequentialStream *lchp, int argc,const char* const argv[])
{
  if (argc == 0) {  // si aucun paramètre n'a été passé à la commande param 
    chprintf(lchp, "pas de paramètre en entrée\r\n");
  } else { // sinon (un ou plusieurs pararamètres passés à la commande param 
    for (int argn=0; argn<argc; argn++) { // pour tous les paramètres
      chprintf(lchp, "le parametre %d/%d est %s\r\n", argn, argc-1, argv[argn]); // afficher

      // tentative pour voir si la chaine peut être convertie en nombre entier et en nombre flottant
      int entier = atoi (argv[argn]); // atoi converti si c'est possible une chaine en entier
      float flottant = atof (argv[argn]); // atof converti si c'est possible une chaine en flottant

      chprintf(lchp, "atoi(%s) = %d ;; atof(%s) = %.3f\r\n",
		argv[argn], entier, argv[argn], flottant);
    }
  }
}


/*
  conf :

  commandes sans arguments
  show
  store
  load
  wipe
  erase

  pour les commandes suivantes : cmd val : affecte la valeur, cmd : affiche la valeur
  magnet 
  motor
  window
  median
  rate
  baud
  smin
  smax
 */

static void cmd_restart(BaseSequentialStream *lchp, int argc,const char* const argv[])
{
  (void) lchp;
  (void) argc;
  (void) argv;
  systemReset();
}

/*
  
 */


/*===========================================================================*/
/* START OF PRIVATE SECTION  : DO NOT CHANGE ANYTHING BELOW THIS LINE        */
/*===========================================================================*/

/*===========================================================================*/
/* Command line related.                                                     */
/*===========================================================================*/


#define SHELL_WA_SIZE   THD_WORKING_AREA_SIZE(2000)




#ifndef CONSOLE_DEV_USB
#define  CONSOLE_DEV_USB 0
#endif

#if CONSOLE_DEV_USB == 0
static const SerialConfig ftdiConfig =  {
  115200,
  0,
  USART_CR2_STOP1_BITS | USART_CR2_LINEN,
  0
};
#endif


#define MAX_CPU_INFO_ENTRIES 20

typedef struct _ThreadCpuInfo {
  float    ticks[MAX_CPU_INFO_ENTRIES];
  float    cpu[MAX_CPU_INFO_ENTRIES];
  float    totalTicks;
  _ThreadCpuInfo () {
    for (auto i=0; i< MAX_CPU_INFO_ENTRIES; i++) {
      ticks[i] = 0.0f;
      cpu[i] = -1.0f;
    }
    totalTicks = 0.0f;
  }
} ThreadCpuInfo ;
  
#if CH_DBG_THREADS_PROFILING
static void stampThreadCpuInfo (ThreadCpuInfo *ti);
static float stampThreadGetCpuPercent (const ThreadCpuInfo *ti, const uint32_t idx);
#endif

static void cmd_uid(BaseSequentialStream *lchp, int argc,const char* const argv[]) {
  (void)argv;
  if (argc > 0) {
     chprintf(lchp, "Usage: uid\r\n");
    return;
  }

  for (uint32_t i=0; i< UniqProcessorIdLen; i++)
    chprintf(lchp, "[%x] ", UniqProcessorId[i]);
  chprintf(lchp, "\r\n");
}



static void cmd_mem(BaseSequentialStream *lchp, int argc,const char* const argv[]) {
  (void)argv;
  if (argc > 0) {
    chprintf(lchp, "Usage: mem\r\n");
    return;
  }

  chprintf(lchp, "core free memory : %u bytes\r\n", chCoreGetStatusX());

#if CH_HEAP_SIZE != 0
  chprintf(lchp, "heap free memory : %u bytes\r\n", getHeapFree());
  
  void * ptr1 = malloc_m (100);
  void * ptr2 = malloc_m (100);
  
  chprintf(lchp, "(2x) malloc_m(1000) = %p ;; %p\r\n", ptr1, ptr2);
  chprintf(lchp, "heap free memory : %d bytes\r\n", getHeapFree());
  
  free_m (ptr1);
  free_m (ptr2);
#endif
  
}



#if  CH_DBG_THREADS_PROFILING
static void cmd_threads(BaseSequentialStream *lchp, int argc,const char* const argv[]) {
  static const char *states[] = {CH_STATE_NAMES};
  thread_t *tp = chRegFirstThread();
  (void)argv;
  (void)argc;
  float totalTicks=0;
  float idleTicks=0;

  static ThreadCpuInfo threadCpuInfo ;
  
  stampThreadCpuInfo (&threadCpuInfo);
  
  chprintf(lchp, "    addr    stack  frestk prio refs  state        time \t percent        name\r\n");
  uint32_t idx=0;
  do {
    chprintf(lchp, "%.8lx %.8lx %6lu %4lu %4lu %9s %9lu   %.1f    \t%s\r\n",
	      (uint32_t)tp, (uint32_t)tp->ctx.sp,
	      get_stack_free (tp),
	      (uint32_t)tp->hdr.pqueue.prio, (uint32_t)(tp->refs - 1),
	      states[tp->state], (uint32_t)tp->time, 
	      stampThreadGetCpuPercent (&threadCpuInfo, idx),
	      chRegGetThreadNameX(tp));
    totalTicks+= (float) tp->time;
    if (strcmp (chRegGetThreadNameX(tp), "idle") == 0)
      idleTicks =  (float) tp->time;
    tp = chRegNextThread ((thread_t *)tp);
    idx++;
  } while (tp != NULL);

  const float idlePercent = (idleTicks*100.f)/totalTicks;
  const float cpuPercent = 100.f - idlePercent;
  chprintf(lchp, "\r\ncpu load = %.2f%%\r\n", cpuPercent);
}
#endif

static const ShellConfig shell_cfg1 = {
#if CONSOLE_DEV_USB == 0
  (BaseSequentialStream *) &CONSOLE_DEV_SD,
#else
  (BaseSequentialStream *) &SDU1,
#endif
  commands
};



void consoleInit (void)
{
  /*
   * Activates the USB driver and then the USB bus pull-up on D+.
   * USBD1 : FS, USBD2 : HS
   */

#if CONSOLE_DEV_USB != 0
  usbSerialInit(&SDU1, &USBDRIVER); 
  chp = (BaseSequentialStream *) &SDU1;
#else
  sdStart(&CONSOLE_DEV_SD, &ftdiConfig);
  chp = (BaseSequentialStream *) &CONSOLE_DEV_SD;
#endif
  /*
   * Shell manager initialization.
   */
  shellInit();
}


void consoleLaunch (void)
{
  thread_t *shelltp = NULL;

 
#if CONSOLE_DEV_USB != 0
  if (!shelltp) {
    while (usbGetDriver()->state != USB_ACTIVE) {
      chThdSleepMilliseconds(10);
    }
    
    // activate driver, giovani workaround
    chnGetTimeout(&SDU1, TIME_IMMEDIATE);
    while (!isUsbConnected()) {
      chThdSleepMilliseconds(10);
    }
    shelltp = shellCreate(&shell_cfg1, SHELL_WA_SIZE, NORMALPRIO);
    palSetLine(LINE_USB_LED);
  } else if (shelltp && (chThdTerminated(shelltp))) {
    chThdRelease(shelltp);    /* Recovers memory of the previous shell.   */
    shelltp = NULL;           /* Triggers spawning of a new shell.        */
  }

#else // CONSOLE_DEV_USB == 0

   if (!shelltp) {
     shelltp = shellCreate(&shell_cfg1, SHELL_WA_SIZE, NORMALPRIO);
   } else if (chThdTerminatedX(shelltp)) {
     chThdRelease(shelltp);    /* Recovers memory of the previous shell.   */
     shelltp = NULL;           /* Triggers spawning of a new shell.        */
   }
   chThdSleepMilliseconds(100);
   
#endif //CONSOLE_DEV_USB

}

#if  CH_DBG_THREADS_PROFILING
static void stampThreadCpuInfo (ThreadCpuInfo *ti)
{
  const thread_t *tp =  chRegFirstThread();
  uint32_t idx=0;
  
  float totalTicks =0;
  do {
    totalTicks+= (float) tp->time;
    ti->cpu[idx] = (float) tp->time - ti->ticks[idx];;
    ti->ticks[idx] = (float) tp->time;
    tp = chRegNextThread ((thread_t *)tp);
    idx++;
  } while ((tp != NULL) && (idx < MAX_CPU_INFO_ENTRIES));
  
  const float diffTotal = totalTicks- ti->totalTicks;
  ti->totalTicks = totalTicks;
  
  tp =  chRegFirstThread();
  idx=0;
  do {
    ti->cpu[idx] =  (ti->cpu[idx]*100.f)/diffTotal;
    tp = chRegNextThread ((thread_t *)tp);
    idx++;
  } while ((tp != NULL) && (idx < MAX_CPU_INFO_ENTRIES));
}


static float stampThreadGetCpuPercent (const ThreadCpuInfo *ti, const uint32_t idx)
{
  if (idx >= MAX_CPU_INFO_ENTRIES) 
    return -1.f;

  return ti->cpu[idx];
}
#endif

#endif
