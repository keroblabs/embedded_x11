#ifndef _hal_driver_h_
#define _hal_driver_h_

//////////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "hal_input.h"

//////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////

// The wire protocol is owned by hal_input.h; these are the legacy spellings.
#define CMD_MODIFIER            HAL_CMD_MODIFIER
#define CMD_KEY_DOWN            HAL_CMD_KEY_DOWN
#define CMD_KEY_UP              HAL_CMD_KEY_UP
#define CMD_MOUSE_X             HAL_CMD_MOUSE_X
#define CMD_MOUSE_Y             HAL_CMD_MOUSE_Y
#define CMD_MOUSE_Z             HAL_CMD_MOUSE_Z
#define CMD_MOUSE_BUTTON        HAL_CMD_MOUSE_BUTTON
#define CMD_MOUSE_WHEEL_X       HAL_CMD_MOUSE_WHEEL_X
#define CMD_MOUSE_WHEEL_Y       HAL_CMD_MOUSE_WHEEL_Y

#define KEY_DATA_BACKSPACE      HAL_KEY_BACKSPACE
#define KEY_DATA_ENTER          HAL_KEY_ENTER
#define KEY_DATA_ESCAPE         HAL_KEY_ESCAPE
#define KEY_DATA_LEFT           HAL_KEY_LEFT
#define KEY_DATA_DOWN           HAL_KEY_DOWN
#define KEY_DATA_UP             HAL_KEY_UP
#define KEY_DATA_RIGHT          HAL_KEY_RIGHT

#define MOUSE_BUTTON_LEFT       HAL_MOUSE_BUTTON_LEFT
#define MOUSE_BUTTON_MIDDLE     HAL_MOUSE_BUTTON_MIDDLE
#define MOUSE_BUTTON_RIGHT      HAL_MOUSE_BUTTON_RIGHT
#define MOUSE_BUTTON_X1         HAL_MOUSE_BUTTON_X1
#define MOUSE_BUTTON_X2         HAL_MOUSE_BUTTON_X2

// Mouse configuration commands - backend-internal, passed through
// hal_input_control() as an opaque byte buffer.
#define MOUSE_CONFIG_SETTING        0x50
#define MOUSE_CONFIG_CURSOR_SHOW    0x51
#define MOUSE_CONFIG_TOUCH_MODE     0x52

// Mouse setting parameters
#define MOUSE_SETTING_SCALE         0x01

//////////////////////////////////////////////////////////////////////////////////////

void keypad_driver_init( void );
bool_t keypad_driver_has_data( void );
uint8_t keypad_driver_read_data( void );

void lcd_driver_deinit( void );

PIXEL_TYPE_T * lcd_driver_get_framebuffer( void );
uint32_t lcd_driver_get_bits_per_pixel( void );

void mouse_driver_init( uint8_t index );
bool_t mouse_driver_has_data( uint8_t index );
uint8_t mouse_driver_read_data( uint8_t index );
void mouse_driver_process( void );
void mouse_driver_write_command( uint8_t index, const uint8_t *buffer, uint32_t max_size );

void lcd_driver_init( void );
uint16_t lcd_driver_get_width( void );
uint16_t lcd_driver_get_height( void );
void lcd_driver_clear_screen( void );
void lcd_driver_update_window( void );
void lcd_driver_do_scroll( uint16_t height );
void lcd_driver_set_pixel( uint16_t in_posx, uint16_t in_posy, PIXEL_TYPE_T colour );
void lcd_driver_show_bitmap_rgb( uint16_t posx, uint16_t posy, uint16_t width, uint16_t height, PIXEL_TYPE_T * bitmap, int use_transparent );
void lcd_driver_fill_colour( uint16_t in_posx, uint16_t in_posy, uint16_t in_width, uint16_t in_height, PIXEL_TYPE_T colour );

//////////////////////////////////////////////////////////////////////////////////////

#endif // _hal_driver_h_
