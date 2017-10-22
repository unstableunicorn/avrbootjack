/*****************************************************************************
*
* Atmel Corporation
*
* File              : serial.c
* Compiler          : IAR C 3.10C Kickstart, AVR-GCC/avr-libc(>= 1.2.5)
* Revision          : $Revision: 1.7 $
* Date              : $Date: Tuesday, June 07, 200 $
* Updated by        : $Author: raapeland $
*
* Support mail      : avr@atmel.com
*
* Target platform   : All AVRs with bootloader support
*
* AppNote           : AVR109 - Self-programming
*
* Description       : UART communication routines
****************************************************************************/
#include "defines.h"
#include <util/delay.h>

void initbootuart(void)
{
  BAUD_RATE_LOW_REG = BRREG_VALUE;
  UART_CONTROL_REG = (1 << ENABLE_RECEIVER_BIT) |
                     (1 << ENABLE_TRANSMITTER_BIT); // enable receive and transmit 

  UCSR0C |= (1 << UCSZ00)|(1 << UCSZ01); // set character length to 8 bits
 
}





void sendchar(unsigned char c)
{
  UART_DATA_REG = c;                                   // prepare transmission
  while (!(UART_STATUS_REG & (1 << TRANSMIT_COMPLETE_BIT))) { }// wait until byte sent
  UART_STATUS_REG |= (1 << TRANSMIT_COMPLETE_BIT);          // delete TXCflag
}

void sendstring(char* s)
{

	while (*s!='\0')
	{
		sendchar(*s);
		s++;
	}

}


unsigned char recchar()
{
  while(!(UART_STATUS_REG & (1 << RECEIVE_COMPLETE_BIT))) 
  { 
	
  }  // wait for data
  return UART_DATA_REG;
}

//timeout
//added by jess
unsigned char recchar_timeout(unsigned int timeout_ms)
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
