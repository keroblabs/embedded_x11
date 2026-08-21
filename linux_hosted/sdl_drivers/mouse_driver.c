#include <SDL.h>
#include "sdl_lib_export.h"
#include "sdl_main.h"
#include "sdl_window.h"
#include "sdl_input.h"

#include "types.h"
#include "buffer.h"
#include "hal_drivers.h"

///////////////////////////////////////////////////////////////

#define DEFAULT_BUFFER_SIZE         128
#define MOUSE_MAX_DEV               2
#define SDL_MOUSE_INDEX             0
#define SCALE_RANGE                 4096

///////////////////////////////////////////////////////////////////////////

typedef struct _command_pos_t_
{
	uint16_t val_x;
	uint16_t val_y;
} param_pos_t;

typedef struct mouse_config_t_
{
	param_pos_t min_pos;
	param_pos_t max_pos;
	param_pos_t curr_pos;
    uint16_t posz;

} mouse_config_t;

typedef struct mouse_drv_t_
{
    buffer_data_t output_buffer_data;
    uint8_t output_buffer[ DEFAULT_BUFFER_SIZE ];

    buffer_data_t command_buffer_data;
    uint8_t command_buffer[ DEFAULT_BUFFER_SIZE ];

    int32_t scale_mul;
    bool_t touch_mode;

    int16_t raw_posx;
    int16_t raw_posy;
    int16_t raw_posz;
    uint8_t button_state;

    int16_t wheel_posx;
    int16_t wheel_posy;

} mouse_drv_t;

///////////////////////////////////////////////////////////////

static mouse_drv_t mouse_drv_list[ MOUSE_MAX_DEV ];

///////////////////////////////////////////////////////////////////////////

void mouse_driver_event_mouse( uint32_t window_id, int32_t deltax, int32_t deltay, uint16_t posz, uint8_t button_state )
{
    UNUSED(window_id);
    UNUSED(button_state);

    /* SDL has exactly one pointer; only device 0 is initialised. */
    mouse_drv_t * mouse_drv = &mouse_drv_list[ SDL_MOUSE_INDEX ];

    mouse_drv->raw_posx = ( int16_t )( ( deltax * mouse_drv->scale_mul ) / SCALE_RANGE );
    mouse_drv->raw_posy = ( int16_t )( ( deltay * mouse_drv->scale_mul ) / SCALE_RANGE );

    mouse_drv->raw_posz = mouse_drv->touch_mode ? posz : 0;
    mouse_drv->button_state = mouse_drv->touch_mode ? 0 : posz;

    buffer_write( &mouse_drv->output_buffer_data, CMD_MOUSE_X );
    buffer_write( &mouse_drv->output_buffer_data, ( uint8_t )mouse_drv->raw_posx & 0x0FF );

    buffer_write( &mouse_drv->output_buffer_data, CMD_MOUSE_Y );
    buffer_write( &mouse_drv->output_buffer_data, ( uint8_t )mouse_drv->raw_posy & 0x0FF );

    // in touch mode, send posz as the touch pressure (0 = no press, >0 = press)
    // otherwise, send button status (0 = no button, 1 = MOUSE_BUTTON_LEFT, 2 = MOUSE_BUTTON_MIDDLE, 3 = MOUSE_BUTTON_RIGHT, etc)
    buffer_write( &mouse_drv->output_buffer_data, mouse_drv->touch_mode ? CMD_MOUSE_Z : CMD_MOUSE_BUTTON );
    buffer_write( &mouse_drv->output_buffer_data, ( uint8_t )( mouse_drv->touch_mode ? mouse_drv->raw_posz : mouse_drv->button_state ) & 0x0FF );
}

