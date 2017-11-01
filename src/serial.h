/*****************************************************************************
* Description       : Header file for serial.c
****************************************************************************/

void initUart( void );
void sendChar( unsigned char );
void sendString(char* s);
unsigned char getChar( void );
unsigned char getCharTimeout(unsigned int timeout_ms);
