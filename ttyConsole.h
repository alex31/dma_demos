#ifndef __TTY_CONSOLE_H__
#define __TTY_CONSOLE_H__


// USB : 1, SERIAL : 0
#define CONSOLE_DEV_USB 0

#if CONSOLE_DEV_USB == 0
#define CONSOLE_DEV_SD SD2
#endif


void consoleInit (void);
void consoleLaunch (void);


#endif // __TTY_CONSOLE_H__
