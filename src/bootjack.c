#include "defines.h"
#include "serial.h"
#include "flash.h"
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#define SPIFbit 7
#define SPR (1<<0)
#define SpmCsr 0x37
#define SpmCsrMem (SpmCsr+0x20)

#define IOAddrInsMask(aPort) (((aPort&0x30)<<5)|(aPort&7))

#define	PAGESIZE	256
#define	APP_END     0x1F800	
#define BOOT_END    0x1FFFE
#define BOOT_START 0x1f8ff 
#define StsIns 0x9200
#define StsRegMask 0x01f0
#define OutSpmCsrIns (0xb800+IOAddrInsMask(SpmCsr))
#define OutSpmCsrRegMask 0x01f0
#define SpmIns 0x95e8
#define TestTimerWait 31

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
        uint8_t res;
        uint8_t rz;
        uint8_t zh;
        uint8_t zl;
    };
    uint32_t add_32;
} ADD_T;

ADD_T SpmSequenceAddr;

void soft_reset()
{
    do
    {
        wdt_enable(WDTO_15MS);
        for(;;){}
    }while(0);
}

uint16_t FlashLpmData(uint16_t addr, uint8_t spmCmd)
{
	uint16_t val;
	asm volatile("push r0\n"
				"push r1\n"
				"push r16\n"
				"push r30\n"
				"push r31\n"
				"movw r30,%1\n"	// set the addr to be written.
				"1: in r16,%3\n"	// 
				"sbrc r16,0\n"	//wait for operation complete.
				"rjmp 1b\n"
				"cli\n"
				"out %3,%2\n"
				"lpm %0,Z\n"			// now we start the load/erase/write.
				"sei\n"
				"pop r31\n"
				"pop r30\n"
				"pop r16\n"
				"pop r1\n"
				"pop r0\n" : "=r" (val): "r" (addr), "r" (spmCmd), "I" (SpmCsr));
	return val;
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
    data = (_LOAD_PROGRAM_MEMORY( (address << 1)+1 ));
    data = (data << 8) & 0xFF00;
    data |=  _LOAD_PROGRAM_MEMORY( (address << 1)+0 )&0x00FF;
    return data;
}


