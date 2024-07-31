/* -*- mode: C++; tab-width: 4 -*- */
/* ===================================================================== *\
	Copyright (c) Retro68 contributors.

	This file is part of the Palm OS Emulator.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
\* ===================================================================== */

#include "EmRegsVZAlphaSmart.h"
#include "EmRegsVZPrv.h"
#include "EmSession.h"
#include "EmSPISlaveADS784x.h"
#include "EmSPISlaveSTMicro.h"

enum {
	hwrAlphaSmartPortESpiADCOn    = 0x08,

	hwrAlphaSmartPortKKeyboardOn  = 0x04, // ?
	hwrAlphaSmartPortKBacklightOn = 0x10,
	hwrAlphaSmartPortKLCDEnableOn = 0x80,
};

#undef INSTALL_HANDLER
#define INSTALL_HANDLER(read, write, reg)			\
	this->SetHandler (	(ReadFunction) &EmRegsVZAlphaSmart::read,		\
						(WriteFunction) &EmRegsVZAlphaSmart::write,		\
						addressof (reg),			\
						sizeof (f68VZ328Regs.reg) )

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::EmRegsVZAlphaSmart
// ---------------------------------------------------------------------------

EmRegsVZAlphaSmart::EmRegsVZAlphaSmart (void) :
	EmRegsVZ (),
	fSPISlaveADC (new EmSPISlaveADS784x (kChannelSet2)),
	fSPISlaveSTMicro (new EmSPISlaveSTMicro ())
{
	gSession->fHasVZAlphaSmart = true;
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::~EmRegsVZAlphaSmart
// ---------------------------------------------------------------------------

EmRegsVZAlphaSmart::~EmRegsVZAlphaSmart (void)
{
	gSession->fHasVZAlphaSmart = false;
	delete fSPISlaveADC;
	delete fSPISlaveSTMicro;
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::SetSubBankHandlers
// ---------------------------------------------------------------------------

void EmRegsVZAlphaSmart::SetSubBankHandlers (void)
{
	EmRegsVZ::SetSubBankHandlers ();

	INSTALL_HANDLER (StdRead, spiMasterControlWrite, spiMasterControl);
	INSTALL_HANDLER (StdRead, portKDirWrite, portKDir);
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::CycleSlowly
// ---------------------------------------------------------------------------
// This is mostly copied from EmRegsVZ.

void EmRegsVZAlphaSmart::CycleSlowly (Bool sleeping)
{
	// See if there's anything new ("Put the data on the bus")

	EmRegsVZ::UpdateUARTState (false, 0);
	EmRegsVZ::UpdateUARTState (false, 1);

	// Check to see if the RTC alarm is ready to go off.  First see
	// if the RTC is enabled, and that the alarm event isn't already
	// registered (the latter check is just an optimization).

	if ((READ_REGISTER (rtcIntEnable) & hwrVZ328RTCIntEnableAlarm) != 0 &&
		(READ_REGISTER (rtcIntStatus) & hwrVZ328RTCIntStatusAlarm) == 0)
	{
		uint32	rtcAlarm = READ_REGISTER (rtcAlarm);

		int32	almHour	 = (rtcAlarm & hwrVZ328RTCAlarmHoursMask) >> hwrVZ328RTCAlarmHoursOffset;
		int32	almMin	 = (rtcAlarm & hwrVZ328RTCAlarmMinutesMask) >> hwrVZ328RTCAlarmMinutesOffset;
		int32	almSec	 = (rtcAlarm & hwrVZ328RTCAlarmSecondsMask) >> hwrVZ328RTCAlarmSecondsOffset;
		int32	almInSeconds = (almHour * 60 * 60) + (almMin * 60) + almSec;

		int32	nowHour;
		int32	nowMin;
		int32	nowSec;
		::GetHostTime (&nowHour, &nowMin, &nowSec);
		int32	nowInSeconds = (nowHour * 60 * 60) + (nowMin * 60) + nowSec;

		if (almInSeconds <= nowInSeconds)
		{
			WRITE_REGISTER (rtcIntStatus, READ_REGISTER (rtcIntStatus) | hwrVZ328RTCIntStatusAlarm);
			EmRegsVZ::UpdateRTCInterrupts ();
		}
	}

	UpdateSPISlaveSTMicro ();
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::GetLCDScreenOn
// ---------------------------------------------------------------------------

Bool EmRegsVZAlphaSmart::GetLCDScreenOn (void)
{
	return (READ_REGISTER (portKData) & hwrAlphaSmartPortKLCDEnableOn) != 0;
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::GetLCDBacklightOn
// ---------------------------------------------------------------------------

Bool EmRegsVZAlphaSmart::GetLCDBacklightOn (void)
{
	return (READ_REGISTER (portKData) & hwrAlphaSmartPortKBacklightOn) != 0;
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::GetPortInputValue
// ---------------------------------------------------------------------------

uint8 EmRegsVZAlphaSmart::GetPortInputValue (int port)
{
	if (port == 'D' || port == 'E' || port == 'G')
		return 0;

	uint8 value = EmRegsVZ::GetPortInputValue (port);
	if (port == 'K')
		value |= READ_REGISTER (portKData) & hwrAlphaSmartPortKKeyboardOn;
	return value;
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::GetPortInternalValue
// ---------------------------------------------------------------------------

uint8 EmRegsVZAlphaSmart::GetPortInternalValue (int port)
{
	if (port == 'D')
		return 0x80;

	uint8 value = EmRegsVZ::GetPortInternalValue (port);
	if (port == 'G')
		value |= 4;
	return value;
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::GetKeyInfo
// ---------------------------------------------------------------------------

void EmRegsVZAlphaSmart::GetKeyInfo (int* numRows, int* numCols, uint16* keyMap, Bool* rows)
{
	*numRows = EmSPISlaveSTMicro::kNumRows;
	*numCols = EmSPISlaveSTMicro::kNumCols;
	rows[0] = 0;
	rows[1] = 0;
	rows[2] = 0;
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::GetKeyBits
// ---------------------------------------------------------------------------

uint8 EmRegsVZAlphaSmart::GetKeyBits (void)
{
	return 0;
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::GetSPISlave
// ---------------------------------------------------------------------------

EmSPISlave *EmRegsVZAlphaSmart::GetSPISlave (void)
{
	if ((READ_REGISTER (portEData) & hwrAlphaSmartPortESpiADCOn) == 0)
		return fSPISlaveSTMicro;
	else
		return fSPISlaveADC;
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::UpdatePortDInterrupts
// ---------------------------------------------------------------------------

void EmRegsVZAlphaSmart::UpdatePortDInterrupts ()
{
	uint16	intPendingLo	= READ_REGISTER (intPendingLo) & ~hwrVZ328IntLoAllKeys;

	uint8	portDDir		= READ_REGISTER (portDDir);	// Interrupt on inputs only (when pin is low)
	uint8	portDData		= EmHAL::GetPortInputValue ('D');
	uint8	portDKbdIntEn	= READ_REGISTER (portDKbdIntEn);

	if (!gSession->GetDevice ().EdgeHack ())
	{
		uint8	KB = portDData & ~portDDir & portDKbdIntEn;

		if (KB)
			intPendingLo |= hwrVZ328IntLoKbd;
		else
			intPendingLo &= ~hwrVZ328IntLoKbd;
	}

	WRITE_REGISTER (intPendingLo, intPendingLo);

	this->UpdateInterrupts ();
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::UpdateSPISlaveSTMicro
// ---------------------------------------------------------------------------
// This was abstracted out from two functions that copy-pasted this code.

void EmRegsVZAlphaSmart::UpdateSPISlaveSTMicro (void)
{
	uint16	intPendingHi 	= READ_REGISTER (intPendingHi);

	uint8	newData			= fSPISlaveSTMicro->NeedsExchange ();

	if ((intPendingHi & hwrVZ328IntHiIRQ1) != newData)
	{
		if (newData)
			WRITE_REGISTER (intPendingHi, intPendingHi | hwrVZ328IntHiIRQ1);
		else
			WRITE_REGISTER (intPendingHi, intPendingHi & ~hwrVZ328IntHiIRQ1);

		this->UpdateInterrupts ();
	}
}

#if 0
// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::Unknown
// ---------------------------------------------------------------------------

Bool EmRegsVZAlphaSmart::Unknown (void)
{
	return (READ_REGISTER (uControl) & hwrVZ328UControlUARTEnable) != 0;
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::Unknown
// ---------------------------------------------------------------------------

Bool EmRegsVZAlphaSmart::Unknown (void)
{
	uint16	uControl = READ_REGISTER (uControl);
	uint16	uMisc = READ_REGISTER (uMisc);

	return (uControl & hwrVZ328UControlUARTEnable) != 0 &&
			(uMisc & hwrVZ328UMiscIRDAEn) != 0;
}
#endif

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::spiMasterControlWrite
// ---------------------------------------------------------------------------
// Most of this is a copy from EmRegsVZ.

void EmRegsVZAlphaSmart::spiMasterControlWrite (emuptr address, int size, uint32 value)
{
	EmRegs::StdWrite (address, size, value);

	// Get the current value.

	uint16	spiMasterData		= READ_REGISTER (spiMasterData);
	uint16	spiMasterControl	= READ_REGISTER (spiMasterControl);

	// Check to see if data exchange and enable are enabled.

	#define BIT_MASK (hwrVZ328SPIMControlExchange | hwrVZ328SPIMControlEnable)
	if ((spiMasterControl & BIT_MASK) == BIT_MASK)
	{
		// If the SPI is hooked up to something, talk with it.

		EmSPISlave*	spiSlave = this->GetSPISlave ();
		if (spiSlave)
		{
			// Write out the old data, read in the new data.

			uint16	newData = spiSlave->DoExchange (spiMasterControl, spiMasterData);

			// Shift in the new data.

			uint16	numBits = (spiMasterControl & hwrVZ328SPIMControlBitsMask) + 1;

			uint16	oldBitsMask = ~0 << numBits;
			uint16	newBitsMask = ~oldBitsMask;

			spiMasterData = /*((spiMasterData << numBits) & oldBitsMask) | */
				(newData & newBitsMask);

			WRITE_REGISTER (spiMasterData, spiMasterData);
		}

		// Assert the interrupt and clear the exchange bit.

		spiMasterControl |= hwrVZ328SPIMControlIntStatus;
		spiMasterControl &= ~hwrVZ328SPIMControlExchange;

		WRITE_REGISTER (spiMasterControl, spiMasterControl);

		// If hwrVZ328SPIMControlIntEnable is set, trigger an interrupt.

		if ((spiMasterControl & hwrVZ328SPIMControlIntEnable) != 0)
		{
			uint16	intPendingLo	= READ_REGISTER (intPendingLo);
			intPendingLo |= hwrVZ328IntLoSPIM;
			WRITE_REGISTER (intPendingLo, intPendingLo);
			this->UpdateInterrupts ();
		}

		UpdateSPISlaveSTMicro ();
	}
}

// ---------------------------------------------------------------------------
//		• EmRegsVZAlphaSmart::portKDirWrite
// ---------------------------------------------------------------------------

void EmRegsVZAlphaSmart::portKDirWrite (emuptr address, int size, uint32 value)
{
	EmRegs::StdWrite (address, size, value);

	if ((value & hwrAlphaSmartPortKKeyboardOn) == 0)
		return;

	uint8 portKData = READ_REGISTER (portKData);
	if ((portKData & hwrAlphaSmartPortKKeyboardOn) == 0)
	{
		WRITE_REGISTER (portKData, portKData | hwrAlphaSmartPortKKeyboardOn);
		fSPISlaveSTMicro->PortKOutputEnable ();
	}
}
