#include <SDL.h>
#include "sdl_lib_export.h"
#include "sdl_main.h"
#include "sdl_window.h"
#include "sdl_input.h"

#include "types.h"
#include "buffer.h"
#include "hal_drivers.h"

///////////////////////////////////////////////////////////////////////////

#define KEYPAD_BUFFER_SIZE              20

///////////////////////////////////////////////////////////////////////////

static buffer_data_t keypad_buffer_data;
static uint8_t keypad_buffer[ KEYPAD_BUFFER_SIZE ];

///////////////////////////////////////////////////////////////////////////

void keypad_driver_event_key( uint32_t window_id, uint8_t key_code, uint16_t control_key, uint8_t state )
{
    UNUSED(window_id);

    if( key_code == 0 )
    {
        buffer_write( &keypad_buffer_data, CMD_MODIFIER );
        buffer_write( &keypad_buffer_data, ( uint8_t )control_key );
    }
    else if( key_code == 1 )
    {
        bool_t valid_key = FALSE;

        switch( control_key )
        {
            case EVENT_KEY_BACKSPACE:
            case EVENT_KEY_ENTER:
            case EVENT_KEY_ESC:
            case EVENT_KEY_UP:
            case EVENT_KEY_DOWN:
            case EVENT_KEY_RIGHT:
            case EVENT_KEY_LEFT:
                valid_key = TRUE;
                break;

            default:
                valid_key = FALSE;
                break;            
        }

        if( valid_key == TRUE )
        {
            buffer_write( &keypad_buffer_data, ( state == KEY_DOWN ) ? CMD_KEY_DOWN : CMD_KEY_UP );

            switch( control_key )
            {
                case EVENT_KEY_BACKSPACE:
                    buffer_write( &keypad_buffer_data, KEY_DATA_BACKSPACE );
                    break;

                case EVENT_KEY_ENTER:
                    buffer_write( &keypad_buffer_data, KEY_DATA_ENTER );
                    break;

                case EVENT_KEY_ESC:
                    buffer_write( &keypad_buffer_data, KEY_DATA_ESCAPE );
                    break;

                case EVENT_KEY_UP:
                    buffer_write( &keypad_buffer_data, KEY_DATA_UP );
                    break;

                case EVENT_KEY_DOWN:
                    buffer_write( &keypad_buffer_data, KEY_DATA_DOWN );
                    break;

                case EVENT_KEY_RIGHT:
                    buffer_write( &keypad_buffer_data, KEY_DATA_RIGHT );
                    break;

                case EVENT_KEY_LEFT:
                    buffer_write( &keypad_buffer_data, KEY_DATA_LEFT );
                    break;            
            }
        }

    }
    else if( ( key_code >= 0x20 ) && ( key_code <= 125 ) )
    {
        buffer_write( &keypad_buffer_data, ( state == KEY_DOWN ) ? CMD_KEY_DOWN : CMD_KEY_UP );
        buffer_write( &keypad_buffer_data, key_code );
    }
}

///////////////////////////////////////////////////////////////////////////

void keypad_driver_init( void )
{
    buffer_init( &keypad_buffer_data, keypad_buffer, KEYPAD_BUFFER_SIZE );
}

bool_t keypad_driver_has_data( void )
{
    return buffer_is_empty( &keypad_buffer_data ) ? FALSE : TRUE;
}

uint8_t keypad_driver_read_data( void )
{
    return buffer_read( &keypad_buffer_data );
}
