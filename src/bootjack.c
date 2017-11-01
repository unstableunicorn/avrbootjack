/********************************************************************************
 *
 * Author       : Elric Hindy
 *
 * Description  : This program updates the bootloader of an unprotected avr
 *                bootloader by hijacking the spm instruction in the bootloader
 *                section.
 ********************************************************************************/
#include "defines.h"
#include "serial.h"
#include "flash.h"
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <util/delay.h>
//include the new bootloder array and definitions
#include "new_boot.h"


//define boot code to represent this program
#define BOOT_CODE_STRING "AVRBC10\0"
// define the SPM Control and Status Register
#define SPM_CONTROL_STATUS_REGISTER 0x37
// define the memory location of this register (0x20 + the default)
#define SPM_CONTROL_STATUS_REGISTER_MEMORY (SPM_CONTROL_STATUS_REGISTER+0x20)

#define STS_INSTRUCTION 0x9200
#define STS_REGISTER_MASK 0x01f0
#define SPM_INSTRUCTION 0x95e8
/*
 * This timer sets the cycle count from the enable timer asm instruction to the spm call
 * This value may require adjustment depending on device and requires cycle 
 * counting the operations
*/
#define TEST_TIMER_WAIT 0x2E

// define the bit masks and SPM instruction masks
#define FLASH_SPM_EN_MASK (1<<0)
#define FLASH_SPM_ERASE_MASK (1<<1)
#define FLASH_SPM_WRITE_PAGE_MASK (1<<2)
#define FLASH_SPM_EN FLASH_SPM_EN_MASK
#define FLASH_SPM_ERASE (FLASH_SPM_ERASE_MASK|FLASH_SPM_EN_MASK)
#define FLASH_SPM_WRITE_PAGE (FLASH_SPM_WRITE_PAGE_MASK|FLASH_SPM_EN_MASK)


/*
 * A large address broken to allow easy access to the
 * Z High,
 * Z Low and
 * Ramp Z bytes
 */
typedef union
{
    struct 
    {
        uint8_t zl;
        uint8_t zh;
        uint8_t rz;
		uint8_t res; // reserved as not used
    };
    uint32_t add_32;
} Address32;


/*
 *A watchdog reset routine to reset the firmware
 * */
void softReset()
{
    sendString("AVRERR0");
    wdt_enable(WDTO_15MS);
    for(;;){}
}


/*
 * Checks if the bootloader is protected
 * This program can not work on a protected bootloader
 */
uint8_t checkBootLock(void)
{
	if((_GET_LOCK_BITS()&0x3F)!=0x3F)
    {
        return 0;
	}
    else
    {
        return 1;
    }
}


/*
 * get a word from a flash address
 */
uint16_t getFlashWord(uint32_t address)
{
    uint16_t data = 0;
    data = _LOAD_PROGRAM_MEMORY(address + 1);
    data = (data << 8) & 0xFF00;
	data |= _LOAD_PROGRAM_MEMORY(address)&0x00FF;

    return data;
}


//initialise global SPM sequence address, this is set by the find spm instruction
Address32 spm_sequence_address;

/*
 * This function searches through the bootloader
 * section for a valid 'out' and 'spm' instruction 
 * and stores the location in the global spm address variable
 * This instruction is used to execute a page
 * load, write and erase instructions.
 * Every bootloader will have this in the code somewhere.
 */
uint8_t findSpmInstruction(void)
{
	uint16_t sts_instruction_check, spm_csr_mem_check, spm_instruction_check;
	
	for(uint32_t i = APP_END; i < BOOT_END; i += 2)
    {
		sts_instruction_check = getFlashWord((i));
		sts_instruction_check &= ~STS_REGISTER_MASK;
		spm_csr_mem_check = getFlashWord(i+2);
		spm_instruction_check = getFlashWord(i+4);

		if( sts_instruction_check == STS_INSTRUCTION &&
            spm_csr_mem_check == SPM_CONTROL_STATUS_REGISTER_MEMORY &&
            spm_instruction_check == SPM_INSTRUCTION)
        {
            spm_sequence_address.add_32=i;
            return 1;
		}
	}
    //did not find spm!!
    sendString("AVRNSPM");
	return 0;
}

/*
 * Sets up the timer to interrupt the spm instruction
 */
void setupTimer0B(uint8_t cycles)
{
	// clear PCINT2.
	PCICR&=~(1<<PCIE2);	// disable PCINT2 interrupts.
	PCIFR|=(1<<PCIE2);	// clear any pending PCINT2 interrupts.
	
	TCCR0B=0;	// stop the timer.
	TCCR0A=0;	// mode 0, no OCR outputs.
	TCNT0=0;	// reset the timer
	TIFR0=(1<<OCF0B)|(1<<OCF0A)|(1<<TOV0);	// clear all pending timer0 interrupts.
	OCR0B=cycles;	// cycles until interrupt is called
	TIMSK0=(1<<OCIE0B);	// OCR0B interrupt enabled.
}


