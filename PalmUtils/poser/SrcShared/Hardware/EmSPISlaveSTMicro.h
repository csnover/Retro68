/* -*- mode: C++; tab-width: 4 -*- */
/* ===================================================================== *\
	Copyright (c) Retro68 contributors.

	This file is part of the Palm OS Emulator.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
\* ===================================================================== */

#ifndef EmSPISlaveSTMicro_h
#define EmSPISlaveSTMicro_h

#include "EmCommon.h"
#include "EmSPISlave.h"			// EmSPISlave

class EmSPISlaveSTMicro : public EmSPISlave
{
	public:
		enum { kNumRows = 8, kNumCols = 16 };

								EmSPISlaveSTMicro		(void);
		virtual					~EmSPISlaveSTMicro		(void);

		virtual uint16			DoExchange				(uint16 control, uint16 data);

		uint8					NeedsExchange			(void);
		void					PortKOutputEnable		(void);

	protected:
		void					ProcessEventCommand		(void);
		void					ButtonEvent				(void);
		void					KeyEvent				(void);

		uint8					ProcessVersionCommand	(uint8);

	private:
		enum KeyEventState { Ready, Busy, Reset };

		// In the queue of pressed key codes, the low nibble is the byte index
		// of the key in the key maps, and the high nibble is the bit index.

		uint16					fQueueIn;  //< count of keys in the queue
		uint16					fQueueOut; //< count of keys read from the queue
		uint8					fQueue[20];

		bool					fPendingExchange;
		uint8					fNewKeyMap[kNumCols];
		uint8					fOldKeyMap[kNumCols];

		bool					fStickyShift; //< if true, shift key on skin was clicked

		uint8					fGetVerIn;  //< count of incoming version query bytes
		uint8					fGetVerOut; //< count of outgoing version query bytes
		bool					fInGetVer;  //< if true, sending/receiving version

		bool					fOutputEnabled;
		KeyEventState			fEventState;
};

#endif	// EmSPISlaveSTMicro_h
