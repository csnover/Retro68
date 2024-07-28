/* -*- mode: C++; tab-width: 4 -*- */
/* ===================================================================== *\
	Copyright (c) Retro68 contributors.

	This file is part of the Palm OS Emulator.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
\* ===================================================================== */

#ifndef EmRegsVZAlphaSmart_h
#define EmRegsVZAlphaSmart_h

#include "EmRegsVZ.h"

class EmSPISlaveSTMicro;

class EmRegsVZAlphaSmart : public EmRegsVZ
{
	public:
								EmRegsVZAlphaSmart		(void);
		virtual					~EmRegsVZAlphaSmart		(void);

		virtual void			SetSubBankHandlers		(void);

		virtual void			CycleSlowly				(Bool sleeping);

		virtual Bool			GetLCDScreenOn			(void);
		virtual Bool			GetLCDBacklightOn		(void);

		virtual uint8			GetPortInputValue		(int);
		virtual uint8			GetPortInternalValue	(int);
		virtual void			GetKeyInfo				(int* numRows, int* numCols,
														 uint16* keyMap, Bool* rows);

	protected:
		virtual uint8			GetKeyBits				(void);
		virtual EmSPISlave*		GetSPISlave				(void);
		virtual void			UpdatePortDInterrupts	(void);

	private:
		void					UpdateSPISlaveSTMicro	(void);

		// There were two more virtual methods here that seem to not ever be
		// called and do not exist in the the superclass. One returns whether
		// UART is enabled. The other looks like a replacement for
		// GetLineDriverState and returns whether IRDA mode is enabled.
		// Breakpoints in the original binary emulator are never triggered
		// during normal operation.

		// Was virtual in original code
		/* virtual */ void		spiMasterControlWrite	(emuptr address, int size,
														 uint32 value);
		void					portKDirWrite			(emuptr address, int size,
														 uint32 value);

		EmSPISlave*				fSPISlaveADC;
		EmSPISlaveSTMicro*		fSPISlaveSTMicro;
};

#endif	/* EmRegsVZAlphaSmart_h */