void mouse_driver_event_wheel( uint32_t window_id, int32_t wheel_x, int32_t wheel_y )
{
    UNUSED(window_id);

    mouse_drv_t * mouse_drv = &mouse_drv_list[ SDL_MOUSE_INDEX ];

    mouse_drv->wheel_posx = ( int16_t )( ( wheel_x * mouse_drv->scale_mul ) / SCALE_RANGE );
    mouse_drv->wheel_posy = ( int16_t )( ( wheel_y * mouse_drv->scale_mul ) / SCALE_RANGE );

    buffer_write( &mouse_drv->output_buffer_data, CMD_MOUSE_WHEEL_X );
    buffer_write( &mouse_drv->output_buffer_data, ( uint8_t )mouse_drv->wheel_posx & 0x0FF );

    buffer_write( &mouse_drv->output_buffer_data, CMD_MOUSE_WHEEL_Y );
    buffer_write( &mouse_drv->output_buffer_data, ( uint8_t )mouse_drv->wheel_posy & 0x0FF );
}

///////////////////////////////////////////////////////////////////////////

void mouse_driver_init( uint8_t index )
{
    mouse_drv_t * mouse_drv = &mouse_drv_list[ index ];

    memset( mouse_drv, 0x00, sizeof( mouse_drv_t ) );

    buffer_init( &mouse_drv->output_buffer_data, mouse_drv->output_buffer, DEFAULT_BUFFER_SIZE );
    buffer_init( &mouse_drv->command_buffer_data, mouse_drv->command_buffer, DEFAULT_BUFFER_SIZE );

    mouse_drv->scale_mul = SCALE_RANGE;
    mouse_drv->touch_mode = FALSE;
}

bool_t mouse_driver_has_data( uint8_t index )
{
    mouse_drv_t * mouse_drv = &mouse_drv_list[ index ];
    return buffer_is_empty( &mouse_drv->output_buffer_data ) ? FALSE : TRUE;
}

uint8_t mouse_driver_read_data( uint8_t index )
{
    mouse_drv_t * mouse_drv = &mouse_drv_list[ index ];
    return buffer_read( &mouse_drv->output_buffer_data );
}

static void mouse_command_process( mouse_drv_t * mouse_drv )
{
    uint32_t read_size = buffer_get_size( &mouse_drv->command_buffer_data );

    if( read_size == 0 ) return;

    uint8_t cmd_byte = buffer_peek( &mouse_drv->command_buffer_data );

    switch( cmd_byte )
    {
        case MOUSE_CONFIG_SETTING:
            if( read_size >= ( 2 + sizeof( int32_t )) )
            {
                buffer_read( &mouse_drv->command_buffer_data ); // take command from buffer
                uint8_t next_param = buffer_read( &mouse_drv->command_buffer_data );

                switch( next_param )
                {
                    case MOUSE_SETTING_SCALE:
                        buffer_read_buffer( &mouse_drv->command_buffer_data, ( uint8_t * )&mouse_drv->scale_mul, sizeof( int32_t ) );
                        break;
                }
            }
            break;

        case MOUSE_CONFIG_CURSOR_SHOW:
            if( read_size >= 2 )
            {
                buffer_read( &mouse_drv->command_buffer_data ); // take command from buffer
                uint8_t cursor_state = buffer_read( &mouse_drv->command_buffer_data );
                SDL_ShowCursor( cursor_state ? SDL_DISABLE : SDL_ENABLE );
            }
            break;

        case MOUSE_CONFIG_TOUCH_MODE:
            if( read_size >= 2 )
            {
                buffer_read( &mouse_drv->command_buffer_data ); // take command from buffer
                uint8_t touch_mode = buffer_read( &mouse_drv->command_buffer_data );
                mouse_drv->touch_mode = touch_mode ? TRUE : FALSE;
            }
            break;

        default:
            buffer_read( &mouse_drv->command_buffer_data ); // take command from buffer
            break;            
    }
}

void mouse_driver_process( void )
{
    for( int index = 0; index < MOUSE_MAX_DEV; index++ )
    {
        mouse_drv_t * mouse_drv = &mouse_drv_list[ index ];
        mouse_command_process( mouse_drv );
    }
}

void mouse_driver_write_command( uint8_t index, const uint8_t *buffer, uint32_t max_size )
{
    mouse_drv_t * mouse_drv = &mouse_drv_list[ index ];
    buffer_write_buffer( &mouse_drv->command_buffer_data, ( uint8_t * )buffer, max_size );
}

