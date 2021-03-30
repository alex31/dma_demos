#pragma once



// variables globales exportées par le module


// fonctions exportées par le module

#ifdef CONSOLE_DEV_SD
void consoleInit (void);
void consoleLaunch (void);

#if defined TRACE 
#include "stdutil.h"
#include "usb_serial.h"
#define CDCTrace(fmt, ...) {if (isUsbConnected()) chprintf (chp, fmt "\r\n", ## __VA_ARGS__ );}
#else
#define CDCTrace(...) 
#endif // TRACE


#endif
