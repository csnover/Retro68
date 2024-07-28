/* -*- mode: C++; tab-width: 4 -*- */
/* ===================================================================== *\
	Copyright (c) Retro68 contributors.

	This file is part of the Palm OS Emulator.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
\* ===================================================================== */

#include "EmSPISlaveSTMicro.h"
#include "EmSession.h"
#include "Chars.h"

// ---------------------------------------------------------------------------
//		¥ EmSPISlaveSTMicro::EmSPISlaveSTMicro
// ---------------------------------------------------------------------------

EmSPISlaveSTMicro::EmSPISlaveSTMicro (void)
	: fQueueIn ()
	, fQueueOut ()
	, fQueue ()
	, fPendingExchange ()
	, fNewKeyMap ()
	, fOldKeyMap ()
	, fStickyShift ()
	, fGetVerIn ()
	, fGetVerOut ()
	, fInGetVer ()
	, fOutputEnabled ()
	, fEventState (Ready)
{
	gSession->fHasSTMicroSlave = true;
}

// ---------------------------------------------------------------------------
//		¥ EmSPISlaveSTMicro::~EmSPISlaveSTMicro
// ---------------------------------------------------------------------------

EmSPISlaveSTMicro::~EmSPISlaveSTMicro (void)
{
	gSession->fHasSTMicroSlave = false;
}

// ---------------------------------------------------------------------------
//		¥ EmSPISlaveSTMicro::DoExchange
// ---------------------------------------------------------------------------

uint16 EmSPISlaveSTMicro::DoExchange (uint16, uint16 data)
{
	uint16 out;
	if (fOutputEnabled)
	{
		fQueueIn = fQueueOut = 0;
		fOutputEnabled = false;
		out = 0xaa;
	}
	else if (data || fInGetVer)
	{
		out = ProcessVersionCommand (data);
		fQueueOut = fQueueIn = 0;
	}
	else
	{
		if (fQueueIn)
		{
			out = fQueue[fQueueOut++];
			if (fQueueOut == fQueueIn)
				fQueueOut = fQueueIn = 0;
		}
		else
		{
			if (fEventState == Busy)
				fEventState = Reset;
			else if (fEventState == Reset)
				fEventState = Ready;

			ProcessEventCommand ();
			fQueueOut = 0;

			out = fQueueIn ? (fQueueIn | 0x40) : 0;
			fPendingExchange = false;
			if (fEventState == Reset)
				fEventState = Ready;
		}
	}

	return out;
}

// ---------------------------------------------------------------------------
//		¥ EmSPISlaveSTMicro::NeedsExchange
// ---------------------------------------------------------------------------

uint8 EmSPISlaveSTMicro::NeedsExchange (void)
{
	if (fPendingExchange)
		return hwrVZ328IntHiIRQ1;

	bool hasKeyEvent;
	if (gSession->HasKeyEvent ())
	{
		EmKeyEvent event = gSession->PeekKeyEvent ();
		hasKeyEvent = !event.fControlDown;
	}
	else
		hasKeyEvent = false;

	fPendingExchange = (gSession->HasButtonEvent () || hasKeyEvent);
	return fPendingExchange ? hwrVZ328IntHiIRQ1 : 0;
}

// ---------------------------------------------------------------------------
//		¥ EmSPISlaveSTMicro::PortKOutputEnable
// ---------------------------------------------------------------------------

void EmSPISlaveSTMicro::PortKOutputEnable (void)
{
	fOutputEnabled = true;
}

// ---------------------------------------------------------------------------
//		¥ EmSPISlaveSTMicro::ProcessEventCommand
// ---------------------------------------------------------------------------

void EmSPISlaveSTMicro::ProcessEventCommand (void)
{
	if (gSession->HasButtonEvent ())
		ButtonEvent ();

	if (gSession->HasKeyEvent ())
	{
		EmKeyEvent event = gSession->PeekKeyEvent ();
		if (!event.fControlDown)
			KeyEvent ();
	}

	for (uint8 col = 0; col < kNumCols; ++col)
	{
		uint8 row = fNewKeyMap[col];
		for (uint8 bit = 0; row != 0; ++bit)
		{
			uint8 mask = 1 << bit;
			if ((row & mask) != 0)
			{
				fQueue[fQueueIn++] = bit << 4 | col;
				if ((fOldKeyMap[col] & mask) == 0)
					fNewKeyMap[col] &= ~mask;
			}
			row &= ~mask;
		}
	}
}

#define DO_BIT_OP(col, op) do {		\
	fNewKeyMap[(col)] op; 			\
	fOldKeyMap[(col)] op;			\
} while (0)

#define TOGGLE_KEY(col, mask) do {		\
	if ((fNewKeyMap[col] & mask) == 0)	\
		DO_BIT_OP (col, |= mask);		\
	else								\
		DO_BIT_OP (col, &= ~mask);		\
} while (0)

// ---------------------------------------------------------------------------
//		¥ EmSPISlaveSTMicro::ButtonEvent
// ---------------------------------------------------------------------------

