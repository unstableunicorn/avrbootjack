#include "defines.h"
#include "serial.h"
#include "flash.h"
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define SPIFbit 7
#define SPR (1<<0)
#define SpmCsr 0x37
#define SpmCsrMem (SpmCsr+0x20)

#define IOAddrInsMask(aPort) (((aPort&0x30)<<5)|(aPort&7))

#define	PAGESIZE	256
#define	APP_END     0x1F800	
#define BOOT_END    0x1FFFE
#define BOOT_START 0x1F8Ff 
#define StsIns 0x9200
#define StsRegMask 0x01f0
#define OutSpmCsrIns (0xb800+IOAddrInsMask(SpmCsr))
#define OutSpmCsrRegMask 0x01f0
#define SpmIns 0x95e8
#define TestTimerWait 0x2E


typedef enum {
	SpmTypeStsIdeal=0,
	SpmTypeStsSecondary,
	SpmTypeOutIdeal,
	SpmTypeOutSecondary,
	SpmTypeNone=7
} SpmType;

#define FlashPageSize (1<<FlashPageSizeBits)
#define FlashPageSizeInWords (1<<(FlashPageSizeBits-1))
#define FlashSpmEnMask (1<<0)
#define FlashSpmEraseMask (1<<1)
#define FlashSpmWritePageMask (1<<2)
#define FlashSpmBlbSetMask (1<<3)
#define FlashSpmRwwsReMask (1<<4)
#define FlashSpmRwwsBusyMask (1<<6)

#define FlashSpmEn FlashSpmEnMask
#define FlashSpmErase (FlashSpmEraseMask|FlashSpmEnMask)
#define FlashSpmWritePage (FlashSpmWritePageMask|FlashSpmEnMask)
#define FlashSpmBlbSet (FlashSpmBlbSetMask|FlashSpmEnMask)
#define FlashSpmRwws (FlashSpmRwwsReMask|FlashSpmEnMask)
#define FlashSpmRwwsBusy (FlashSpmRwwsBusyMask)

typedef union
{
    struct 
    {
        uint8_t zl;
        uint8_t zh;
        uint8_t rz;
		uint8_t res;
        
    };
    uint32_t add_32;
} ADD_T;

ADD_T SpmSequenceAddr;

void soft_reset()
{
    wdt_enable(WDTO_15MS);
    for(;;){}
}

void CheckBootLock(void)
{
	if((_GET_LOCK_BITS()&0x3f)!=0x3f)
    {
        sendstring("AVRBCE9");
        soft_reset();
	}
}

uint16_t GetPgmWord(uint32_t address)
{
    uint16_t data = 0;
	#ifndef LARGE_MEMORY
		address >>=1;
	#endif
    data = _LOAD_PROGRAM_MEMORY( (address) + 1);
    data = (data << 8) & 0xFF00;
	data |= _LOAD_PROGRAM_MEMORY(address)&0x00FF;

    return data;
}


uint8_t FindSpm(void)
{
	uint8_t spmType=SpmTypeNone;
	uint16_t StsIn_check, SpmCsrMem_check, SpmIns_check;
	uint8_t count = 0;
	
	// looking for ?0 92 57 00 e8 95
	for(uint32_t i = APP_END; i < BOOT_END; i += 2) {
		StsIn_check = GetPgmWord((i));
		StsIn_check &= ~StsRegMask;
		SpmCsrMem_check = GetPgmWord(i+2);
		SpmIns_check = GetPgmWord(i+4);
		if( StsIn_check == StsIns && SpmCsrMem_check == SpmCsrMem && SpmIns_check == SpmIns)
        {
			if(count == 1)
			{
				spmType=SpmTypeStsIdeal;
				SpmSequenceAddr.add_32=i;
				break;
			}
			count++;
			
		}
	}
	return spmType;
}

void SetupTimer0B(uint8_t cycles)
{
	// clear PCINT2.
	PCICR&=~(1<<PCIE2);	// disable PCINT2 interrupts.
	PCIFR|=(1<<PCIE2);	// clear any pending PCINT2 interrupts.
	
	TCCR0B=0;	// stop the timer.
	TCCR0A=0;	// mode 0, no OCR outputs.
	TCNT0=0;	// reset the timer
	TIFR0=(1<<OCF0B)|(1<<OCF0A)|(1<<TOV0);	// clear all pending timer0 interrupts.
	OCR0B=cycles;	// 40 clocks from now (40 in test, 31 for real).
	TIMSK0=(1<<OCIE0B);	// OCR0B interrupt enabled.
}


void  SpmLeapCmd( uint32_t addr, uint8_t spmCmd, uint16_t optValue)
{
	uint8_t cmdReg,tmp=0;
    const ADD_T spmaddr = SpmSequenceAddr; //only constants seems to work so create one here.

    cmdReg=(uint8_t)((GetPgmWord(SpmSequenceAddr.add_32)>>4)&0x1f);

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
				//"break\n" // break here for testing.
                "out %2,%11\n"	// set TCCR0B so off we go. This is time 0c.
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
                "pop r1\n" // %0          %1            %2                         %3
                "pop r0\n" : "=d" (tmp), "=r" (addr) : "I" (_SFR_IO_ADDR(TCCR0B)), "r" (cmdReg),
                //%4           %5              %6
                "r" (spmCmd), "r" (optValue), "0" (addr),
                //%7              %8               %9
                "m" (spmaddr.zl), "m" (spmaddr.zh),"m" (spmaddr.rz),
                // %10          %11
                "I" (SpmCsr), "d" (tmp) );
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


void WriteBlock(uint8_t *src, uint16_t dst, uint16_t size)
{	
	uint16_t data = 0xFF;
	for(uint16_t i=1; i <= size; i+=2)
	{
		data = *((uint16_t *)src);
		SpmLeapCmd(dst+i,FlashSpmEn,data);	
		src+=2;
	}
	SpmLeapCmd(dst,FlashSpmErase,data);
	SpmLeapCmd(dst,FlashSpmWritePage,data);
}



void BootJacker(void)
{
    CheckBootLock();
    uint8_t spmType=FindSpm();
	if(spmType==SpmTypeStsSecondary || spmType==SpmTypeStsIdeal)
		SetupTimer0B(TestTimerWait);	// sts timing.
	else
		soft_reset();
    
	//sendstring("Found SPM\r\n");
    uint8_t somedata[PAGESIZE] = {
								0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
								0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
								0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,
								0x70,0x92,0x57,0x00,0xe8,0x95,0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,0x7F,
								0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,
								0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
								0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,
								0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFD
							};
	//write spm command to end of boot where it won't get overwritten for use in the rest of the code
	//sendstring("WSPCSPM1\r\n");
	uint8_t leapcmd[6] = {0x70,0x92,0x57,0x00,0xe8,0x95};
	WriteBlock(leapcmd, BOOT_END-6, 6);
	
	//SpmSequenceAddr.add_32 = BOOT_END-6;
    //write some test data
	//sendstring("WDATA001\r\n");
    for(uint32_t address = BOOT_START; address <= BOOT_END+PAGESIZE; address += PAGESIZE)
    {
        WriteBlock(somedata, address, PAGESIZE-1);
		FindSpm();    
    }
	asm("cli\n");
}

void main()
{
	asm("cli\n");
	wdt_disable();
	BootJacker();
	//asm("break\n");
	for(;;)
	{
		//asm("break\n");
		_delay_ms(1000);
		//sendstring("Done");
	}
}
