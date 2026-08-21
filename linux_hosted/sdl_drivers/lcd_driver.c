#include <SDL.h>
#include "sdl_lib_export.h"
#include "sdl_main.h"
#include "sdl_window.h"
#include "sdl_input.h"
#include "hal_drivers.h"

#include "types.h"

///////////////////////////////////////////////////////////////

#define SDL_SCREEN_WIDTH		_USE_SCREEN_WIDTH
#define SDL_SCREEN_HEIGHT		_USE_SCREEN_HEIGHT

#define LCD_SCREEN_WIDTH		(SDL_SCREEN_WIDTH / PIXEL_X_SCALE)
#define LCD_SCREEN_HEIGHT		(SDL_SCREEN_HEIGHT / PIXEL_Y_SCALE)

#define PIXEL_X_SCALE		1
#define PIXEL_Y_SCALE		1

///////////////////////////////////////////////////////////////

unsigned int mouse_x;
unsigned int mouse_y;
unsigned int mouse_button;
unsigned int mouse_trigg;

static uint32_t window_id;
static uint16_t *lcd_buffer = NULL;

///////////////////////////////////////////////////////////////

void keypad_driver_event_key( uint32_t window_id, uint8_t key_code, uint16_t control_key, uint8_t state );
void mouse_driver_event_mouse( uint32_t window_id, int32_t deltax, int32_t deltay, uint16_t posz, uint8_t button_state );
void mouse_driver_event_wheel( uint32_t window_id, int32_t wheel_x, int32_t wheel_y );

///////////////////////////////////////////////////////////////

void lcd_driver_init( void )
{
	sdl_init();
    window_id = sdl_create_window("Tima Laptop", SDL_SCREEN_WIDTH, SDL_SCREEN_HEIGHT);
    lcd_buffer = sdl_get_buffer( window_id );

	sdl_window_set_key_event( window_id, keypad_driver_event_key );
	sdl_window_set_mouse_delta_event( window_id, mouse_driver_event_mouse );
	sdl_window_set_mouse_wheel_event( window_id, mouse_driver_event_wheel );

	mouse_trigg = 0;
}

void lcd_driver_deinit( void )
{
	sdl_destroy_window( window_id );
}

void lcd_driver_clear_screen( void )
{
	lcd_driver_fill_colour(0, 0, LCD_SCREEN_WIDTH, LCD_SCREEN_HEIGHT, 0 );
}

void lcd_driver_update_window( void )
{
	sdl_event_loop();
}

uint16_t lcd_driver_get_width( void )
{
	return LCD_SCREEN_WIDTH;
}

uint16_t lcd_driver_get_height( void )
{
	return LCD_SCREEN_HEIGHT;
}

uint16_t * lcd_driver_get_framebuffer( void )
{
	return ( uint16_t * )lcd_buffer;
}

uint32_t lcd_driver_get_bits_per_pixel( void )
{
	return sizeof(PIXEL_TYPE_T) * 8;
}

void lcd_driver_do_scroll( uint16_t height )
{
	uint32_t scroll_height = height * PIXEL_Y_SCALE;
	uint32_t lcd_base_scroll;
	uint32_t scroll_size;

	scroll_size = ( SDL_SCREEN_HEIGHT - scroll_height ) * SDL_SCREEN_WIDTH;
	lcd_base_scroll = scroll_height * SDL_SCREEN_WIDTH;

	memcpy( lcd_buffer, &lcd_buffer[ lcd_base_scroll ], scroll_size * sizeof( uint16_t ) );

	lcd_base_scroll = scroll_size;
	scroll_size = scroll_height * SDL_SCREEN_WIDTH;
	memset( &lcd_buffer[ lcd_base_scroll ], 0x00, scroll_size * sizeof( uint16_t ) );
}

void lcd_driver_set_pixel( uint16_t in_posx, uint16_t in_posy, PIXEL_TYPE_T colour )
{
	uint16_t posx = in_posx * PIXEL_X_SCALE;
	uint16_t posy = in_posy * PIXEL_Y_SCALE;
	uint16_t repeat_x_cnt;
	uint16_t repeat_y_cnt;

	uint32_t lcd_offset = ( posy * SDL_SCREEN_WIDTH ) + posx;

	for( repeat_y_cnt = 0; repeat_y_cnt < PIXEL_Y_SCALE; repeat_y_cnt++ )
	{
		if( ( posy + repeat_y_cnt) >= SDL_SCREEN_HEIGHT )
		{
			break;
		}

		for( repeat_x_cnt = 0; repeat_x_cnt < PIXEL_X_SCALE; repeat_x_cnt++ )
		{
			if( ( posx + repeat_x_cnt ) >= SDL_SCREEN_WIDTH )
			{
				break;
			}

			lcd_buffer[ lcd_offset + repeat_x_cnt ] = colour;
		}

		lcd_offset += SDL_SCREEN_WIDTH;
	}
}

void lcd_driver_show_bitmap_rgb( uint16_t posx, uint16_t posy, uint16_t width, uint16_t height, PIXEL_TYPE_T * bitmap, int use_transparent )
{
    uint16_t x, y;
    uint16_t colour16;
	uint16_t transparent;
	uint32_t bitmap_offset = 0; 

    if( lcd_buffer == NULL ) return;
	if( use_transparent != -1 )
	{
		transparent = ( uint16_t )use_transparent;
	}

	if( posx >= LCD_SCREEN_WIDTH )
	{
		return;
	}

    for( y = 0; y < height; y++ )
    {
		if( ( posy + y ) >= LCD_SCREEN_HEIGHT )
		{
			break;
		}

		for( x = 0; x < width; x++ )
		{
			if( ( posx + x ) >= LCD_SCREEN_WIDTH )
			{
				break;
			}

			colour16 = bitmap[bitmap_offset + x];

			if( ( colour16 != transparent ) || ( use_transparent == -1 ) )
			{
				lcd_driver_set_pixel( posx + x, posy + y, colour16 );
			}
		}

		bitmap_offset += width;
    }    
}

void lcd_driver_fill_colour( uint16_t in_posx, uint16_t in_posy, uint16_t in_width, uint16_t in_height, PIXEL_TYPE_T colour )
{
    uint16_t x, y;
	uint16_t posx = in_posx * PIXEL_X_SCALE;
	uint16_t posy = in_posy * PIXEL_Y_SCALE;
	uint16_t width = in_width * PIXEL_X_SCALE;
	uint16_t height = in_height * PIXEL_Y_SCALE;

    if( lcd_buffer == NULL ) return;

	uint32_t lcd_offset = ( posy * SDL_SCREEN_WIDTH ) + posx;

	if( posy >= SDL_SCREEN_HEIGHT )
	{
		return;
	}
   
    for( y = 0; y < height; y++ )
    {
		if( ( posy + y ) >= SDL_SCREEN_HEIGHT )
		{
			break;
		}

        for( x = 0; x < width; x++ )
        {
			if( ( posx + x ) >= SDL_SCREEN_WIDTH )
			{
				break;
			}
            
			lcd_buffer[ lcd_offset + x ] = colour;
        }

		lcd_offset += SDL_SCREEN_WIDTH;
    }    
}