void EmSPISlaveSTMicro::ButtonEvent (void)
{
	EmButtonEvent event = gSession->GetButtonEvent ();
	uint8 code;
	switch (event.fButton)
	{
		case kElement_PowerButton: code = 0x0f; break;
		case kElement_UpButton:    code = 0x70; break;
		case kElement_DownButton:  code = 0x12; break;
		case kElement_App1Button:
		case kElement_F9Key:       code = 0x4e; break;
		case kElement_App2Button:
		case kElement_F10Key:      code = 0x5e; break;
		case kElement_App3Button:
		case kElement_F11Key:      code = 0x60; break;
		case kElement_App4Button:
		case kElement_F12Key:      code = 0x53; break;
		case kElement_F1Key:       code = 0x4c; break;
		case kElement_F2Key:       code = 0x4d; break;
		case kElement_F3Key:       code = 0x0d; break;
		case kElement_F4Key:       code = 0x1d; break;
		case kElement_F5Key:       code = 0x1e; break;
		case kElement_F6Key:       code = 0x17; break;
		case kElement_F7Key:       code = 0x05; break;
		case kElement_F8Key:       code = 0x45; break;
		case kElement_F13Key:      code = 0x33; break;
		case kElement_F14Key:      code = 0x62; break;
		case kElement_F15Key:      code = 0x41; break;
		case kElement_F16Key:      code = 0x40; break;
		case kElement_PcDeleteKey: code = 0x66; break;
		case kElement_CapsLockKey: code = 0x0c; break;
		case kElement_EscapeKey:   code = 0x73; break;
		case kElement_None:        code = 0xff; break;
		case kElement_ControlKey:
			if (event.fButtonIsDown)
				TOGGLE_KEY (0x0b, 0x80);
			return;
		case kElement_AltLeftKey:
			if (event.fButtonIsDown)
				TOGGLE_KEY (0x06, 0x10);
			return;
		case kElement_CommandKey:
			if (event.fButtonIsDown)
				TOGGLE_KEY (0x03, 0x02);
			return;
		case kElement_ShiftLeftKey:
		case kElement_ShiftRightKey:
			if (event.fButtonIsDown)
			{
				uint8 mask = (event.fButton == kElement_ShiftLeftKey) ? 0x01 : 0x40;
				if ((fNewKeyMap[0x09] & mask) == 0)
				{
					DO_BIT_OP (0x09, |= mask);
					fStickyShift = true;
				}
				else
				{
					DO_BIT_OP (0x09, &= ~mask);
					fStickyShift = false;
				}
			}
			return;
		case kElement_ClearModifiersKey:
			if (event.fButtonIsDown)
			{
				DO_BIT_OP (0x0b, &= ~0x80); /* control */
				DO_BIT_OP (0x06, &= ~0x10); /* alt */
				DO_BIT_OP (0x09, &= ~0x41); /* left & right shift */
				DO_BIT_OP (0x03, &= ~0x02); /* command */
			}
			return;
		default:
			code = 0;
			break;
	}

	if (event.fButtonIsDown)
	{
		DO_BIT_OP (code & 0xf, |= (1 << (code >> 4)));
		if (code == /* down */ 0x12 || code == /* up */ 0x70)
			DO_BIT_OP (6, |= 0x10);
	}
	else
	{
		DO_BIT_OP (code & 0xf, &= ~(1 << (code >> 4)));
		if (code == /* down */ 0x12 || code == /* up */ 0x70)
			DO_BIT_OP (6, &= ~0x10);
	}
}

// ---------------------------------------------------------------------------
//		¥ EmSPISlaveSTMicro::KeyEvent
// ---------------------------------------------------------------------------