/*
 * The heart of the program!
 * This function leaps to the spm instructions found in the bootloader.
 * The AVR's can only write to flash while in the bootloader section
 * so this function loads the registers use by the found instruction and 
 * puts the program counter pointer at the spm instructions found in
 * the bootloader to load the data and commands in for the spm to execute.
 * It then places the current program counter pointer on the stack
 * for the interrupt to pop and return to this function.
 * We must interrupt the spm instruction call within 4 clock cycles
 * of the call so that we can pop the previous program counter in to
 * the call stack and return to this function. Otherwise the program
 * counter will continue through the bootloader executing any instructions
 * after the found spm instruction.
 */
void spmLeap( uint32_t address, uint8_t spm_cmd, uint16_t data)
{
	uint8_t cmd_register, tmp=0;
    //only constants seem to work in the asm address calls
    const Address32 spmaddr = spm_sequence_address;

    //get the register address used by the current found spm command
    cmd_register=(uint8_t)((getFlashWord(spm_sequence_address.add_32)>>4)&0x1f);

	sei();
    asm volatile(
                "push r0\n"
                "push r1\n"		// needed for opt command.
                "push %11\n"
                "push r30\n"
                "push r31\n"
                "SpmLeapCmdWaitSpm: in %11,%10\n"	// 
                "sbrc %11,0\n"	//wait for spm operation complete.
                "rjmp SpmLeapCmdWaitSpm\n"
                "ldi %11,1\n"	// timer 0 start at fClk
                "out %2,%11\n"	// set TCCR0B so off we go.
                "movw r0,%5\n"	// set the value to be written.
                "mov r30,%3\n"	// get the register used by the sequence's spm command.
                "ldi r31,0\n"	// z^reg to save.
                "ld %11,Z\n"	// get the reg
                "push %11\n"	// saved it, now we can overwrite with spm command.
                "ldi r30,lo8(pm(SpmLeapCmdRet))\n"
                "ldi r31,hi8(pm(SpmLeapCmdRet))\n"
                "push r30\n"
                "push r31\n"	// return address must be pushed big-endian.
                "ldd r30,%7\n"	// lo uint8_t of Spm sequence address
                "ldd r31,%8\n" // hi uint8_t of Spm sequence address. z^sequence in code.
                "lsr r31\n"
                "ror r30\n"		// div 2 to get correct Spm program address.
				"ori r31, 0x80\n"
                "push r30\n"
                "push r31\n"	// Spm sequence program address must be pushed big-endian.		
                "push %A6\n"	// before we overwrite reg used by sequence's spm command
                                // we must first save the spm target address
                "push %B6\n"	// in case it would get overwritten by the st Z.
                "mov r30,%3\n"	// get the register used by the sequence's spm command.
                "ldi r31,0\n"	// z^reg to save.
                "st Z,%4\n"	// store the command in the reg.
                "pop r31\n"
                "pop r30\n"	// restore the spm target address into Z.
				//"break\n" // break here for testing.
                "ret\n"			// return to bootloader.
                "SpmLeapCmdRet:pop %11\n"		// restore command Reg.
                "mov r30,%3\n"
                "ldi r31,0\n"	// z^reg to save.
                "st Z,%11\n"	// pop the reg		
                "pop r31\n"
                "pop r30\n"
                "pop %11\n"
                "pop r1\n" // %0          %1              %2                          %3
                "pop r0\n" : "=d" (tmp), "=r" (address) : "I" (_SFR_IO_ADDR(TCCR0B)), "r" (cmd_register),
                //%4           %5           %6
                "r" (spm_cmd), "r" (data), "0" (address),
                //%7              %8               %9
                "m" (spmaddr.zl), "m" (spmaddr.zh),"m" (spmaddr.rz),
                // %10                             %11
                "I" (SPM_CONTROL_STATUS_REGISTER), "d" (tmp) );
}

/*
 * write a block of data to program memory
 */
uint8_t writeBlock(uint8_t *src, uint16_t address, uint16_t size)
{	
	uint16_t data = 0xFF; //initialise to 0xFF, should be overwritten

    /* 
     * store the data to be written to the page
     * Note: if less than a page is specified the
     * rest of the data will be 0xFF due to the erase
     * and more than a page will be ignored
     */
	for(uint16_t i=1; i <= size || i <= PAGESIZE; i+=2)
	{
		data = *((uint16_t *)src);
		spmLeap(address+i, FLASH_SPM_EN, data);	
		src+=2;
	}

    //erase the page that is about to be written
	spmLeap(address,FLASH_SPM_ERASE,data);

    //must refind spm instruction here in case the one currently in use was just erased
    if(!findSpmInstruction())    
    {
        return 0; //fail
    }

    //Write the new page to the flash memory
	spmLeap(address,FLASH_SPM_WRITE_PAGE,data);

    return 1; //Success
}


/*
 * Writes the new bootloader to the bootlader section
 * Note: This process will fail if the new bootloader only 
 * has spm instructions in the last page.
 * As long as there is one spm instruction in memory at all times
 * even after a page erase then this will work.
 */
