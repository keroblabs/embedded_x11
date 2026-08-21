#ifndef SDL_LIB_EXPORT_H_
#define SDL_LIB_EXPORT_H_

//////////////////////////////////////////////////////////////////////////////////////

typedef void ( * event_mouse_t )( uint32_t window_id, uint16_t posx, uint16_t posy, uint16_t posz, uint8_t button_state );
typedef void ( * event_delta_t )( uint32_t window_id, int32_t deltax, int32_t deltay, uint16_t posz, uint8_t button_state );
typedef void ( * event_wheel_t )( uint32_t window_id, int32_t deltax, int32_t deltay );
typedef void ( * event_key_t   )( uint32_t window_id, uint8_t key_code, uint16_t control_key, uint8_t state );

//////////////////////////////////////////////////////////////////////////////////////

typedef struct sdl_api_t_
{
    uint32_t ( *create_window )( const char * name, uint16_t width, uint16_t height );

    void     ( *set_mouse_handler )( uint32_t window_id, event_mouse_t handler );
    void     ( *set_mouse_wheel_handler )( uint32_t window_id, event_wheel_t handler );
    void     ( *set_key_handler )( uint32_t window_id, event_key_t handler );

    uint16_t ( *get_width )( uint32_t window_id );
    uint16_t ( *get_height )( uint32_t window_id );

    void * ( *get_buffer )( uint32_t window_id );

    void ( *destroy )( uint32_t window_id );
    void ( *terminate )( void );
    int  ( *process )( void );

} sdl_api_t;

//////////////////////////////////////////////////////////////////////////////////////

sdl_api_t * library_entry_point( void );

//////////////////////////////////////////////////////////////////////////////////////

#endif // SDL_LIB_EXPORT_H_
