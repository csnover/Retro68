/* -*- mode: C++; tab-width: 4 -*- */
/* ===================================================================== *\
	Copyright (c) Retro68 contributors.

	This file is part of the Palm OS Emulator.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
\* ===================================================================== */

#ifndef EmRegsUSBISP_1161_h
#define EmRegsUSBISP_1161_h

#include "EmCommon.h"
#include "EmPalmStructs.h"
#include "EmRegs.h"

class EmRegsUSBISP_1161 : public EmRegs
{
	public:
								EmRegsUSBISP_1161	(emuptr baseAddr);
		virtual					~EmRegsUSBISP_1161	(void);

		virtual void			Reset				(Bool hardwareReset);

		virtual void			SetSubBankHandlers	(void);
		virtual uint8*			GetRealAddress		(emuptr address);
		virtual emuptr			GetAddressStart		(void);
		virtual uint32			GetAddressRange		(void);

	private:
		enum HcControl {
			HcChipID = 0x27,
			HcITLBufferPort = 0x40,
			HcATLBufferPort = 0x41,

			ProductName = 0x6100,
			ProductRevision = 0x22
		};

		uint32					hostDataRead		(emuptr address, int size);
		void					hostDataWrite		(emuptr address, int size, uint32 value);
		void					hostCommandWrite	(emuptr address, int size, uint32 value);

		uint16					fUseBank1;
		uint16					fCommand;
		uint16					fBank0[70];
		uint16					fBank1[70];
		uint16					fReadOnlyRegs[70];
		emuptr					fBaseAddr;
		EmProxyUsbHwrISP1161Type	fRegs;
};

#endif	/* EmRegsUSBISP_1161_h */
