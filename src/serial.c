/*****************************************************************************
* Description       : UART communication routines
****************************************************************************/
#include "defines.h"
#include <util/delay.h>

void initUart(void)
{
  BAUD_RATE_LOW_REG = BRREG_VALUE;
  UART_CONTROL_REG = (1 << ENABLE_RECEIVER_BIT) |
                     (1 << ENABLE_TRANSMITTER_BIT); // enable receive and transmit 

  UCSR0C |= (1 << UCSZ00)|(1 << UCSZ01); // set character length to 8 bits
 
}


void sendChar(unsigned char c)
{
  UART_DATA_REG = c;                                   // prepare transmission
  while (!(UART_STATUS_REG & (1 << TRANSMIT_COMPLETE_BIT))) { }// wait until byte sent
  UART_STATUS_REG |= (1 << TRANSMIT_COMPLETE_BIT);          // delete TXCflag
}


void sendString(char* s)
{

	while (*s!='\0')
	{
		sendChar(*s);
		s++;
	}
    sendChar('\r');
    sendChar('\n');

}


unsigned char getChar()
{
  while(!(UART_STATUS_REG & (1 << RECEIVE_COMPLETE_BIT))) 
  { 
	
  }  // wait for data
  return UART_DATA_REG;
}


//timeout
unsigned char getCharTimeout(unsigned int timeout_ms)
{
  while(!(UART_STATUS_REG & (1 << RECEIVE_COMPLETE_BIT))) 
  { 	
	if (timeout_ms==0) 
		return 0;
	else
		timeout_ms--;
		
	_delay_us(100);	
  }  // wait for data
  return UART_DATA_REG;
}
