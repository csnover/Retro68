/* -*- mode: C++; tab-width: 4 -*- */
/* ===================================================================== *\
	Copyright (c) Retro68 contributors.

	This file is part of the Palm OS Emulator.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
\* ===================================================================== */

#include "EmRegsUSBISP_1161.h"

// Given a register (specified by its field name), return its address
// in emulated space.

#undef addressof
#define addressof(reg)				\
	(this->GetAddressStart () + fRegs.offsetof_##reg ())


// Macro to help the installation of handlers for a register.

#undef INSTALL_HANDLER
#define INSTALL_HANDLER(read, write, reg)			\
	this->SetHandler (	(ReadFunction) &EmRegsUSBISP_1161::read,		\
						(WriteFunction) &EmRegsUSBISP_1161::write,		\
						addressof (reg),			\
						fRegs.reg.GetSize ())

// ---------------------------------------------------------------------------
//		¥ EmRegsUSBISP_1161::EmRegsUSBISP_1161
// ---------------------------------------------------------------------------

EmRegsUSBISP_1161::EmRegsUSBISP_1161 (emuptr baseAddr)
	: EmRegs ()
	, fUseBank1 ()
	, fCommand ()
	, fBank0 ()
	, fBank1 ()
	, fReadOnlyRegs ()
	, fBaseAddr (baseAddr)
	, fRegs ()
{
	fReadOnlyRegs[HcChipID] = 1;
	fReadOnlyRegs[HcITLBufferPort] = 1;
	fReadOnlyRegs[HcATLBufferPort] = 1;
	fBank0[HcChipID] = ProductName | ProductRevision;
}

// ---------------------------------------------------------------------------
//		¥ EmRegsUSBISP_1161::~EmRegsUSBISP_1161
// ---------------------------------------------------------------------------

EmRegsUSBISP_1161::~EmRegsUSBISP_1161 ()
{
}

// ---------------------------------------------------------------------------
//		¥ EmRegsUSBISP_1161::Reset
// ---------------------------------------------------------------------------

void EmRegsUSBISP_1161::Reset (Bool hardwareReset)
{
	EmRegs::Reset (hardwareReset);

	if (hardwareReset)
		memset (fRegs.GetPtr (), 0, fRegs.GetSize ());
}

// ---------------------------------------------------------------------------
//		¥ EmRegsUSBISP_1161::SetSubBankHandlers
// ---------------------------------------------------------------------------

void EmRegsUSBISP_1161::SetSubBankHandlers ()
{
	EmRegs::SetSubBankHandlers ();

	INSTALL_HANDLER (hostDataRead, hostDataWrite,    hostData);
	INSTALL_HANDLER (StdReadBE,    hostCommandWrite, hostCommand);
	INSTALL_HANDLER (StdReadBE,    StdWriteBE,       deviceData);
	INSTALL_HANDLER (StdReadBE,    StdWriteBE,       deviceCommand);
}

// ---------------------------------------------------------------------------
//		¥ EmRegsUSBISP_1161::GetRealAddress
// ---------------------------------------------------------------------------

uint8* EmRegsUSBISP_1161::GetRealAddress (emuptr address)
{
	return (uint8*) fRegs.GetPtr () + address - this->GetAddressStart ();
}

// ---------------------------------------------------------------------------
//		¥ EmRegsUSBISP_1161::GetAddressStart
// ---------------------------------------------------------------------------

emuptr EmRegsUSBISP_1161::GetAddressStart (void)
{
	return fBaseAddr;
}

// ---------------------------------------------------------------------------
//		¥ EmRegsUSBISP_1161::GetAddressRange
// ---------------------------------------------------------------------------

uint32 EmRegsUSBISP_1161::GetAddressRange (void)
{
	return fRegs.GetSize ();
}

// ---------------------------------------------------------------------------
//		¥ EmRegsUSBISP_1161::hostDataRead
// ---------------------------------------------------------------------------

uint32 EmRegsUSBISP_1161::hostDataRead (emuptr address, int size)
{
	uint8*	realAddr = this->GetRealAddress (address);

	if (size == 1)
		return *realAddr;

	if (size == 2)
	{
		if (fUseBank1)
			return fBank1[fCommand];
		else
		{
			fUseBank1 = 1;
			return fBank0[fCommand];
		}
	}

	return	(realAddr[0] << 24) |
			(realAddr[1] << 16) |
			(realAddr[2] <<  8) |
			(realAddr[3] <<  0);
}

// ---------------------------------------------------------------------------
//		¥ EmRegsUSBISP_1161::hostDataWrite
// ---------------------------------------------------------------------------

void EmRegsUSBISP_1161::hostDataWrite (emuptr address, int size, uint32 value)
{
	if (size == 2 && !fReadOnlyRegs[fCommand])
	{
		if (fUseBank1)
			fBank1[fCommand] = value;
		else
			fBank0[fCommand] = value;
	}
}

// ---------------------------------------------------------------------------
//		¥ EmRegsUSBISP_1161::hostCommandWrite
// ---------------------------------------------------------------------------

void EmRegsUSBISP_1161::hostCommandWrite (emuptr address, int size, uint32 value)
{
	if (size == 2)
	{
		if ((value & 0x7f) < 0x46)
		{
			fUseBank1 = 0;
			fCommand = (value & 0x7f);
		}
		else
			fUseBank1 = fCommand = 0;
	}
}
