#include "jwm_port.h"

const char WmbJwm_DefaultConfiguration[] =
    "<JWM>"
    "<Desktops width=\"1\" height=\"1\"><Background type=\"solid\">#304454</Background></Desktops>"
    "<FocusModel>click</FocusModel>"
    "<WindowStyle><Font>fixed</Font><Width>4</Width><Height>22</Height>"
    "<Foreground>#ffffff</Foreground><Background>#506070</Background>"
    "<Active><Foreground>#ffffff</Foreground><Background>#204878</Background></Active>"
    "</WindowStyle>"
    "<MoveMode>opaque</MoveMode><ResizeMode>opaque</ResizeMode>"
    "<RootMenu onroot=\"3\">"
    "<Program label=\"Open demo\">demo</Program>"
    "<Exit label=\"Exit desktop\" confirm=\"false\"/>"
    "</RootMenu>"
    "<Tray x=\"0\" y=\"-1\" height=\"26\"><TaskList/><Clock format=\"%H:%M:%S\"/></Tray>"
    "</JWM>";
