/*
#define LUN_LOCAL_DRIVE			0

#define TTY_CONSOLE_PRINTF		1
#define TTY_CLIENT_SOCKET		2
#define TTY_CONSOLE_OVER_DEBUG	3
#define TTY_CONSOLE_UART        4
#define TTY_SERVER_SOCKET		5

#define TTY_PRINTK				TTY_CONSOLE_OVER_DEBUG

#ifdef __MACH__
#define TTY_UART                TTY_CONSOLE_UART
#else
#define TTY_UART                TTY_CONSOLE_PRINTF
#endif
*/

#define TIMA_OS

#define DEV_PRINTK				"/dev/tty5"
#define DEV_SOCKET_CLIENT		"/dev/tty7"
#define DEV_SOCKET_SERVER		"/dev/tty6"
#define DEV_CONSOLE				"/dev/tty8"

#define DEV_AUDIO				"/dev/audio0"

#define DEV_REMOTE				"/dev/tty10"
#define DEV_DATALINK			"/dev/tty7"
#define DEV_4DLCD				"/dev/tty9"
#define DEV_KEYPAD				"/dev/keypad0"
#define DEV_VM_SERIAL			"/dev/tty7"
#define DEV_OBD_TTY				"/dev/tty18"

#define DEV_TTY_COMMAND			DEV_PRINTK
//#define DEV_TTY_COMMAND			DEV_SOCKET_CLIENT
//#define DEV_TTY_COMMAND			DEV_SOCKET_SERVER

#define DONT_USE_MULTITHREAD

#define DONT_WANT_FTP_SERVER
#define DONT_WANT_SOCKET_OVER_TTY
#define DONT_WANT_FTM_OVER_WEB_SERVER
#define DONT_WANT_INPUT_CONSOLE
#define DONT_WANT_WEB_SERVER
#define DONT_WANT_DATALINK
#define DONT_WANT_FTM_LIBRARY

#define DONT_WANT_OBJECT_HANDLER
#define DONT_WAIT_1BPP_FONTS
#define DONT_WANT_RUFFUS_VM

#define DONT_WANT_AUDIO_INTERFACE
#define DONT_WANT_SPEEX
#define DONT_WANT_GPIO

#define WANT_GRAPHICS