uint8_t bootJack(void)
{
	/* Write spm command to end of boot section where it won't
     * get overwritten until the last call so it can be used
     * in the rest of the spm calls if others were deleted
     * will still fail if no spm calls in every other page as this is the last to get written
     */
	sendString("AVRWSPM");
	uint8_t leapcmd[6] = {0x70,0x92,0x57,0x00,0xe8,0x95};

	if(!writeBlock(leapcmd, (uint16_t)(BOOT_END-6), 6))
    {
        return 0; //failed
    }
	
    //write bootlaoder
	sendString("AVRWDAT");
    uint8_t page_count = 0;
    for(uint32_t address = BOOT_START, page_start=0;
        address <= BOOT_END+PAGESIZE;
        address += PAGESIZE, page_start+=PAGESIZE)
    {
        sendString("AVRPGWR");

        if(!writeBlock(newbootloader+page_start, address, PAGESIZE-1))
        {
            return 0; //failed
        }

        //break after all pages written if less than bootloader section
        page_count ++;
        if(page_count == NUMBER_OF_PAGES)
        {
            break;
        }
    }

    return 1; //success
}

void blockRead(unsigned int size, unsigned char memory_type, unsigned long *address)
{
    // Flash memory type.
    if(memory_type=='F')
    {
        (*address) <<= 1; // Convert address to bytes temporarily.

        do
        {
            sendChar( _LOAD_PROGRAM_MEMORY(*address) );
            sendChar( _LOAD_PROGRAM_MEMORY((*address)+1) );
            (*address) += 2; // Select next word in memory.
            size -= 2; // Subtract two bytes from number of bytes to read
        } while (size); // Repeat until all block has been read

        (*address) >>= 1; // Convert address back to Flash words again.
    }
    else
    {
        //send unknown memory_type type
        sendChar('?');
    }
}

unsigned char blockLoad(unsigned int size, unsigned char memory_type, unsigned long *address)
{
    unsigned char buffer[PAGESIZE];
    uint16_t count = 0;

    // Flash memory type.
    if(memory_type=='F')
    { 
        //load page in to buffer
        do
        {
            buffer[count] = getChar();
            size--; // Reduce number of bytes to write by one.
            count++;
        } while(size); // Loop until all bytes written.

        if(!writeBlock(buffer, (uint16_t) address, PAGESIZE-1))
        {
            return '0';
        }

        return '\r'; // Report programming OK
    }
    else
    {
        // Invalid memory type?
        return '?';
    }
}

void main()
{
    cli(); //disable all interrupts
	wdt_disable();
    initUart();

    //check if boot section is locked
    if(!checkBootLock())
    {
        sendString("AVRERR1");
        _delay_ms(1000);        
        softReset();
    }

    // find spm instruction
	if(findSpmInstruction())
    {
        sendString("AVRFSPM");
		setupTimer0B(TEST_TIMER_WAIT);	// sts timing.
    }
	else
    {
        sendString("AVRERR2");
        _delay_ms(1000);        
		softReset();
    }

    //found spm good to continue and update bootloader
	if(!bootJack())
    {
        //failed to write bootjack!
        sendString("AVRERR3");
    }
    else
    {
        //Send confirmation string that we have completed
        sendString(BOOT_CODE_STRING); //Send ID String
    }

    cli(); //make sure all interrupts are disabled

    //the below code for writing/reading a bootloader is untested and currently not required.
    unsigned long address;
    unsigned int temp_int=0;
	for(;;)
	{
        unsigned char val=getChar(); // Wait for command character
        // Return programmer identifier.
        if(val=='S')
        {
            sendString(BOOT_CODE_STRING); //Send ID String
        }
        // Set address.
        else if(val=='A') // Set address...
        { // NOTE: Flash addresses are given in words, not bytes.
            address=(getChar()<<8) | getChar(); // Read address high and low byte.
            sendChar('\r'); // Send OK back.
        }
        //Block read
        else if(val=='g')
        {
            temp_int = (getChar()<<8) | getChar(); // Get block size.
            val = getChar(); // Get memtype
            blockRead(temp_int,val,&address); // Block read
        }
        //block load.
        else if(val=='B')
        {
          temp_int = (getChar()<<8) | getChar(); // Get block size.
          val = getChar(); // Get memtype.
          sendChar( blockLoad(temp_int,val,&address) ); // Block load.
        }
	}
}


/**
 * The timer interrupt interrupts bootloader execution
 * just after the spm instruction.
 * if we ret then we'll get back to the bootloader.
 * we need to pop the return address and then ret, which
 * should take us back to the SpmLeapCommand.
 **/
ISR(TIMER0_COMPB_vect, ISR_NAKED) // OCR0B
{
    asm volatile(
        "ldi r30,0\n"
        "out %0,r30\n"	// stop timer 0
        "out %1,r30\n"	// reset timer 0.
        "ldi r30,7\n"
        "out %2,r30\n"	// clear interrupts on timer 0.
        "pop r31\n"	// don't care about overwriting Z because SpmLeap doesn't need it.
        "pop r30\n"
        "reti\n" : : "I" (_SFR_IO_ADDR(TCCR0B)), "I" (_SFR_IO_ADDR(TCNT0)), "I" (_SFR_IO_ADDR(TIFR0)));
}