uint8_t FindSpm(void)
{
	uint8_t spmType=SpmTypeNone;

	for(uint32_t i = APP_END; i < BOOT_END && ((spmType & 1) == 1); i += 2) {
		if( (GetPgmWord( i )&~StsRegMask) == StsIns && GetPgmWord( i + 2 ) == SpmCsrMem && GetPgmWord((i + 4)) == SpmIns)
        {
			spmType=(i+8<BOOT_START)?SpmTypeStsIdeal:SpmTypeStsSecondary;
		}
		if( (GetPgmWord(i)&~OutSpmCsrRegMask)==OutSpmCsrIns && GetPgmWord(i + 2)==SpmIns) 
        {
			spmType=(i+6<BOOT_START)?SpmTypeOutIdeal:SpmTypeOutSecondary;
		}
		if(spmType!=SpmTypeNone)
        {
			SpmSequenceAddr.add_32=i;
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

/*#define TCCR0B 0x25*/
/*#define TCNT0 0x26*/
/*#define TIFR0 0x15*/



void SpmLeapCmd( uint32_t addr, uint8_t spmCmd, uint16_t optValue)
{
	uint8_t cmdReg,tmp=0;
    ADD_T spmaddr;
    spmaddr.add_32 = SpmSequenceAddr.add_32;

	cmdReg=(uint8_t)((GetPgmWord(SpmSequenceAddr.add_32)>>4)&0x1f);
	/*PINC|=(1<<4);*/
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
				"lds r30,%7\n"	// lo uint8_t of Spm sequence address
				"lds r31,%8\n" // hi uint8_t of Spm sequence address. z^sequence in code.
                "lds RAMPZ,%9\n" 
				"lsr r31\n"
				"ror r30\n"		// div 2 to get correct Spm program address.
				"push r30\n"
				"push r31\n"	// Spm sequence program address must be pushed big-endian.		
                "push RAMPZ\n"

				"push %A6\n"	// before we overwrite reg used by sequence's spm command
								// we must first save the spm target address
				"push %B6\n"	// in case it would get overwritten by the st Z.

				"mov r30,%3\n"	// get the register used by the sequence's spm command.
				"ldi r31,0\n"	// z^reg to save.
				"st Z,%4\n"	// store the command in the reg.
				"pop r31\n"
				"pop r30\n"	// restore the spm target address into Z.
                "pop RAMPZ\n"
				"ret\n"			// return to bootloader.
				// sts (2c)
				// spm (1c). 42c in total, timer should be set to 40.
				"SpmLeapCmdRet:pop %11\n"		// restore command Reg.
				"mov r30,%3\n"
				"ldi r31,0\n"	// z^reg to save.
				"st Z,%11\n"	// pop the reg		
				"pop r31\n"
				"pop r30\n"
				"pop %11\n"
				"pop r1\n"
				"pop r0\n" : "=d" (tmp), "=r" (addr) : "I" (TCCR0B), "r" (cmdReg), "r" (spmCmd),
								"r" (optValue), "0" (addr), "M" (spmaddr.zl), "M" (spmaddr.zh),"M" (spmaddr.rz) ,"I" (SpmCsr), "d" (tmp) );
}

/**
 * The timer interrupt interrupted bootloader execution
 * just after the spm instruction.
 * if we ret then we'll get back to the bootloader.
 * we need to pop the return address and then ret, which
 * should take us back to the SpmLeapCommand.
 **/
ISR(__vector_15, ISR_NAKED) // OCR0B
{
	asm volatile(
		"ldi r30,0\n"
		"out %0,r30\n"	// stop timer 0
		"out %1,r30\n"	// reset timer 0.
		"ldi r30,7\n"
		"out %2,r30\n"	// clear interrupts on timer 0.
		"pop r31\n"	// don't care about overwiting Z because SpmLeap doesn't need it.
		"pop r30\n"
		"reti\n" : : "I" (TCCR0B), "I" (TCNT0), "I" (TIFR0));
}

const uint8_t BootloaderJmpVector[] PROGMEM =
{ 0x0c, 0x94, 0xc0, 0x3f, // A vector to the 128b mini Bootloader.
  0x57, 0xbf, 0xe8, 0x95, // An out, spm command.
  0x00, 0x00             // A nop instruction.
};


void ProgPage(uint8_t *src, uint16_t dst)
{
	uint16_t data;
	for(uint8_t ix=0; ix < 128; ix += 2) {	// 64 words.
		/*data=GetPgmWord(src);*/
        data = *src;
		SpmLeapCmd(dst+ix,FlashSpmEn,data);
		src+=2;	// advance to next word
	}
	SpmLeapCmd(dst,FlashSpmErase,data);
	SpmLeapCmd(dst,FlashSpmWritePage,data);
}

extern void _Upgrader(void);
extern uint8_t _UpgradeSpmLeap;

void BootJacker(void)
{
    CheckBootLock();
    uint8_t spmType=FindSpm();
	if(spmType==SpmTypeStsSecondary || spmType==SpmTypeStsIdeal)
		SetupTimer0B(TestTimerWait);	// sts timing.
	else
		SetupTimer0B(TestTimerWait-1);	// out timing is one cycle less.
    

    uint8_t somedata[128] = {0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA};


    //write some test data
    for(uint32_t add = BOOT_START; add < BOOT_END; add += PAGESIZE)
    {
        ProgPage(somedata, add);         
    }

	/*if(spmType==SpmTypeStsIdeal || spmType==SpmTypeOutIdeal) {*/
		/*ProgPage((uint8_t*)&_etext, BOOT_START);	// program the micro boot.*/
		/*OCR0B=TestTimerWait-1;	// it's an out command now.*/
		/*SpmSequenceAddr=(ADD_T)&_UpgradeSpmLeap;*/
		/*ProgPage((uint8_t*)BootloaderJmpVector, APP_END);	// program the jump vector.*/
		/*// using the microboot spm.*/
	/*}*/
	/*else {*/
		/*ProgPage((uint8_t*)BootloaderJmpVector, APP_END);	// program the jump vector.*/
		/*OCR0B=TestTimerWait-1;	// it's an out command now.*/
		/*SpmSequenceAddr=APP_END+4;	// it's just after the boot vector.*/
		/*ProgPage((uint8_t*)&_etext, BOOT_START);	// program the micro boot using the jump*/
		/*// vector spm.*/
	/*}*/
    asm("cli\n");
}