void EmSPISlaveSTMicro::KeyEvent (void)
{
	if (fEventState != Ready)
		return;

	uint8 code;
	EmKeyEvent event = gSession->PeekKeyEvent ();
	switch (event.fKey)
	{
		// Escape key and CR were missing in original implementation, possibly
		// because the Windows backend was written to send CR as LF and ESC as
		// an kElement_EscapeKey button?
		case chrEscape:               code = 0x73; break;
		case chrBackspace:            code = 0x0e; break;
		case chrHorizontalTabulation: code = 0x0b; break;
		case chrCarriageReturn:
		case chrLineFeed:             code = 0x6e; break;
		case chrFileSeparator:        code = 0x72; break;
		case chrGroupSeparator:       code = 0x71; break;
		case chrRecordSeparator:      code = 0x70; break;
		case chrUnitSeparator:        code = 0x12; break;
		case chrSpace:                code = 0x7e; break;
		case chrExclamationMark:
		case chrDigitOne:             code = 0x5b; break;
		case chrQuotationMark:
		case chrApostrophe:           code = 0x14; break;
		case chrNumberSign:
		case chrDigitThree:           code = 0x5d; break;
		case chrDollarSign:
		case chrDigitFour:            code = 0x5a; break;
		case chrPercentSign:
		case chrDigitFive:            code = 0x4a; break;
		case chrAmpersand:
		case chrDigitSeven:           code = 0x58; break;
		case chrLeftParenthesis:
		case chrDigitNine:            code = 0x55; break;
		case chrRightParenthesis:
		case chrDigitZero:            code = 0x54; break;
		case chrAsterisk:
		case chrDigitEight:           code = 0x57; break;
		case chrPlusSign:
		case chrEqualsSign:           code = 0x47; break;
		case chrComma:
		case chrLessThanSign:         code = 0x67; break;
		case chrHyphenMinus:
		case chrLowLine:              code = 0x44; break;
		case chrFullStop:
		case chrGreaterThanSign:      code = 0x65; break;
		case chrSolidus:
		case chrQuestionMark:         code = 0x74; break;
		case chrDigitTwo:
		case chrCommercialAt:         code = 0x5c; break;
		case chrDigitSix:
		case chrCircumflexAccent:     code = 0x48; break;
		case chrColon:
		case chrSemicolon:            code = 0x24; break;
		case chrCapital_A:
		case chrSmall_A:              code = 0x2b; break;
		case chrCapital_B:
		case chrSmall_B:              code = 0x7a; break;
		case chrCapital_C:
		case chrSmall_C:              code = 0x6d; break;
		case chrCapital_D:
		case chrSmall_D:              code = 0x2d; break;
		case chrCapital_E:
		case chrSmall_E:              code = 0x3d; break;
		case chrCapital_F:
		case chrSmall_F:              code = 0x2a; break;
		case chrCapital_G:
		case chrSmall_G:              code = 0x1a; break;
		case chrCapital_H:
		case chrSmall_H:              code = 0x18; break;
		case chrCapital_I:
		case chrSmall_I:              code = 0x37; break;
		case chrCapital_J:
		case chrSmall_J:              code = 0x28; break;
		case chrCapital_K:
		case chrSmall_K:              code = 0x27; break;
		case chrCapital_L:
		case chrSmall_L:              code = 0x25; break;
		case chrCapital_M:
		case chrSmall_M:              code = 0x68; break;
		case chrCapital_N:
		case chrSmall_N:              code = 0x78; break;
		case chrCapital_O:
		case chrSmall_O:              code = 0x35; break;
		case chrCapital_P:
		case chrSmall_P:              code = 0x34; break;
		case chrCapital_Q:
		case chrSmall_Q:              code = 0x3b; break;
		case chrCapital_R:
		case chrSmall_R:              code = 0x3a; break;
		case chrCapital_S:
		case chrSmall_S:              code = 0x2c; break;
		case chrCapital_T:
		case chrSmall_T:              code = 0x0a; break;
		case chrCapital_U:
		case chrSmall_U:              code = 0x38; break;
		case chrCapital_V:
		case chrSmall_V:              code = 0x6a; break;
		case chrCapital_W:
		case chrSmall_W:              code = 0x3c; break;
		case chrCapital_X:
		case chrSmall_X:              code = 0x6c; break;
		case chrCapital_Y:
		case chrSmall_Y:              code = 0x08; break;
		case chrCapital_Z:
		case chrSmall_Z:              code = 0x6b; break;
		case chrLeftSquareBracket:
		case chrLeftCurlyBracket:     code = 0x04; break;
		case 0x5c /* chrReverseSolidus */:
		case chrVerticalLine:         code = 0x2e; break;
		case chrRightSquareBracket:
		case chrRightCurlyBracket:    code = 0x07; break;
		case chrGraveAccent:
		case chrTilde:                code = 0x4b; break;
		// The original code returned early in the default case, but doing
		// that causes the IRQ handler to get stuck in an infinite loop.
		default:                      code = 0x00; break;
	}

	uint8 col = code & 0xf;
	uint8 mask = (1 << (code >> 4));

	if ((fNewKeyMap[col] & mask) != 0)
		return;

	event = gSession->GetKeyEvent ();
	fNewKeyMap[col] |= mask;
	fOldKeyMap[col] &= ~mask;

	if (!fStickyShift)
	{
		// DO_BIT_OP sets both sets of bitmaps; this only sets one and always
		// clears the other one
		if (event.fShiftDown)
			fNewKeyMap[0x09] |= 0x01;
		else
			fNewKeyMap[0x09] &= ~0x01;

		fOldKeyMap[0x09] &= ~0x01;
	}

	fEventState = Busy;
}

#undef DO_BIT_OP

// ---------------------------------------------------------------------------
//		¥ EmSPISlaveSTMicro::ProcessVersionCommand
// ---------------------------------------------------------------------------

uint8 EmSPISlaveSTMicro::ProcessVersionCommand (uint8 command)
{
	uint8 out = 0;
	if (fInGetVer)
	{
		static const char VERSION[] = "06.8";
		out = VERSION[fGetVerOut++];
		if (fGetVerOut == sizeof (VERSION) - 1)
		{
			fGetVerIn = fGetVerOut = 0;
			fInGetVer = false;
		}
	}
	else
	{
		static const char COMMAND[] = "Get Ver!";
		if (COMMAND[fGetVerIn] == command)
		{
			if ((++fGetVerIn) == sizeof (COMMAND) - 1)
			{
				fInGetVer = true;
				fGetVerOut = 0;
			}
		}
		else
			fGetVerIn = 0;
	}
	return out;
}
