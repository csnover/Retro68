/* -*- mode: C++; tab-width: 4 -*- */
/* ===================================================================== *\
	Copyright (c) 1998-2001 Palm, Inc. or its subsidiaries.
	All rights reserved.

	This file is part of the Palm OS Emulator.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
\* ===================================================================== */

#include "EmCommon.h"
#include "EmMemory.h"

#include <stdio.h>		// needed for sprintf.
#include <stdlib.h>		// needed for rand and srand
#include <string.h>		// needed for strcpy and friends


#ifdef forSimulator

#define PILOT_PRECOMPILED_HEADERS_OFF

// Palm Includes 
#include <BuildDefines.h>
#ifdef HAS_LOCAL_BUILD_DEFAULTS
#include "LocalBuildDefaults.h"
#endif
#include <PalmTypes.h>

#include <Chars.h>
#include <DebugMgr.h>
#include <ErrorBase.h>
#include <FeatureMgr.h>
#include <Field.h>
#include <Form.h>
#include <TextMgr.h>
#include <PalmLocale.h>

#include "CGremlinsStubs.h"
#include "CGremlins.h"
#include "Hardware.h"
#include <EmuStubs.h>

#define	NON_PORTABLE
#include "SystemPrv.h"
#include "DataPrv.h"
#include "SysEvtPrv.h"
#include <SystemPkt.h>

#include "ShellCmd.h"

#else	// !forSimulator

#include "EmBankRegs.h"			// RegsBank
#include "EmEventPlayback.h"	// RecordPenEvent, etc.
#include "EmMemory.h"			// EmMemPut16, EmMemPut32
#include "EmPalmStructs.h"		// EmAliasPenBtnInfoType
#include "EmPatchState.h"		// GetCurrentAppInfo
#include "EmSession.h"			// gSession, ScheduleAutoSaveState
#include "ErrorHandling.h"		// Errors::ThrowIfPalmError
#include "Hordes.h"				// Hordes::IsOn, TurnOn
#include "Logging.h"
#include "PreferenceMgr.h"		// Preference<GremlinInfo>
#include "ROMStubs.h"			// FtrGet, TxtGetNextChar, TxtCharBounds, TxtByteAddr, FrmGetActiveForm...
#include "SessionFile.h"		// SessionFile
#include "Strings.r.h"			// kStr_ values
#include "EmLowMem.h"			// EmLowMem_SetGlobal for setting battery level



///////////////////////////////////////////////////////////////////////////////////
// Private function declarations
// (actually just some operator override declarations we'll need)

static EmStream&	operator >> (EmStream&, DatabaseInfo&);
static EmStream&	operator << (EmStream&, const DatabaseInfo&);

static EmStream&	operator >> (EmStream&, GremlinInfo&);
static EmStream&	operator << (EmStream&, const GremlinInfo&);


///////////////////////////////////////////////////////////////////////////////////
// Private globals

static int	gIntlMgrExists = -1;





///////////////////////////////////////////////////////////////////////////////////
// Private functions

static Bool IntlMgrExists (void)
{
	if (gIntlMgrExists < 0)
	{
		// Note that we need to check by calling the feature manager rather than
		// checking to see if the trap is implemented. sysTrapIntlDispatch is
		// sysTrapPsrInit on 1.0 systems and sysUnused2 on intermediate systems.
		// That means that the trap IS implemented, but just not the one we want.

		UInt32	data;
		Err		err = FtrGet (sysFtrCreator, sysFtrNumIntlMgr, &data);

		gIntlMgrExists = !err && (data & intlMgrExists) != 0;
	}

	return gIntlMgrExists != 0;
}

static UInt16 _TxtGetNextChar (const Char *inText, UInt32 inOffset, WChar *outChar)
{
	if (IntlMgrExists ())
	{
		return TxtGetNextChar (inText, inOffset, outChar);
	}

	if (outChar)
		*outChar = (UInt8) inText[inOffset];

	return sizeof (Char);
}

static WChar _TxtCharBounds (const Char *inText, UInt32 inOffset, UInt32 *outStart, UInt32 *outEnd)
{
	if (IntlMgrExists ())
	{
		return TxtCharBounds (inText, inOffset, outStart, outEnd);
	}

	if (outStart)
		*outStart = inOffset;

	if (outEnd)
		*outEnd = inOffset + 1;

	return inText[inOffset];
}

static UInt8 _TxtByteAttr (UInt8 inByte)
{
	if (IntlMgrExists ())
	{
		return TxtByteAttr (inByte);
	}

	return byteAttrSingle;
}

#define TxtGetNextChar	_TxtGetNextChar
#define TxtCharBounds	_TxtCharBounds
#define TxtByteAttr		_TxtByteAttr

#include "CGremlins.h"
#include "CGremlinsStubs.h"

#define PRINTF	if (!LogGremlins ()) ; else LogAppendMsg


// Use our own versions of rand() and srand() so that we generate the
// same numbers on both platforms.

#undef RAND_MAX
#define RAND_MAX 0x7fff

#define rand	Gremlin_rand
#define srand	Gremlin_srand

unsigned int gGremlinNext = 1;

static int rand(void)
{
//	gGremlinNext = gGremlinNext * 1103515245 + 12345;	// MSL numbers

	gGremlinNext = gGremlinNext * 214013 + 2531011;	// VC++ numbers
	PRINTF ("--- gGremlinNext == 0x%08X", gGremlinNext);

	return ((gGremlinNext >> 16) & 0x7FFF);
}

static void srand(unsigned int seed)
{
	gGremlinNext = seed;
}


#endif


//#define randN(N) ((N) ? rand() / (RAND_MAX / (N)) : (0))
#define randN(N) ((int) (((int) rand() * (N)) / ((int) RAND_MAX + 1)))
#define randPercent (randN(100))

#ifndef forSimulator
#undef randN
inline int randN (int N)
{
	int	result = ((int) (((int) rand() * (N)) / ((int) RAND_MAX + 1)));
	PRINTF ("--- randN(%ld) == 0x%08X", N, (int) result);
	return result;
}
#endif

#define PEN_MOVE_CHANCE							50			// 50% move pen else pen up
#define PEN_BIG_MOVE_CHANCE						5			// 5% move pen really far

#define KEY_DOWN_EVENT_WITHOUT_FOCUS_CHANCE		10
#define KEY_DOWN_EVENT_WITH_FOCUS_CHANCE		40
#define PEN_DOWN_EVENT_CHANCE					(70 + KEY_DOWN_EVENT_WITHOUT_FOCUS_CHANCE)
#define MENU_EVENT_CHANCE						(PEN_DOWN_EVENT_CHANCE + 4)
#define FIND_EVENT_CHANCE						(MENU_EVENT_CHANCE + 2)
#define KEYBOARD_EVENT_CHANCE					(FIND_EVENT_CHANCE + 1)
#define LOW_BATTERY_EVENT_CHANCE				(KEYBOARD_EVENT_CHANCE + 2)
#define APP_SWITCH_EVENT_CHANCE					(LOW_BATTERY_EVENT_CHANCE + 4)
// #define POWER_OFF_CHANCE						(APP_SWITCH_EVENT_CHANCE + 1)

#define LAUNCHER_EVENT_CHANCE					0	// percent of APP_SWITCH_EVENT_CHANCE


#define commandKeyMask							0x0008


#define TYPE_QUOTE_CHANCE						10

#define MAX_SEED_VALUE							1000	// Max. # of seed values allowed.
#define INITIAL_SEED							1

#define LETTER_PROB								60

// Chars less often than a letter
#define SYMBOL_PROB								(LETTER_PROB / 10)
#define EXT_LTTR_PROB							(LETTER_PROB / 3)
#define EXTENDED_PROB							(LETTER_PROB / 5)
#define CONTROL_PROB							(LETTER_PROB / 2)
#define MENU_PROB								(LETTER_PROB / 10)
#define KBRD_PROB								1 	// The formula results in 0 
	//												((LETTER_PROB / 30) / 3)	// three chars to activate keyboard
#define NXTFLD_PROB								(LETTER_PROB / 10)
#define SEND_DATA_PROB							(LETTER_PROB / 60)

// Chars more often than a letter
#define SPACE_PROB								(LETTER_PROB * 5)
#define TAB_PROB								(LETTER_PROB * 2)
#define BACKSPACE_PROB							(LETTER_PROB * 3) 
#define RETURN_PROB								((LETTER_PROB * 10) * 1)	// extra exercise


//Global variables
Gremlins*	TheGremlinsP;					// Pointer to the Gremlins class.
int32		IdleTimeCheck;					// Tick count for the next idle query

// Array of probabilities of a key being pressed for gremlin mode.
#define NUM_OF_KEYS		0x110
static const int chanceForKey[NUM_OF_KEYS] = {
	0, 0, 0, 0, 0, 0, 0, 0,														// 0x00 - 0x07
	BACKSPACE_PROB, TAB_PROB, RETURN_PROB, CONTROL_PROB, CONTROL_PROB, 0, 0, 0,	// 0x08 - 0x0F
	0, 0, 0, 0, 0, 0, 0, 0,														// 0x10 - 0x17
	0, 0, 0, 0, CONTROL_PROB, CONTROL_PROB, CONTROL_PROB, CONTROL_PROB,			// 0x18 - 0x1F

	// Symbols
	SPACE_PROB, SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB,	// 0x20 - 0x23
	SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB,	// 0x24 - 0x27
	SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB,	// 0x28 - 0x2B
	SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB,	// 0x2C - 0x2F
	SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB,	// 0x30 - 0x33
	SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB,	// 0x34 - 0x37
	SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB,	// 0x38 - 0x3B
	SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB, SYMBOL_PROB,	// 0x3C - 0x3F

	// Uppercase
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x40 - 0x43
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x44 - 0x47
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x48 - 0x4B
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x4C - 0x4F
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x50 - 0x53
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x54 - 0x57
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x58 - 0x5B
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x5C - 0x5F

	// Lowercase
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x60 - 0x63
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x64 - 0x67
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x68 - 0x6B
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x6C - 0x6F
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x70 - 0x73
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x74 - 0x77
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x78 - 0x8B
	LETTER_PROB, LETTER_PROB, LETTER_PROB, LETTER_PROB,	// 0x7C - 0x7F

	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0x80 - 0x83
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0x84 - 0x87
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0x88 - 0x8B
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0x8C - 0x8F
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0x90 - 0x93
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0x94 - 0x97
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0x98 - 0x9B
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0x9C - 0x9F

	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xA0 - 0xA3
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xA4 - 0xA7
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xA8 - 0xAB
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xAC - 0xAF
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xB0 - 0xB3
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xB4 - 0xB7
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xB8 - 0xBB
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xBC - 0xBF

	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xC0 - 0xC3
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xC4 - 0xC7
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xC8 - 0xCB
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xCC - 0xCF
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xD0 - 0xD3
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xD4 - 0xD7
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xD8 - 0xDB
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xDC - 0xDF

	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xE0 - 0xE3
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xE4 - 0xE7
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xE8 - 0xEB
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xEC - 0xEF
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xF0 - 0xF3
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xF4 - 0xF7
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xF8 - 0xFB
	EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB, EXTENDED_PROB,	// 0xFC - 0xFF
	
	// Virtual events
	// DOLATER kwk - Why not generate keyboardAlphaChr (0x110) & keyboardNumericChr (0x111)?
	0, 0, 0, NXTFLD_PROB, 0, MENU_PROB, CONTROL_PROB, 0,				// 0x100 - 0x107
	CONTROL_PROB, KBRD_PROB, CONTROL_PROB, 0, NXTFLD_PROB, 0, 0, 0,		// 0x108 - 0x10f

};

#define NUM_OF_QUOTES	18

// Shakespearean quotes used by Gremlins for English text
static const char * kAsciiQuotes[NUM_OF_QUOTES] = {
	"Out out damn spot!",
	
	"Et tu, Brute?",
	
	"When in disgrace with fortune and mens' eyes "
		"I all alone beweep my outcast state.  "
		"And trouble deaf heaven with my bootless cries and "
		"look upon myself and curse my fate. "
		"Wishing me like to one more rich in hope, "
		"featured like him, like him with friends possest, "
		"desiring this man's art and that man's scope, "
		"with what I most enjoy contented least;"
		"\n"
		"Yet in these thoughts myself almost despising- "
		"haply I think on thee: and then my state, "
		"like to the lark at break of day arising "
		"from sullen earth, sings hymns at Heaven's gate; "
		"for thy sweet love rememb'red such wealth brings "
		"that then I scorn to change my state with kings.",
		
	"I think my wife is honest, and think she is not; "
		"I think that thou art just, and think thou art not.",
		
	"O that this too too sullied flesh would melt, thaw, "
		"and resolve itself into a dew, "
		"or that the Everlasting had not fixed "
		"His canon 'gainst self-slaughter.",
		
	"Come, you spirits that tend on mortal thoughts, unsex me here, "
		"and fill me from the crown to the toe top-full "
		"of direst cruelty.",
		
	"I do not think but Desdemona's honest.",
	
	"That I did love the Moor to live with him",
	
	"What a piece of work is a man",
	
	"Fair is foul, and foul is fair.",
	
	"All hail, Macbeth, that shalt be King hereafter!",
	
	"What's Montague?",
	
	"To a nunnery, go, and quickly too.",
	
	"I'll have some proof.",
	
	"Now are we well resolved, and by God's help and yours, "
		"the noble sinews of our power, France being ours, "
		"we'll bend it to our awe or break it all to pieces.",
		
	"Tennis balls, my liege.",
	
	"De Sin: le col de Nick, le menton de Sin.",
	
	"But swords I smile at, weapons laugh to scorn, "
		"brandished by man that's of a woman born."
	
};

static const char * kShiftJISQuotes[NUM_OF_QUOTES] = {
	"\x90\x6c\x82\xcd\x81\x41\x82\xa9\x82\xc2\x82\xc4\x90\x58\x82\xcc"
	"\x90\x5f\x82\xf0\x8e\x45\x82\xb5\x82\xbd",

	"\x82\xe0\x82\xcc\x82\xcc\x82\xaf\x95\x50",

	"\x90\x6c\x96\xca\x82\xc6\x8f\x62\x82\xcc\x90\x67\x91\xcc\x81\x41"
	"\x8e\xf7\x96\xd8\x82\xcc\x8a\x70\x82\xf0\x8e\x9d\x82\xc2\x90\x58"
	"\x82\xcc\x90\x5f\x81\x45\x83\x56\x83\x56\x90\x5f\x82\xf0\x90\x6c"
	"\x82\xcd\x89\xbd\x8c\xcc\x8e\x45\x82\xb3\x82\xcb\x82\xce\x82\xc8"
	"\x82\xe7\x82\xc8\x82\xa9\x82\xc1\x82\xbd\x82\xcc\x82\xa9\x81\x7c"
	"\x82\xb1\x82\xcc\x8e\x9e\x91\xe3\x81\x41\x90\x6c\x8a\xd4\x82\xaa"
	"\x82\xd3\x82\xa6\x81\x41\x91\xbd\x82\xad\x82\xcc\x8c\xb4\x90\xb6"
	"\x97\xd1\x82\xaa\x91\xf1\x82\xa9\x82\xea\x82\xbd\x82\xc6\x82\xcd"
	"\x82\xa2\x82\xa6\x81\x41\x82\xdc\x82\xbe\x90\x6c\x82\xf0\x8a\xf1"
	"\x82\xb9\x82\xc2\x82\xaf\x82\xca\x91\xbe\x8c\xc3\x82\xcc\x90\x58"
	"\x82\xaa\x82\xa0\x82\xbf\x82\xb1\x82\xbf\x82\xc9\x8e\x63\x82\xc1"
	"\x82\xc4\x82\xa2\x82\xbd\x0a\x82\xbb\x82\xea\x82\xbc\x82\xea\x82"
	"\xcc\x90\x58\x82\xcd\x81\x41\x92\x96\x82\xe2\x8e\x52\x8c\xa2\x82"
	"\xc8\x82\xc7\x8b\x90\x91\xe5\x82\xc5\x8c\xab\x82\xa9\x82\xc1\x82"
	"\xbd\x8f\x62\x82\xbd\x82\xbf\x82\xaa\x95\x4b\x8e\x80\x82\xc9\x82"
	"\xc8\x82\xc1\x82\xc4\x8e\xe7\x82\xc1\x82\xc4\x82\xa2\x82\xbd\x82"
	"\xbb\x82\xb5\x82\xc4\x81\x41\x90\xb9\x88\xe6\x82\xf0\x90\x4e\x82"
	"\xb7\x90\x6c\x8a\xd4\x92\x42\x82\xf0\x8f\x50\x82\xa2\x8d\x72\x82"
	"\xd4\x82\xe9\x90\x5f\x81\x58\x82\xc6\x8b\xb0\x82\xea\x82\xe7\x82"
	"\xea\x82\xc4\x82\xa2\x82\xbd\x82\xbb\x82\xcc\x8f\x62\x92\x42\x82"
	"\xf0\x8f\x5d\x82\xa6\x82\xc4\x82\xa2\x82\xbd\x82\xcc\x82\xaa\x81"
	"\x41\x83\x56\x83\x56\x90\x5f\x82\xc5\x82\xa0\x82\xe9\x8d\x72\x82"
	"\xd4\x82\xe9\x90\x5f\x81\x58\x82\xf0\x8d\xc5\x82\xe0\x8c\x83\x82"
	"\xb5\x82\xad\x90\xed\x82\xc1\x82\xc4\x82\xa2\x82\xbd\x82\xcc\x82"
	"\xcd\x83\x5e\x83\x5e\x83\x89\x8e\xd2\x82\xc6\x8c\xc4\x82\xce\x82"
	"\xea\x82\xe9\x90\xbb\x93\x53\x8f\x57\x92\x63\x82\xbe\x82\xc1\x82"
	"\xbd",

	"\x8f\x97\x82\xcc\x90\x67\x82\xc5\x83\x5e\x83\x5e\x83\x89\x8f\x57"
	"\x92\x63\x82\xf0\x97\xa6\x82\xa2\x82\xe9\x83\x47\x83\x7b\x83\x56"
	"\x8c\xe4\x91\x4f\x94\xde\x8f\x97\x82\xcd\x8c\xc8\x82\xaa\x90\x4d"
	"\x94\x4f\x82\xc5\x81\x41\x90\x58\x82\xf0\x90\xd8\x82\xe8\x91\xf1"
	"\x82\xa2\x82\xc4\x82\xa2\x82\xbd",

	"\x82\xbb\x82\xcc\x94\x7a\x89\xba\x82\xc5\x81\x41\x8c\xe4\x91\x4f"
	"\x82\xf0\x8c\x68\x82\xa2\x95\xe7\x82\xa4\x81\x41\x83\x53\x83\x93"
	"\x83\x55\x82\xc9\x82\xa8\x83\x67\x83\x4c\x82\xc6\x8d\x62\x98\x5a"
	"\x83\x56\x83\x56\x90\x5f\x82\xf0\x82\xcb\x82\xe7\x82\xa4\x90\xb3"
	"\x91\xcc\x95\x73\x96\xbe\x82\xcc\x96\x56\x8e\xe5\x81\x45\x83\x57"
	"\x83\x52\x96\x56\x96\x6b\x82\xcc\x92\x6e\x82\xcc\x89\xca\x82\xc4"
	"\x82\xcc\x89\x42\x82\xea\x97\xa2\x82\xc9\x8f\x5a\x82\xde\x98\x56"
	"\x9b\xde\x8f\x97\x81\x45\x83\x71\x83\x43\x82\xb3\x82\xdc\x89\xb3"
	"\x8e\x96\x8e\xe5\x81\x41\x83\x69\x83\x53\x82\xcc\x90\x5f\x81\x41"
	"\x83\x82\x83\x8d\x82\xc8\x82\xc7\x90\x58\x82\xf0\x8e\xe7\x82\xe9"
	"\x90\x5f\x8f\x62\x82\xbd\x82\xbf",

	"\x82\xbb\x82\xea\x82\xc9\x90\x58\x82\xcc\x90\xb8\x97\xec\x81\x45"
	"\x83\x52\x83\x5f\x83\x7d\x82\xbd\x82\xbf\x81\x63\x8f\xad\x8f\x97"
	"\x83\x54\x83\x93\x82\xcd\x90\x6c\x8a\xd4\x82\xcc\x8e\x71\x82\xc5"
	"\x82\xa0\x82\xe8\x82\xc8\x82\xaa\x82\xe7\x8e\x52\x8c\xa2\x83\x82"
	"\x83\x8d\x82\xc9\x88\xe7\x82\xc4\x82\xe7\x82\xea\x82\xbd\x81\x75"
	"\x82\xe0\x82\xcc\x82\xcc\x82\xaf\x95\x50\x81\x76",

	"\x82\xbe\x82\xc1\x82\xbd\x83\x54\x83\x93\x82\xcd\x81\x41\x90\x58"
	"\x82\xf0\x90\x4e\x82\xb7\x90\x6c\x8a\xd4\x82\xf0\x8c\x83\x82\xb5"
	"\x82\xad\x91\x9e",

	"\x82\xf1\x82\xc5\x82\xa2\x82\xbd\x82\xbb\x82\xb5\x82\xc4\x81\x41"
	"\x90\x6c\x8a\xd4\x82\xc6\x8d\x72\x82\xd4\x82\xe9\x90\x5f\x81\x58"
	"\x82\xcc\x8d\xc5\x8c\xe3",

	"\x82\xcc\x91\xe5\x8c\x88\x90\xed\x82\xc9\x8a\xaa\x82\xab\x8d\x9e"
	"\x82\xdc\x82\xea\x82\xe9\x8f\xad\x94\x4e",

	"\x83\x41\x83\x56\x83\x5e\x83\x4a\x94\xde\x82\xcd\x81\x41\x8e\x80"
	"\x82\xcc\x8e\xf4\x82\xa2\x82\xf0",

	"\x82\xa9\x82\xaf\x82\xe7\x82\xea\x82\xbd\x82\xaa\x82\xe4\x82\xa6"
	"\x82\xc9\xe2\x71\x82\xea\x82\xf0\x8f\xf2\x82\xdf\x82\xe9\x95\xfb"
	"\x96\x40\x82\xf0\x92\x54\x82\xb5\x82\xc9\x81\x41",

	"\x97\xb7\x82\xc9\x8f\x6f\x82\xbd\x8f\xad\x94\x4e\x82\xbe\x82\xc1"
	"\x82\xbd",

	"\x8f\xad\x94\x4e\x82\xc6\x8f\xad\x8f\x97\x82\xcd\x8e\x53\x8c\x80"
	"\x82\xcc\x92\x86\x82\xc5\x8f\x6f\x89\xef\x82\xa2\x81\x41",

	"\x8e\x9f\x91\xe6\x82\xc9\x90\x53\x82\xf0\x92\xca\x82\xed\x82\xb9",

	"\x82\xc4\x82\xe4\x82\xad\x82\xd3\x82\xbd\x82\xe8\x82\xaa\x91\x9e"
	"\x88\xab\x82\xc6\x8e\x45\x9d\x43\x82\xcc\x89\xca\x82\xc4\x82\xc9"
	"\x8c\xa9\x82\xa2\x82\xbe\x82\xb5\x82\xbd\x8a\xf3\x96\x5d\x82\xc6"
	"\x82\xcd\x89\xbd\x82\xbe\x82\xc1\x82\xbd\x82\xcc\x82\xa9\x8f\xad"
	"\x94\x4e\x82\xc6\x8f\xad\x8f\x97\x82\xcc\x88\xa4\x82\xf0\x89\xa1"
	"\x8e\x85\x82\xc9\x83\x56\x83\x56\x90\x5f\x82\xf0\x82\xdf\x82\xae"
	"\x82\xe9\x90\x6c\x8a\xd4\x82\xc6\x8f\x62\x82\xbd\x82\xbf",

	"\x82\xcc\x90\xed\x82\xa2\x82\xf0\x8f\x63\x8e\x85\x82\xc9",

	"\x94\x67\xe0\x70\x96\x9c\x8f\xe4\x82\xcc\x88\xea\x91\xe5\x8f\x96"
	"\x8e\x96\x8e\x8d\x82\xaa\x81\x41\x93\x57\x8a\x4a\x82\xb3\x82\xea"
	"\x82\xc4\x82\xa2\x82\xad\x81\x63",

	"\x8c\xb4\x8d\xec\x81\x45\x8b\x72\x96\x7b\x81\x40\x81\x40\x81\x46"
	"\x8b\x7b\x8d\xe8\x81\x40\x8f\x78\x90\xbb\x81\x40\x8d\xec\x81\x40"
	"\x81\x40\x81\x40\x81\x40\x81\x46\x8e\x81\x89\xc6\xea\x8e\x88\xea"
	"\x98\x59\x81\x45\x90\xac\x93\x63\x81\x40\x96\x4c\x90\xbb\x8d\xec"
	"\x91\x8d\x8e\x77\x8a\xf6\x81\x40\x81\x40\x81\x46\x93\xbf\x8a\xd4"
	"\x8d\x4e\x89\xf5\x83\x76\x83\x8d\x83\x66\x83\x85\x81\x5b\x83\x54"
	"\x81\x5b\x81\x46\x97\xe9\x96\xd8\x95\x71\x95\x76",
};

static const char * kBig5Quotes[NUM_OF_QUOTES] = {
	"\xa6\xb9\xb6\x7d\xa8\xf7\xb2\xc4\xa4\x40\xa6\x5e\xa4\x5d\xa1\x43"
	"\xa7\x40",

	"\xaa\xcc\xa6\xdb\xb6\xb3\xa1\x47\xa6\x5d\xb4\xbf",

	"\xbe\xe4\xb9\x4c\xa4\x40\xb5\x66\xb9\xda\xa4\xdb\xa4\xa7\xab\xe1"
	"\xa1\x41\xac\x47\xb1\x4e\xaf\x75\xa8\xc6\xc1\xf4\xa5\x68\xa1\x41"
	"\xa6\xd3\xad\xc9\x22\xb3\x71\xc6\x46\x22\xa4\xa7\xbb\xa1\xa1\x41"
	"\xbc\xb6\xa6\xb9\xa4\x40\xae\xd1\xa4\x5d\xa1\x43\xac\x47\xa4\xea"
	"\x22\xba\xc2\xa4\x68\xc1\xf4\x22\xa4\xaa\xa4\xaa\xa1\x43\xa6\xfd"
	"\xae\xd1\xa4\xa4\xa9\xd2\xb0\x4f\xa6\xf3\xa8\xc6\xa6\xf3\xa4\x48"
	"\xa1\x48\xa6\xdb\xa4\x53\xb6\xb3\xa1\x47\xa4\xb5\xad\xb7\xb9\xd0"
	"\xb8\x4c\xb8\x4c\xa1\x41\xa4\x40\xa8\xc6\xb5\x4c\xa6\xa8\xa1\x41",

	"\xa9\xbf\xa9\xc0\xa4\xce\xb7\xed\xa4\xe9\xa9\xd2\xa6\xb3\xa4\xa7"
	"\xa4\x6b\xa4\x6c\xa1\x41\xa4\x40\xa4\x40\xb2\xd3\xa6\xd2\xb8\xfb"
	"\xa5\x68\xa1\x41\xc4\xb1\xa8\xe4\xa6\xe6\xa4\xee\xa8\xa3\xc3\xd1"
	"\xa1\x41\xac\xd2\xa5\x58\xa9\xf3\xa7\xda\xa4\xa7\xa4\x57\xa1\x43"
	"\xa6\xf3\xa7\xda\xb0\xf3\xb0\xf3\xc5\xbd\xac\xdc\xa1\x41\xb8\xdb"
	"\xa4\xa3\xad\x59\xa9\xbc\xb8\xc8\xb3\xa6\xab\x76\xa1\x48\xb9\xea"
	"\xb7\x5c\xab\x68\xa6\xb3\xbe\x6c\xa1\x41\xae\xac\xa4\x53\xb5\x4c"
	"\xaf\x71\xa4\xa7\xa4\x6a\xb5\x4c\xa5\x69\xa6\x70\xa6\xf3\xa4\xa7"
	"\xa4\xe9\xa4\x5d\xa1\x49\xb7\xed\xa6\xb9\xa1\x41\xab\x68\xa6\xdb"
	"\xb1\xfd\xb1\x4e\xa4\x77\xa9\xb9\xa9\xd2\xbf\xe0\xa4\xd1\xae\xa6"
	"\xaf\xaa\xbc\x77\xa1\x41\xc0\x41\xa6\xe7\xd0\x4b\xa4\xa7\xae\xc9"
	"\xa1\x41\xdc\xae\xa5\xcc\xc5\xe4\xaa\xce\xa4\xa7\xa4\xe9\xa1\x41"
	"\xad\x49\xa4\xf7\xa5\x53\xb1\xd0\xa8\x7c\xa4\xa7\xae\xa6\xa1\x41"
	"\xad\x74\xae\x76\x20\x0a\xa4\xcd\xb3\x57\xbd\xcd\xa4\xa7\xbc\x77"
	"\xa1\x41\xa5\x48\xa6\xdc\xa4\xb5\xa4\xe9\xa4\x40\xa7\xde\xb5\x4c"
	"\xa6\xa8\xa1\x41\xa5\x62\xa5\xcd\xbc\xe3\xad\xcb\xa4\xa7\xb8\x6f"
	"\xa1\x41\xbd\x73\x20\xad\x7a\xa4\x40\xb6\xb0\xa1\x41\xa5\x48\xa7"
	"\x69\xa4\xd1\xa4\x55\xa4\x48\xa1\x47\xa7\xda\xa4\xa7\xb8\x6f\xa9"
	"\x54\xa4\xa3\xa7\x4b\xa1\x41\xb5\x4d\xbb\xd3\xbb\xd5\xa4\xa4\xa5"
	"\xbb\xa6\xdb\xbe\xfa\xbe\xfa\xa6\xb3\xa4\x48\xa1\x41\xb8\x55\xa4"
	"\xa3\xa5\x69\xa6\x5d\xa7\xda\xa4\xa7\xa4\xa3\xa8\x76\xa1\x41\xa6"
	"\xdb\xc5\x40\xa4\x76\xb5\x75\x20\xa1\x41\xa4\x40\xa8\xd6\xa8\xcf"
	"\xa8\xe4\xaa\x7b\xb7\xc0\xa4\x5d\xa1\x43\xc1\xf6\xa4\xb5\xa4\xe9"
	"\xa4\xa7\xad\x54\xdd\xdc\xbd\xb4\xbc\xf8\xa1\x41\xa5\xcb\x5f\xc3"
	"\xb7\xa7\xc9\xa1\x41\xa8\xe4\x20\xb1\xe1\xa4\x69\xad\xb7\xc5\x53"
	"\xa1\x41\xb6\xa5\xac\x68\xae\x78\xaa\xe1\xa1\x41\xa5\xe7\xa5\xbc"
	"\xa6\xb3\xa7\xab\xa7\xda\xa4\xa7\xc3\xcc\xc3\x68\xb5\xa7\xbe\xa5"
	"\xaa\xcc\xa1\x43\xc1\xf6\xa7\xda\x20\xa5\xbc\xbe\xc7\xa1\x41\xa4"
	"\x55\xb5\xa7\xb5\x4c\xa4\xe5\xa1\x41\xa4\x53\xa6\xf3\xa7\xab\xa5"
	"\xce\xb0\xb2\xbb\x79\xa7\xf8\xa8\xa5\xa1\x41\xbc\xc5\xba\x74\xa5"
	"\x58\xa4\x40\xac\x71\xac\x47\xa8\xc6\xa8\xd3\xa1\x41\xa5\xe7\xa5"
	"\x69\xa8\xcf\xbb\xd3\xbb\xd5\xac\x4c\xb6\xc7\xa1\x41\xbd\xc6\xa5"
	"\x69\xae\xae\xa5\x40\xa4\xa7\xa5\xd8\xa1\x41\xaf\x7d\xa4\x48\xb7"
	"\x54\xb4\x65\xa1\x41\xa4\xa3\xa5\xe7\x20\xa9\x79\xa5\x47\xa1\x48"
	"\xac\x47\xa4\xea\x22\xb8\xeb\xab\x42\xa7\xf8\x22\xa4\xaa\xa4\xaa"
	"\xa1\x43\xa6\xb9\xa6\x5e\xa4\xa4\xa4\x5a\xa5\xce\x22\xb9\xda\x22"
	"\xa5\xce\x22\xa4\xdb\x22",

	"\xb5\xa5\xa6\x72\xa1\x41\xac\x4f\xb4\xa3\xbf\xf4\xbe\x5c\xaa\xcc"
	"\xb2\xb4\xa5\xd8\xa1\x41\xa5\xe7\xac\x4f\xa6\xb9\xae\xd1\xa5\xdf"
	"\xb7\x4e\xa5\xbb\xa6\xae\xa1\x43\xa6\x43\xa6\xec\xac\xdd\xa9\x78"
	"\xa1\x47\xa7\x41\xb9\x44\xa6\xb9\xae\xd1\xb1\x71\xa6\xf3\xa6\xd3"
	"\xa8\xd3\xa1\x48\xbb\xa1\xb0\x5f\xae\xda\xa5\xd1\xc1\xf6\xaa\xf1"
	"\xaf\xee\x20\xad\xf0\xa1\x41\xb2\xd3\xab\xf6\xab\x68\xb2\x60\xa6"
	"\xb3\xbd\xec\xa8\xfd\xa1\x43\xab\xdd\xa6\x62\xa4\x55\xb1\x4e\xa6"
	"\xb9\xa8\xd3\xbe\xfa\xaa\x60\xa9\xfa\xad\xec\xa8\xd3\xa4\x6b\xb4"
	"\x45\xa4\xf3\xb7\xd2\xa5\xdb\xb8\xc9\xa4\xd1\xa4\xa7\xae\xc9\xa1"
	"\x41\xa9\xf3\xa4\x6a\xaf\xee\xa4\x73",

	"\xb5\x4c\xbd\x5d\xb1\x56\xbd\x6d\xa6\xa8\xb0\xaa\xb8\x67\xa2\xcc"
	"\xa4\x47\xa4\x56\xa1\x41\xa4\xe8\xb8\x67\xa4\x47\xa2\xcc\xa5\x7c"
	"\xa4\x56\xb9\x78\xa5\xdb\xa4\x54\xb8\x55\xa4\xbb\xa4\x64\xa4\xad"
	"\xa6\xca\xb9\x73\xa4\x40\xb6\xf4\xa1\x43\xb4\x45\xac\xd3\xa4\xf3"
	"\xa5\x75\xa5\xce\xa4\x46\xa4\x54\xb8\x55\xa4\xbb\xa4\x64\xa4\xad"
	"\xa6\xca\xb6\xf4\xa1\x41\xa5\x75\xb3\xe6\xb3\xe6\xb3\xd1\xa4\x46"
	"\xa4\x40\xb6\xf4\xa5\xbc\xa5\xce\xa1\x41\xab\x4b\xb1\xf3\xa6\x62"
	"\xa6\xb9\xa4\x73\xab\x43\xae\x47\xae\x70\xa4\x55\xa1\x43",

	"\xbd\xd6\xaa\xbe\xa6\xb9\xa5\xdb\xa6\xdb\xb8\x67\xb7\xd2\xa4\xa7"
	"\xab\xe1\xa1\x41\xc6\x46\xa9\xca\xa4\x77\xb3\x71\xa1\x41\xa6\x5d"
	"\xa8\xa3\x5f\xa5\xdb",

	"\xad\xd1\xb1\x6f\xb8\xc9\xa4\xd1\xa1\x41\xbf\x57\xa6\xdb\xa4\x76"
	"\xb5\x4c\xa7\xf7\xa4\xa3\xb3\xf4\xa4\x4a\xbf\xef\xa1\x41\xb9\x45"
	"\xa6\xdb\xab\xe8\xa6\xdb\xbc\xdb",

	"\xa1\x41\xa4\xe9\xa9\x5d\xb4\x64\xb8\xb9\xba\x46\xb7\x5c\xa1\x43"
	"\xa4\x40\xa4\xe9\xa1\x41\xa5\xbf\xb7\xed\xb6\xd8",

	"\xb1\xa5\xa4\xa7\xbb\xda\xa1\x41\xab\x58\xa8\xa3\xa4\x40\xb9\xac"
	"\xa4\x40\xb9\x44\xbb\xb7\xbb\xb7\xa6\xd3\xa8\xd3\xa1\x41",

	"\xa5\xcd\xb1\x6f\xb0\xa9\xae\xe6\xa4\xa3\xa4\x5a\xa1\x41\xc2\xd7"
	"\xaf\xab\xad\x7e\xb2\xa7\xa1\x41\xbb\xa1\xbb\xa1\xaf\xba\xaf\xba"
	"\xa8\xd3\xa6\xdc\xae\x70\xa4\x55\xa1\x41\xa7\xa4\xa4\x5f\xa5\xdb",

	"\xc3\xe4\xb0\xaa\xbd\xcd\xa7\xd6\xbd\xd7\xa1\x43\xa5\xfd\xac\x4f",

	"\xbb\xa1\xa8\xc7\xb6\xb3\xa4\x73\xc3\xfa\xae\xfc\xaf\xab\xa5\x50"
	"\xa5\xc8\xa4\xdb\xa4\xa7\xa8\xc6\xa1\x41\xab\xe1\xab\x4b\xbb\xa1"
	"\xa8\xec",

	"\xac\xf5\xb9\xd0\xa4\xa4\xba\x61\xb5\xd8\xb4\x49\xb6\x51\xa1\x43"
	"\xa6\xb9\xa5\xdb",

	"\xc5\xa5\xa4\x46\xa1\x41\xa4\xa3\xc4\xb1\xa5\xb4\xb0\xca\xa4\x5a"
	"\xa4\xdf\xa1\x41\xa4\x5d\xb7\x51\xad\x6e\xa8\xec\xa4\x48\xb6\xa1"
	"\xa5\x68\xa8\xc9\xa4\x40\xa8\xc9\xb3\x6f\xba\x61\xb5\xd8\xb4\x49"
	"\xb6\x51\xa1\x41\xa6\xfd\xa6\xdb\xab\xeb\xb2\xca\xc4\xf8\xa1\x41"
	"\xa4\xa3\xb1\x6f\xa4\x77\xa1\x41\xab\x4b\xa4\x66\xa6\x52\xa4\x48"
	"\xa8\xa5\xa1\x41\xa6\x56\xa8\xba\xb9\xac\xb9\x44\xbb\xa1\xb9\x44"
	"\xa1\x47\xa4\x6a\xae\x76\xa1\x41\xa7\xcc\xa4\x6c\xc4\xf8\xaa\xab"
	"\xa1\x41\xa4\xa3\xaf\xe0\xa8\xa3\xc2\xa7\xa4\x46\xa1\x43\xbe\x41"
	"\xbb\x44\xa4\x47\xa6\xec\xbd\xcd\xa8\xba\xa4\x48\xa5\x40\xb6\xa1"
	"\xba\x61\xc4\xa3\xc1\x63\xb5\xd8\xa1\x41",

	"\xa4\xdf\xa4\xc1\xbc\x7d\xa4\xa7\xa1\x43\xa7\xcc\xa4\x6c\xbd\xe8"
	"\xc1\xf6\xb2\xca\xc4\xf8",

	"\xa1\x41\xa9\xca\xab\x6f\xb5\x79\xb3\x71\xa1\x41\xaa\x70\xa8\xa3"
	"\xa4\x47\xae\x76\xa5\x50\xa7\xce\xb9\x44\xc5\xe9\xa1\x41\xa9\x77"
	"\xab\x44\xa4\x5a\xab\x7e\xa1\x41\xa5\xb2",

	"\xa6\xb3\xb8\xc9\xa4\xd1\xc0\xd9\xa5\x40\xa4\xa7\xa7\xf7\xa1\x41"
	"\xa7\x51\xaa\xab\xc0\xd9\xa4\x48\xa4\xa7\xbc\x77\xa1\x43\xa6\x70"
	"\xbb\x58\xb5\x6f\xa4\x40\xc2\x49\xb7\x4f\xa4\xdf\xa1\x41\xc4\xe2"
	"\xb1\x61\xa7\xcc\xa4\x6c\xb1\x6f\xa4\x4a\xac\xf5\xb9\xd0\xa1\x41"
	"\xa6\x62\xa8\xba\xb4\x49\xb6\x51\xb3\xf5\xa4\xa4\xa1\x41\xb7\xc5"
	"\xac\x58\xb6\x6d\xa8\xbd"
};

static const char * kGB2312Quotes[NUM_OF_QUOTES] = {
	"\xb4\xcb\xbf\xaa\xbe\xed\xb5\xda\xd2\xbb\xbb\xd8\xd2\xb2\xa1\xa3"
	"\xd7\xf7",

	"\xd5\xdf\xd7\xd4\xd4\xc6\xa3\xba\xd2\xf2\xd4\xf8",

	"\xc0\xfa\xb9\xfd\xd2\xbb\xb7\xac\xc3\xce\xbb\xc3\xd6\xae\xba\xf3"
	"\xa3\xac\xb9\xca\xbd\xab\xd5\xe6\xca\xc2\xd2\xfe\xc8\xa5\xa3\xac"
	"\xb6\xf8\xbd\xe8\xa1\xb0\xcd\xa8\xc1\xe9\xa1\xb1\xd6\xae\xcb\xb5"
	"\xa3\xac\xd7\xab\xb4\xcb\xd2\xbb\xca\xe9\xd2\xb2\xa1\xa3\xb9\xca"
	"\xd4\xbb\xa1\xb0\xd5\xe7\xca\xbf\xd2\xfe\xa1\xb1\xd4\xc6\xd4\xc6"
	"\xa1\xa3\xb5\xab\xca\xe9\xd6\xd0\xcb\xf9\xbc\xc7\xba\xce\xca\xc2"
	"\xba\xce\xc8\xcb\xa3\xbf\xd7\xd4\xd3\xd6\xd4\xc6\xa3\xba\xbd\xf1"
	"\xb7\xe7\xb3\xbe\xc2\xb5\xc2\xb5\xa3\xac\xd2\xbb\xca\xc2\xce\xde"
	"\xb3\xc9\xa3\xac",

	"\xba\xf6\xc4\xee\xbc\xb0\xb5\xb1\xc8\xd5\xcb\xf9\xd3\xd0\xd6\xae"
	"\xc5\xae\xd7\xd3\xa3\xac\xd2\xbb\xd2\xbb\xcf\xb8\xbf\xbc\xbd\xcf"
	"\xc8\xa5\xa3\xac\xbe\xf5\xc6\xe4\xd0\xd0\xd6\xb9\xbc\xfb\xca\xb6"
	"\xa3\xac\xbd\xd4\xb3\xf6\xd3\xda\xce\xd2\xd6\xae\xc9\xcf\xa1\xa3"
	"\xba\xce\xce\xd2\xcc\xc3\xcc\xc3\xd0\xeb\xc3\xbc\xa3\xac\xb3\xcf"
	"\xb2\xbb\xc8\xf4\xb1\xcb\xc8\xb9\xee\xce\xd4\xd5\xa3\xbf\xca\xb5"
	"\xc0\xa2\xd4\xf2\xd3\xd0\xd3\xe0\xa3\xac\xbb\xda\xd3\xd6\xce\xde"
	"\xd2\xe6\xd6\xae\xb4\xf3\xce\xde\xbf\xc9\xc8\xe7\xba\xce\xd6\xae"
	"\xc8\xd5\xd2\xb2\xa3\xa1\xb5\xb1\xb4\xcb\xa3\xac\xd4\xf2\xd7\xd4"
	"\xd3\xfb\xbd\xab\xd2\xd1\xcd\xf9\xcb\xf9\xc0\xb5\xcc\xec\xb6\xf7"
	"\xd7\xe6\xb5\xc2\xa3\xac\xbd\xf5\xd2\xc2\xe6\xfd\xd6\xae\xca\xb1"
	"\xa3\xac\xe2\xc0\xb8\xca\xf7\xd0\xb7\xca\xd6\xae\xc8\xd5\xa3\xac"
	"\xb1\xb3\xb8\xb8\xd0\xd6\xbd\xcc\xd3\xfd\xd6\xae\xb6\xf7\xa3\xac"
	"\xb8\xba\xca\xa6\x0a\xd3\xd1\xb9\xe6\xcc\xb8\xd6\xae\xb5\xc2\xa3"
	"\xac\xd2\xd4\xd6\xc1\xbd\xf1\xc8\xd5\xd2\xbb\xbc\xbc\xce\xde\xb3"
	"\xc9\xa3\xac\xb0\xeb\xc9\xfa\xc1\xca\xb5\xb9\xd6\xae\xd7\xef\xa3"
	"\xac\xb1\xe0\xca\xf6\xd2\xbb\xbc\xaf\xa3\xac\xd2\xd4\xb8\xe6\xcc"
	"\xec\xcf\xc2\xc8\xcb\xa3\xba\xce\xd2\xd6\xae\xd7\xef\xb9\xcc\xb2"
	"\xbb\xc3\xe2\xa3\xac\xc8\xbb\xb9\xeb\xb8\xf3\xd6\xd0\xb1\xbe\xd7"
	"\xd4\xc0\xfa\xc0\xfa\xd3\xd0\xc8\xcb\xa3\xac\xcd\xf2\xb2\xbb\xbf"
	"\xc9\xd2\xf2\xce\xd2\xd6\xae\xb2\xbb\xd0\xa4\xa3\xac\xd7\xd4\xbb"
	"\xa4\xbc\xba\xb6\xcc\xa3\xac\xd2\xbb\xb2\xa2\xca\xb9\xc6\xe4\xe3"
	"\xfd\xc3\xf0\xd2\xb2\xa1\xa3\xcb\xe4\xbd\xf1\xc8\xd5\xd6\xae\xc3"
	"\xa9\xb4\xaa\xc5\xee\xeb\xbb\xa3\xac\xcd\xdf\xd4\xee\xc9\xfe\xb4"
	"\xb2\xa3\xac\xc6\xe4\xb3\xbf\xcf\xa6\xb7\xe7\xc2\xb6\xa3\xac\xbd"
	"\xd7\xc1\xf8\xcd\xa5\xbb\xa8\xa3\xac\xd2\xe0\xce\xb4\xd3\xd0\xb7"
	"\xc1\xce\xd2\xd6\xae\xbd\xf3\xbb\xb3\xb1\xca\xc4\xab\xd5\xdf\xa1"
	"\xa3\xcb\xe4\xce\xd2\xce\xb4\xd1\xa7\xa3\xac\xcf\xc2\xb1\xca\xce"
	"\xde\xce\xc4\xa3\xac\xd3\xd6\xba\xce\xb7\xc1\xd3\xc3\xbc\xd9\xd3"
	"\xef\xb4\xe5\xd1\xd4\xa3\xac\xb7\xf3\xd1\xdd\xb3\xf6\xd2\xbb\xb6"
	"\xce\xb9\xca\xca\xc2\xc0\xb4\xa3\xac\xd2\xe0\xbf\xc9\xca\xb9\xb9"
	"\xeb\xb8\xf3\xd5\xd1\xb4\xab\xa3\xac\xb8\xb4\xbf\xc9\xd4\xc3\xca"
	"\xc0\xd6\xae\xc4\xbf\xa3\xac\xc6\xc6\xc8\xcb\xb3\xee\xc3\xc6\xa3"
	"\xac\xb2\xbb\xd2\xe0\xd2\xcb\xba\xf5\xa3\xbf\xb9\xca\xd4\xbb\xa1"
	"\xb0\xbc\xd6\xd3\xea\xb4\xe5\xa1\xb1\xd4\xc6\xd4\xc6\xa1\xa3\xb4"
	"\xcb\xbb\xd8\xd6\xd0\xb7\xb2\xd3\xc3\xa1\xb0\xc3\xce\xa1\xb1\xd3"
	"\xc3\xa1\xb0\xbb\xc3\xa1\xb1",

	"\xb5\xc8\xd7\xd6\xa3\xac\xca\xc7\xcc\xe1\xd0\xd1\xd4\xc4\xd5\xdf"
	"\xd1\xdb\xc4\xbf\xa3\xac\xd2\xe0\xca\xc7\xb4\xcb\xca\xe9\xc1\xa2"
	"\xd2\xe2\xb1\xbe\xd6\xbc\xa1\xa3\xc1\xd0\xce\xbb\xbf\xb4\xb9\xd9"
	"\xa3\xba\xc4\xe3\xb5\xc0\xb4\xcb\xca\xe9\xb4\xd3\xba\xce\xb6\xf8"
	"\xc0\xb4\xa3\xbf\xcb\xb5\xc6\xf0\xb8\xf9\xd3\xc9\xcb\xe4\xbd\xfc"
	"\xbb\xc4\xcc\xc6\xa3\xac\xcf\xb8\xb0\xb4\xd4\xf2\xc9\xee\xd3\xd0"
	"\xc8\xa4\xce\xb6\xa1\xa3\xb4\xfd\xd4\xda\xcf\xc2\xbd\xab\xb4\xcb"
	"\xc0\xb4\xc0\xfa\xd7\xa2\xc3\xf7\xd4\xad\xc0\xb4\xc5\xae\xe6\xb4"
	"\xca\xcf\xc1\xb6\xca\xaf\xb2\xb9\xcc\xec\xd6\xae\xca\xb1\xa3\xac"
	"\xd3\xda\xb4\xf3\xbb\xc4\xc9\xbd",

	"\xce\xde\xbb\xfc\xd1\xc2\xc1\xb7\xb3\xc9\xb8\xdf\xbe\xad\xca\xae"
	"\xb6\xfe\xd5\xc9\xa3\xac\xb7\xbd\xbe\xad\xb6\xfe\xca\xae\xcb\xc4"
	"\xd5\xc9\xcd\xe7\xca\xaf\xc8\xfd\xcd\xf2\xc1\xf9\xc7\xa7\xce\xe5"
	"\xb0\xd9\xc1\xe3\xd2\xbb\xbf\xe9\xa1\xa3\xe6\xb4\xbb\xca\xca\xcf"
	"\xd6\xbb\xd3\xc3\xc1\xcb\xc8\xfd\xcd\xf2\xc1\xf9\xc7\xa7\xce\xe5"
	"\xb0\xd9\xbf\xe9\xa3\xac\xd6\xbb\xb5\xa5\xb5\xa5\xca\xa3\xc1\xcb"
	"\xd2\xbb\xbf\xe9\xce\xb4\xd3\xc3\xa3\xac\xb1\xe3\xc6\xfa\xd4\xda"
	"\xb4\xcb\xc9\xbd\xc7\xe0\xb9\xa1\xb7\xe5\xcf\xc2\xa1\xa3",

	"\xcb\xad\xd6\xaa\xb4\xcb\xca\xaf\xd7\xd4\xbe\xad\xc1\xb6\xd6\xae"
	"\xba\xf3\xa3\xac\xc1\xe9\xd0\xd4\xd2\xd1\xcd\xa8\xa3\xac\xd2\xf2"
	"\xbc\xfb\xd6\xda\xca\xaf",

	"\xbe\xe3\xb5\xc3\xb2\xb9\xcc\xec\xa3\xac\xb6\xc0\xd7\xd4\xbc\xba"
	"\xce\xde\xb2\xc4\xb2\xbb\xbf\xb0\xc8\xeb\xd1\xa1\xa3\xac\xcb\xec"
	"\xd7\xd4\xd4\xb9\xd7\xd4\xcc\xbe",

	"\xa3\xac\xc8\xd5\xd2\xb9\xb1\xaf\xba\xc5\xb2\xd1\xc0\xa2\xa1\xa3"
	"\xd2\xbb\xc8\xd5\xa3\xac\xd5\xfd\xb5\xb1\xe0\xb5",

	"\xb5\xbf\xd6\xae\xbc\xca\xa3\xac\xb6\xed\xbc\xfb\xd2\xbb\xc9\xae"
	"\xd2\xbb\xb5\xc0\xd4\xb6\xd4\xb6\xb6\xf8\xc0\xb4\xa3\xac",

	"\xc9\xfa\xb5\xc3\xb9\xc7\xb8\xf1\xb2\xbb\xb7\xb2\xa3\xac\xb7\xe1"
	"\xc9\xf1\xe5\xc4\xd2\xec\xa3\xac\xcb\xb5\xcb\xb5\xd0\xa6\xd0\xa6"
	"\xc0\xb4\xd6\xc1\xb7\xe5\xcf\xc2\xa3\xac\xd7\xf8\xd3\xda\xca\xaf",

	"\xb1\xdf\xb8\xdf\xcc\xb8\xbf\xec\xc2\xdb\xa1\xa3\xcf\xc8\xca\xc7",

	"\xcb\xb5\xd0\xa9\xd4\xc6\xc9\xbd\xce\xed\xba\xa3\xc9\xf1\xcf\xc9"
	"\xd0\xfe\xbb\xc3\xd6\xae\xca\xc2\xa3\xac\xba\xf3\xb1\xe3\xcb\xb5"
	"\xb5\xbd",

	"\xba\xec\xb3\xbe\xd6\xd0\xc8\xd9\xbb\xaa\xb8\xbb\xb9\xf3\xa1\xa3"
	"\xb4\xcb\xca\xaf",

	"\xcc\xfd\xc1\xcb\xa3\xac\xb2\xbb\xbe\xf5\xb4\xf2\xb6\xaf\xb7\xb2"
	"\xd0\xc4\xa3\xac\xd2\xb2\xcf\xeb\xd2\xaa\xb5\xbd\xc8\xcb\xbc\xe4"
	"\xc8\xa5\xcf\xed\xd2\xbb\xcf\xed\xd5\xe2\xc8\xd9\xbb\xaa\xb8\xbb"
	"\xb9\xf3\xa3\xac\xb5\xab\xd7\xd4\xba\xde\xb4\xd6\xb4\xc0\xa3\xac"
	"\xb2\xbb\xb5\xc3\xd2\xd1\xa3\xac\xb1\xe3\xbf\xda\xcd\xc2\xc8\xcb"
	"\xd1\xd4\xa3\xac\xcf\xf2\xc4\xc7\xc9\xae\xb5\xc0\xcb\xb5\xb5\xc0"
	"\xa3\xba\xb4\xf3\xca\xa6\xa3\xac\xb5\xdc\xd7\xd3\xb4\xc0\xce\xef"
	"\xa3\xac\xb2\xbb\xc4\xdc\xbc\xfb\xc0\xf1\xc1\xcb\xa1\xa3\xca\xca"
	"\xce\xc5\xb6\xfe\xce\xbb\xcc\xb8\xc4\xc7\xc8\xcb\xca\xc0\xbc\xe4"
	"\xc8\xd9\xd2\xab\xb7\xb1\xbb\xaa\xa3\xac",

	"\xd0\xc4\xc7\xd0\xc4\xbd\xd6\xae\xa1\xa3\xb5\xdc\xd7\xd3\xd6\xca"
	"\xcb\xe4\xb4\xd6\xb4\xc0",

	"\xa3\xac\xd0\xd4\xc8\xb4\xc9\xd4\xcd\xa8\xa3\xac\xbf\xf6\xbc\xfb"
	"\xb6\xfe\xca\xa6\xcf\xc9\xd0\xce\xb5\xc0\xcc\xe5\xa3\xac\xb6\xa8"
	"\xb7\xc7\xb7\xb2\xc6\xb7\xa3\xac\xb1\xd8",

	"\xd3\xd0\xb2\xb9\xcc\xec\xbc\xc3\xca\xc0\xd6\xae\xb2\xc4\xa3\xac"
	"\xc0\xfb\xce\xef\xbc\xc3\xc8\xcb\xd6\xae\xb5\xc2\xa1\xa3\xc8\xe7"
	"\xc3\xc9\xb7\xa2\xd2\xbb\xb5\xe3\xb4\xc8\xd0\xc4\xa3\xac\xd0\xaf"
	"\xb4\xf8\xb5\xdc\xd7\xd3\xb5\xc3\xc8\xeb\xba\xec\xb3\xbe\xa3\xac"
	"\xd4\xda\xc4\xc7\xb8\xbb\xb9\xf3\xb3\xa1\xd6\xd0\xa3\xac\xce\xc2"
	"\xc8\xe1\xcf\xe7\xc0\xef"
};

typedef struct
{
	UInt16 charEncoding;
	const char** strings;
} QuotesInfoType;

static const QuotesInfoType kQuotesInfo[] =
{
	{ charEncodingPalmSJIS, kShiftJISQuotes },
	
	// All of the possible Traditional Chinese encodings.
	{ charEncodingBig5, kBig5Quotes },
	{ charEncodingBig5_HKSCS, kBig5Quotes },
	{ charEncodingBig5Plus, kBig5Quotes },
	{ charEncodingPalmBig5, kBig5Quotes },
	
	// All of the possible Simplified Chinese encodings.
	{ charEncodingGB2312, kGB2312Quotes },
	{ charEncodingGBK, kGB2312Quotes },
	{ charEncodingPalmGB, kGB2312Quotes }
};

/***********************************************************************
 *
 * FUNCTION:	GetFocusObject
 *
 * DESCRIPTION: Return whether the current form has the focus.
 *
 * CALLED BY:  here
 *
 * PARAMETERS:	none
 *
 * RETURNED:	TRUE if the form has a focus set and FALSE if not
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			roger	8/25/95	Initial Revision
 *			roger	11/27/95	Ignored not editable fields.
 *
 ***********************************************************************/
static FieldPtr GetFocusObject()
{
	FormPtr frm;
	UInt16 focusObj;
	FieldPtr textFieldP;


	// Pick a point within one of the current form's objects
	frm = FrmGetActiveForm ();

	// The active window will not be the active form
	// if a popup list of a menu is displayed.
	if ((! frm) || (FrmGetWindowHandle (frm) != WinGetActiveWindow ()) ||
		((focusObj = FrmGetFocus(frm)) == noFocus))
	{
		if (!frm)
			PRINTF ("--- GetFocusObject == NULL (FrmGetActiveForm () == NULL)");
		else if (FrmGetWindowHandle (frm) != WinGetActiveWindow ())
			PRINTF ("--- GetFocusObject == NULL (FrmGetWindowHandle () != WinGetActiveWindow ())");
		else
			PRINTF ("--- GetFocusObject == NULL (FrmGetFocus () == noFocus)");

		return NULL;
	}

	// Get the field.  If it's a table get it's field.
	if (FrmGetObjectType(frm, focusObj) == frmTableObj)
	{
		textFieldP = TblGetCurrentField((TablePtr) FrmGetObjectPtr(frm, focusObj));
		if (textFieldP == NULL)
		{
			PRINTF ("--- GetFocusObject == NULL (TblGetCurrentField () == NULL)");
			return NULL;
		}
	}
	else
	{
		textFieldP = (FieldPtr) FrmGetObjectPtr(frm, focusObj);

		if (textFieldP == NULL)
		{
			PRINTF ("--- GetFocusObject == NULL (FrmGetObjectPtr () == NULL)");
		}
	}

	return textFieldP;
}


/***********************************************************************
 *
 * FUNCTION:	IsFocus
 *
 * DESCRIPTION: Return whether the current form has the focus.
 *
 * CALLED BY:  EmGremlins.cp
 *
 * PARAMETERS:	none
 *
 * RETURNED:	TRUE if the form has a focus set and FALSE if not
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			roger	8/25/95	Initial Revision
 *			roger	11/27/95	Ignored not editable fields, broke out GetFocusObject
 *
 ***********************************************************************/
static int IsFocus()
{
	FieldPtr textFieldP;
	FieldAttrType attr;



	textFieldP = GetFocusObject();
	if (textFieldP == NULL)
	{
		PRINTF ("--- IsFocus == false (textFieldP == NULL)");
		return false;
	}

	// Now make sure that the field is editable.
	FldGetAttributes(textFieldP, &attr);
	if (!attr.editable)
	{
		PRINTF ("--- IsFocus == false (!attr.editable 0x%04X)", (uint32) *(uint16*) &attr);
		return false;
	}

	PRINTF ("--- IsFocus == true");
	return true;
}


/***********************************************************************
 *
 * FUNCTION:	SpaceLeftInFocus
 *
 * DESCRIPTION: Return the number of characters which can be added to 
 *	the object with the focus.
 *
 * CALLED BY:  EmGremlins.cp
 *
 * PARAMETERS:	none
 *
 * RETURNED:	The number of characters which can be added to 
 *	the object with the focus.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			roger	11/27/95	Initial Revision
 *
 ***********************************************************************/
static int SpaceLeftInFocus()
{
	FieldPtr textFieldP;
	FieldAttrType attr;



	textFieldP = GetFocusObject();
	if (textFieldP == NULL)
		return 0;

	// Now make sure that the field is editable.
	FldGetAttributes(textFieldP, &attr);
	if (!attr.editable)
		return 0;



	return FldGetMaxChars(textFieldP) - FldGetTextLength(textFieldP);
}


/***********************************************************************
 *
 * FUNCTION:	FakeLocalMovement
 *
 * DESCRIPTION: Generate a random point within the vicinity of the last
 *						point.
 *
 * CALLED BY:  EmGremlins.cp
 *
 * PARAMETERS:	currentX -	the new x-coordinate of a pen movement.
 *					currentY -	the new y-coordinate of a pen movement.
 * 				lastX -		the last x-coordinate of a pen movement.
 *					lastY -		the last y-coordinate of a pen movement.					
 *
 * RETURNED:	Nothing.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			David	8/15/95	Initial Revision
 *
 ***********************************************************************/
static void FakeLocalMovement(Int16* currentX, Int16* currentY, Int16 lastX, Int16 lastY)
{
	Int16 winWidth, winHeight;

	*currentX = lastX + (randN(FntLineHeight() * 2) - FntLineHeight());
	*currentY = lastY + (randN(FntLineHeight() * 2) - FntLineHeight());	// FntLineHeight

	// Note: This code was incorrectly using Hwr Display constants to determine screen size.
	//			The approved of method is to use the size of the current window, which may also be
	//			the screen, however, this may not be correct for what gremilns needs to do.
	//			Something needs to be done for now just to get it to work. BRM 6/30/99
	WinGetDisplayExtent(&winWidth, &winHeight);
	
	// Clip to screen bounds
	//
	// KAAR: In original Gremlins, the point was pinned to [-1...winWidth/Height].
	// That doesn't seem right, especially since -1 is used as a pen up indicator.
	// So now I clip to [0...winWidth/Height).

	if (*currentX < 0) *currentX = 0;
	if (*currentX >= winWidth) 
		*currentX = winWidth - 1;

	if (*currentY < 0) *currentY = 0;
	if (*currentY >= winHeight) 
		*currentY = winHeight = 1;
}


/***********************************************************************
 *
 * FUNCTION:	RandomScreenXY
 *
 * DESCRIPTION: Generate a random point.
 *
 * CALLED BY:  EmGremlins.cp
 *
 * PARAMETERS:	x -	the x-coordinate of a pen movement.
 *					y -	the y-coordinate of a pen movement.
 *
 * RETURNED:	Nothing.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			David	8/15/95	Initial Revision
 *
 ***********************************************************************/
static void RandomScreenXY(Int16* x, Int16* y)
{
#ifdef __DEBUGGER_APPLICATION__

	// Since the WinGetDisplayExtent() trap doesn't exist in all versions
	// of the Palm OS, the debugger can't rely on it being around.  So,
	// for the debugger version of this build, we explicitely set the
	// old screen width.
	//
	// DOLATER:  Figure out a way to determine if the WinGetDisplayExtent()
	// is around.  If it is, then call it.  Otherwise, revert to the
	// old constants.
	//
	#define hwrDisplayWidth 	160
	#define hwrDisplayHeight	160

	*x = randN(hwrDisplayWidth);
	*y = randN(hwrDisplayHeight);
	
#else

	Int16 winWidth, winHeight;

	WinGetDisplayExtent(&winWidth, &winHeight);

	*x = randN(winWidth);
	*y = randN(winHeight);

#endif
}


/***********************************************************************
 *
 * FUNCTION:	RandomWindowXY
 *
 * DESCRIPTION: Generate a random point.
 *
 * CALLED BY:  EmGremlins.cp
 *
 * PARAMETERS:	x -	the x-coordinate of a pen movement.
 *					y -	the y-coordinate of a pen movement.
 *
 * RETURNED:	Nothing.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			Keith	11/11/99	Initial Revision
 *
 ***********************************************************************/
static void RandomWindowXY(Int16* x, Int16* y)
{
	// Every so often tap anywhere on the screen (10%)
	if ((randN(10) == 1) || (WinGetActiveWindow () == NULL))
	{
		RandomScreenXY(x, y);
	}
	else
	{
		// We want to tap in the active window.  However, WinGetWindowBounds
		// works against the draw window, which is not necessarily the active
		// window.  Make it so.

		WinHandle	oldDraw = WinSetDrawWindow (WinGetActiveWindow());

		RectangleType	bounds;
		WinGetWindowBounds (&bounds);

		*x = bounds.topLeft.x + randN(bounds.extent.x);
		*y = bounds.topLeft.y + randN(bounds.extent.y);

		WinSetDrawWindow (oldDraw);
	}
}


/***********************************************************************
 *
 * FUNCTION:	FakeEventXY
 *
 * DESCRIPTION: Generate random (x,y) coordindates to produce an event.
 *
 * CALLED BY:  EmGremlins.cp
 *
 * PARAMETERS:	x -	x-coordinate of a pen down.
 *					y -	y-coordinate of a pen down.
 *
 * RETURNED:	
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			David	08/15/95	Initial Revision
 *			kwk	07/17/98	10% of the time, generate tap in silkscreen btn.
 *			kwk	08/04/99	Cranked percentage down to 5%, since otherwise
 *								we're always just bringing up the keyboard or
 *								the Find form.
 *
 ***********************************************************************/
static void FakeEventXY(Int16* x, Int16* y)
{
	FormPtr frm;
	Int16 objIndex;
	RectangleType bounds;

#ifndef forSimulator
	// Every so often tap anywhere on the screen (2%)
	if (randN(100) < 2)
		{
		RandomScreenXY(x, y);
		return;
		}
#endif

	// Pick a point within one of the current form's objects
	frm = FrmGetActiveForm ();

	// First see if we want to generate a tap in a silkscreen button. If not, then
	// generate a point in the draw window if there no active form, or the active form 
	// is not the the active window.. The active window will not be the active form
	// if a popup list of a menu is displayed.
	//
	// Also do this if there aren't any objects in the form.
	
	if (randN(20) == 1) {
		UInt16 numButtons;
		const PenBtnInfoType* buttonListP = EvtGetPenBtnList(&numButtons);

		const size_t	size = EmAliasPenBtnInfoType<PAS>::GetSize ();
		emuptr			addr = EmMemPtr(buttonListP) + randN(numButtons) * size;

		EmAliasPenBtnInfoType<PAS>	button (addr);
		RectangleType randButtonRect;
		randButtonRect.topLeft.x = button.boundsR.topLeft.x;
		randButtonRect.topLeft.y = button.boundsR.topLeft.y;
		randButtonRect.extent.x = button.boundsR.extent.x;
		randButtonRect.extent.y = button.boundsR.extent.y;
		
		*x = randButtonRect.topLeft.x + (randButtonRect.extent.x / 2);
		*y = randButtonRect.topLeft.y + (randButtonRect.extent.y / 2);
	} else if ((frm == NULL) || 
		(FrmGetWindowHandle (frm) != WinGetActiveWindow ()))
	{
		RandomWindowXY (x, y);
	}
	else
	{
		// Generate a point in an one of the form's objects that we expect
		// can do something with the point (i.e. labels are ignored).

#ifdef forSimulator
		do 
		{
			objIndex = randN(numObjects);
			switch (FrmGetObjectType (frm, objIndex))
			{
				case frmBitmapObj:
				case frmLineObj:
				case frmFrameObj:
				case frmRectangleObj:
				case frmLabelObj:
				case frmTitleObj:
				case frmPopupObj:
					// do nothing for these
					objIndex = -1;
					break;
				
				default:
					FrmGetObjectBounds (frm, objIndex, &bounds);
					*x = bounds.topLeft.x + randN(bounds.extent.x);
					*y = bounds.topLeft.y + randN(bounds.extent.y);
					WinWindowToDisplayPt(x, y);

					if (	*x < -1 || *x > 1000 || 
							*y < -1 || *y > 1000)
						ErrDisplay("Invalid point made");

					break;
			}	// end switch
		} while (objIndex == -1);	// don't leave until we found a useful object

#else
		// Get the list of objects we can click on.

		vector<UInt16>	okObjects;
		::CollectOKObjects (frm, okObjects);

		// If there are no such objects, just generate a random point.

		if (okObjects.size () == 0)
		{
			RandomWindowXY (x, y);
		}

		// If there are such objects, pick one and click on it.

		else
		{
			objIndex = okObjects[randN(okObjects.size ())];

			FrmGetObjectBounds (frm, objIndex, &bounds);

			Int16 winWidth, winHeight;
			::WinGetDisplayExtent(&winWidth, &winHeight);

			if (bounds.topLeft.x < 0)
				bounds.topLeft.x = 0;

			if (bounds.topLeft.y < 0)
				bounds.topLeft.y = 0;

			if (bounds.extent.x > winWidth - bounds.topLeft.x - 1)
				bounds.extent.x = winWidth - bounds.topLeft.x - 1;

			if (bounds.extent.y > winHeight - bounds.topLeft.y - 1)
				bounds.extent.y = winHeight - bounds.topLeft.y - 1;

			*x = bounds.topLeft.x + randN(bounds.extent.x);
			*y = bounds.topLeft.y + randN(bounds.extent.y);

			WinWindowToDisplayPt(x, y);
		}
#endif
	}	// end else
}


/************************************************************
 *
 * FUNCTION: 	 GremlinsSendEvent
 *
 * DESCRIPTION: Send a synthesized event to the device if it's 
 *              idle.
 *
 * PARAMETERS:  nothing
 *
 * RETURNS:     nothing
 * 
 * CALLED BY:   the debugger's console object
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	11/2/95	Created.
 *			dia	8/26/98	Added try/catch block.
 *
 *************************************************************/
#ifdef forSimulator

void GremlinsSendEvent (void)
{
//	int					tick;
//	Boolean					idle;
//	LowMemType*				lowMemP = (LowMemType*)PilotGlobalsP;
//	SysEvtMgrGlobalsPtr		sysEvtMgrGlobalsP;
	
	if (!TheGremlinsP->IsInitialized() || !StubGremlinsIsOn())
		return;

	ErrTry
		{
#if EMULATION_LEVEL == EMULATION_WINDOWS
		TheGremlinsP->GetFakeEvent();
#else
//		This makes it go faster, but it is much less careful (not as reproducable).
//		The code was left here for future reference / fixing.

//		// If accessing remote device, low memory is at 0...
//		#if MEMORY_TYPE == MEMORY_REMOTE 
//		lowMemP = (LowMemType*)0;
//		#endif
//		
//		// Find out if the device is idle.
//		tick = StubTimGetTicks();
//		if ((tick - IdleTimeCheck) >= 0)
//			{
//			sysEvtMgrGlobalsP = (SysEvtMgrGlobalsPtr)ShlDWord(&lowMemP->fixed.globals.sysEvtMgrGlobalsP);
//			idle = ShlByte(&sysEvtMgrGlobalsP->idle);
//			if (!idle) 
//				{
//				IdleTimeCheck = tick + 12;  // 10 times a second
//				return;
//				}
//			else 
//				// Clear the idle bit so the the device will not send us another idle packet.
//				// Send an event
//				IdleTimeCheck = 0;
//
			TheGremlinsP->GetFakeEvent();
//			}
#endif
		}
	ErrCatch (inErr)
		{
		if (inErr != -1)
			{
			char text[256];
			UInt32 step;

			// Print error & stop...
			TheGremlinsP->Status (NULL, &step, NULL);
			sprintf(text, "Error #%lx occurred while sending.  Gremlins at %ld.  Stopping.\n", inErr, step);
			DbgMessage(text);
			StubAppGremlinsOff();
			}
		}
	ErrEndCatch
}

#endif


/************************************************************
 *
 * FUNCTION: 	 GremlinsProcessPacket
 *
 * DESCRIPTION: Send a synthesized event to the device if it's 
 *              idle.
 *
 * PARAMETERS:  bodyP - pointer to Gremlins packet from device.
 *
 * RETURNS:     nothing
 * 
 * CALLED BY:   the debugger's console object
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	11/2/95	Created.
 *			dia	8/26/98	Added try/catch block.
 *
 *************************************************************/
#ifdef forSimulator

void GremlinsProcessPacket (void* bodyParamP)
{
	UInt8					flags;
	SysPktGremlinsCmdType*	bodyP = (SysPktGremlinsCmdType*)bodyParamP;
	LowMemType*				lowMemP = (LowMemType*)PilotGlobalsP;
	SysEvtMgrGlobalsPtr		sysEvtMgrGlobalsP;

	if (!TheGremlinsP->IsInitialized())
		return;
		
	ErrTry
		{
		// See which action code got sent us
		if (bodyP->action == sysPktGremlinsIdle) {

			// If accessing remote device, low memory is at 0...
			#if MEMORY_TYPE == MEMORY_REMOTE 
			lowMemP = (LowMemType*)0;
			#endif

			// Clear the idle bit so the the device will not send us another idle packet.
			// Send an event
			TheGremlinsP->GetFakeEvent();
			
			// Turn the idle bit back on.
			sysEvtMgrGlobalsP = (SysEvtMgrGlobalsPtr)ShlDWord((void *)&lowMemP->fixed.globals.sysEvtMgrGlobalsP);
			flags = ShlByte((void *)&sysEvtMgrGlobalsP->gremlinsFlags);
			flags |= grmGremlinsIdle;
			ShlWriteMem (&flags, (UInt32)&sysEvtMgrGlobalsP->gremlinsFlags, sizeof(UInt8));

//			flags = ShlByte(&sysEvtMgrGlobalsP->gremlinsFlags);
//			ErrFatalDisplayIf (!(flags & grmGremlinsIdle), "Invalid flags");
			}
			
		else
			ErrDisplay("Invalid action code");
		}
	ErrCatch (inErr)
		{
		if (inErr != -1)
			{
			char text[256];
			UInt32 step;
			
			// Print error & stop...
			TheGremlinsP->Status (NULL, &step, NULL);
			sprintf(text, "Error #%lx occurred while processing.  Gremlins at %ld.  Stopping.\n", inErr, step);
			DbgMessage(text);
			StubAppGremlinsOff();
			}
		}
	ErrEndCatch
}
#endif


// ---------------------------------------------------------------------------
//		• operator >> (EmStream&, DatabaseInfo&)
// ---------------------------------------------------------------------------

EmStream& operator >> (EmStream& inStream, DatabaseInfo& outInfo)
{
	inStream >> outInfo.creator;
	inStream >> outInfo.type;
	inStream >> outInfo.version;

	inStream >> outInfo.dbID;
	inStream >> outInfo.cardNo;
	inStream >> outInfo.modDate;
	inStream >> outInfo.dbAttrs;
	inStream >> outInfo.name;

	outInfo.dbName[0] = 0;

	return inStream;
}


// ---------------------------------------------------------------------------
//		• operator << (EmStream&, const DatabaseInfo&)
// ---------------------------------------------------------------------------

EmStream& operator << (EmStream& inStream, const DatabaseInfo& inInfo)
{
	LocalID		dbID = 0;
	UInt16 		cardNo = 0;
	UInt32		modDate = 0;
	UInt16		dbAttrs = 0;
	char		name[dmDBNameLength] = {0};

	inStream << inInfo.creator;
	inStream << inInfo.type;
	inStream << inInfo.version;

	// I have no idea why dummy values are written out for these fields.
	// But it sure causes us problems later when we need to access them!
	// See the code in Hordes::GetAppList that needs to patch up the missing
	// information.

	inStream << dbID;
	inStream << cardNo;
	inStream << modDate;
	inStream << dbAttrs;
	inStream << name;

	return inStream;
}


// ---------------------------------------------------------------------------
//		• operator >> (EmStream&, AppPreferences::GremlinInfo&)
// ---------------------------------------------------------------------------

EmStream& operator >> (EmStream& inStream, GremlinInfo& outInfo)
{
	bool	dummy;

	inStream >> outInfo.fNumber;
	inStream >> outInfo.fSteps;
	inStream >> outInfo.fAppList;

	inStream >> dummy;				// forward compatibility: this field was
									// fContinuePastWarnings

	inStream >> dummy;
	inStream >> dummy;

	return inStream;
}


// ---------------------------------------------------------------------------
//		• operator << (EmStream&, const AppPreferences::GremlinInfo&)
// ---------------------------------------------------------------------------

EmStream& operator << (EmStream& inStream, const GremlinInfo& inInfo)
{
	bool	dummy = false;

	inStream << inInfo.fNumber;
	inStream << inInfo.fSteps;
	inStream << inInfo.fAppList;

	inStream << dummy;				// backward compatibility: this field was
									// fContinuePastWarnings

	inStream << dummy;
	inStream << dummy;

	return inStream;
}


/************************************************************
 *
 * FUNCTION: Default Constructor
 *
 * DESCRIPTION: Finds the key probablilities sum.
 *
 * PARAMETERS: None.
 *
 * RETURNS: Nothing.
 * 
 * CALLED BY: main() of EmEmulatorApp.cp
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			David	08/01/95	Created.
 *			kwk	07/17/98	Moved key probability init into run-time
 *								section.
 *
 *************************************************************/
Gremlins::Gremlins()
{
	keyProbabilitiesSum = 0;
	inited = false;
#ifdef forSimulator
	number = -1;
#else
	number = ~0;
#endif
}

/************************************************************
 *
 * FUNCTION: Destructor
 *
 * DESCRIPTION: Any necessary deallocation or cleanup.
 *
 * PARAMETERS: None.
 *
 * RETURNS: Nothing.
 * 
 * CALLED BY: main() of EmEmulatorApp.cp
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			David	8/1/95	Created.
 *
 *************************************************************/
Gremlins::~Gremlins()
{
}

/************************************************************
 *
 * FUNCTION: IsInitialized
 *
 * DESCRIPTION: Returns whether or not Gremlins has be initialized.
 *
 * PARAMETERS: None.
 *
 * RETURNS: TRUE - has been initialized, FALSE - has not been initialized.
 * 
 * CALLED BY: FindCommandStatus() in EmEmulatorApp.cp.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			David	8/1/95	Created.
 *
 *************************************************************/
Boolean Gremlins::IsInitialized() const
{
	return inited;
}


/************************************************************
 *
 * FUNCTION: Initialize
 *
 * DESCRIPTION: Initialize the gremlins class.
 *
 * PARAMETERS: None.
 *
 * RETURNS: Nothing.
 * 
 * CALLED BY: ObeyCommand() in EmEmulatorApp.cp.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			David	8/1/95	Created.
 *
 *************************************************************/
void Gremlins::Initialize(UInt16 newNumber, UInt32 untilStep, UInt32 finalVal)
{
#ifndef forSimulator
	gIntlMgrExists = -1;
	::ResetCalibrationInfo();
	::ResetClocks ();
	EmLowMem_SetGlobal (hwrBatteryLevel, 255);
	EmLowMem_SetGlobal (hwrBatteryPercent, 100);
#endif

	counter = 0;
	until = untilStep;
	finalUntil = finalVal;
#ifndef forSimulator
	saveUntil = until;
#endif
	catchUp = false;
	needPenUp = false;
	charsToType[0] = '\0';
	inited = true;
#ifdef forSimulator
	// removed...test will always fail because newNumber is unsigned...
//	if (newNumber == -1)
//		newNumber = INITIAL_SEED;
		//newNumber = clock() % MAX_SEED_VALUE + 1;	
#endif
	number = newNumber;
	srand(number);

	IdleTimeCheck = 0;

	// Update menus (needed when init. called from console)
	StubAppGremlinsOn ();
}



/************************************************************
 *
 * FUNCTION: Reset
 *
 * DESCRIPTION: Un-initialize the gremlins class.
 *
 * PARAMETERS: None.
 *
 * RETURNS: Nothing.
 * 
 *************************************************************/
void Gremlins::Reset(void)
{
	inited = false;
}


/************************************************************
 *
 * FUNCTION: New
 *
 * DESCRIPTION: Start new Gremlins
 *
 * PARAMETERS: GremlinInfo info
 *
 * RETURNS: Nothing.
 * 
 *************************************************************/
void
Gremlins::New (const GremlinInfo& info)
{
	if (LogGremlins ())
	{
		string	templ = Platform::GetString (kStr_GremlinStarted);
		LogAppendMsg (templ.c_str (), (int) info.fNumber, info.fSteps);
	}

	// If needed, switch to an "approved" application.
	// This code roughly follows that in AppsViewSwitchApp in Launcher.

	if (info.fAppList.size () > 0)
	{
		// Switch to the first on the list.
		
		DatabaseInfo	dbInfo = *(info.fAppList.begin ());

		//---------------------------------------------------------------------
		// If this is an executable, call SysUIAppSwitch
		//---------------------------------------------------------------------
		if (::IsExecutable (dbInfo.type, dbInfo.creator, dbInfo.dbAttrs))
		{
			Err err = ::SysUIAppSwitch (dbInfo.cardNo, dbInfo.dbID,
							sysAppLaunchCmdNormalLaunch, NULL);
			Errors::ThrowIfPalmError (err);
		}

		//---------------------------------------------------------------------
		// else, this must be a launchable data database. Find it's owner app
		//  and launch it with a pointer to the data database name.
		//---------------------------------------------------------------------
		else
		{
			DmSearchStateType	searchState;
			UInt16				cardNo;
			LocalID				dbID;
			Err err = ::DmGetNextDatabaseByTypeCreator (true, &searchState, 
						sysFileTApplication, dbInfo.creator, 
						true, &cardNo, &dbID);
			Errors::ThrowIfPalmError (err);

			// Create the param block
			emuptr	cmdPBP = EmMemPtr(::MemPtrNew (sizeof (SysAppLaunchCmdOpenDBType)));
			Errors::ThrowIfNULL (cmdPBP);

			// Fill it in
			::MemPtrSetOwner (EmMemFakeT<MemPtr>(cmdPBP), 0);
			EmMemPut16 (cmdPBP + offsetof (SysAppLaunchCmdOpenDBType, cardNo), dbInfo.cardNo);
			EmMemPut32 (cmdPBP + offsetof (SysAppLaunchCmdOpenDBType, dbID), dbInfo.dbID);

			// Switch now
			err = ::SysUIAppSwitch (cardNo, dbID, sysAppLaunchCmdOpenDB, EmMemFakeT<MemPtr>(cmdPBP));
			Errors::ThrowIfPalmError (err);
		}
	}

	this->Initialize (info.fNumber, info.fSteps, info.fFinal);

	gremlinStartTime = Platform::GetMilliseconds ();

	// Make sure the app's awake.  Normally, we post events on a patch to
	// SysEvGroupWait.  However, if the Palm device is already waiting,
	// then that trap will never get called.  By calling EvtWakeup now,
	// we'll wake up the Palm device from its nap.

	Errors::ThrowIfPalmError (EvtWakeup ());

	Hordes::TurnOn(true);

	if (info.fSaveFrequency != 0)
	{
		EmAssert (gSession);
		gSession->ScheduleAutoSaveState ();
	}
}



/************************************************************
 *
 * FUNCTION: Save
 *
 * DESCRIPTION: Saves Gremlin Info
 *
 * PARAMETERS: SessionFile to write to.
 *
 * RETURNS: Nothing.
 *
 *************************************************************/
void
Gremlins::Save (SessionFile& f)
{
	gremlinStopTime = Platform::GetMilliseconds ();

	const int	kCurrentVersion = 2;

	Chunk			chunk;
	EmStreamChunk	s (chunk);

	Bool hordesIsOn = Hordes::IsOn ();

	s << kCurrentVersion;

	s << keyProbabilitiesSum;
	s << lastPointY;
	s << lastPointX;
	s << lastPenDown;
	s << number;
	s << counter;
	s << finalUntil;
	s << saveUntil;
	s << inited;
	s << catchUp;
	s << needPenUp;
	s << charsToType;

	s << (hordesIsOn != false);
	s << gremlinStartTime;
	s << gremlinStopTime;

	s << gGremlinNext;

	GremlinInfo info;

	info.fAppList = gGremlinAppList;
	info.fNumber = number;
	info.fSaveFrequency = gGremlinSaveFrequency;
	info.fSteps = until;
	info.fFinal = finalUntil;

	s << info;

	f.WriteGremlinInfo (chunk);
}


/************************************************************
 *
 * FUNCTION: Load
 *
 * DESCRIPTION: Loads Gremlin Info
 *
 * PARAMETERS: SessionFile to read from.
 *
 * RETURNS: TRUE if a Gremlin state have been loaded and it
 *			is ON.
 *			FALSE otherwise.
 *
 *************************************************************/
Boolean
Gremlins::Load (SessionFile& f)
{
	Chunk	chunk;
	bool	fHordesOn;

	if (f.ReadGremlinInfo (chunk))
	{
		int			version;
		EmStreamChunk	s (chunk);

		s >> version;

		if (version >= 1)
		{
			s >> keyProbabilitiesSum;
			s >> lastPointY;
			s >> lastPointX;
			s >> lastPenDown;
			s >> number;
			s >> counter;
			s >> finalUntil;
			s >> saveUntil;
			s >> inited;
			s >> catchUp;
			s >> needPenUp;
			s >> charsToType;

			s >> fHordesOn;

			s >> gremlinStartTime;
			s >> gremlinStopTime;

			s >> gGremlinNext;

			// sync until to finalUntil

			until = finalUntil;

			// Patch up the start and stop times.

			int32	delta = gremlinStopTime - gremlinStartTime;
			gremlinStopTime = Platform::GetMilliseconds ();
			gremlinStartTime = gremlinStopTime - delta;

			// Reset keyProbabilitiesSum to zero so that it gets
			// recalculated.  Writing it out to the session file
			// was a bad idea.  The value written out may not be
			// appropriate for the version of Poser reading it in.

			keyProbabilitiesSum = 0;
		}

		if (version >= 2)
		{
			GremlinInfo	info;

			s >> info;

			Preference<GremlinInfo>	pref (kPrefKeyGremlinInfo);
			pref = info;
		}
	}

	return fHordesOn;
}


/************************************************************
 *
 * FUNCTION: Status
 *
 * DESCRIPTION: Return the gremlin number and counter.
 *
 * PARAMETERS: None.
 *
 * RETURNS: Nothing.
 * 
 * CALLED BY: DoGremlins() in ShellCmdSys.cpp.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			roger	8/4/95	Created.
 *			dia	9/1/98	Allows for NULL parameters.
 *
 *************************************************************/
void Gremlins::Status(UInt16 *currentNumber, UInt32 *currentStep, 
	UInt32 *currentUntil)
{
	if (currentNumber) *currentNumber = number;
	if (currentStep) *currentStep = counter;
	if (currentUntil) *currentUntil = until;
}


/************************************************************
 *
 * FUNCTION: SetSeed
 *
 * DESCRIPTION: Allows the user to set the seed to be used.
 *
 * PARAMETERS: newSeed -		the new value of the seed.
 *
 * RETURNS: TRUE - seed value set to new seed, FALSE - value not set.
 * 
 * CALLED BY: Uncalled. (to be called from Debug Console)
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			David	8/2/95	Created.
 *
 *************************************************************/
Boolean Gremlins::SetSeed(UInt32 newSeed)
{
	if (newSeed > MAX_SEED_VALUE)
		return false;
	else
	{
		number = (UInt16) newSeed;
		srand(number);
		return true;
	}
}

/************************************************************
 *
 * FUNCTION: SetUntil
 *
 * DESCRIPTION: Allows the user to set the until value to be used.
 *
 * PARAMETERS: newUntil -		the new value of until.
 *
 * RETURNS: Nothing.
 * 
 * CALLED BY: Hordes::Step
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			David	8/2/95	Created.
 *
 *************************************************************/
void Gremlins::SetUntil(UInt32 newUntil)
{
	until = newUntil;
#ifndef forSimulator
	saveUntil = until;
#endif
}

/************************************************************
 *
 * FUNCTION: RestoreFinalUntil
 *
 * DESCRIPTION: Restores the original max gremlins limit.
 *
 * CALLED BY: Hordes::Resume
 *
 *************************************************************/

void Gremlins::RestoreFinalUntil (void)
{
	until = finalUntil;
}

/************************************************************
 *
 * FUNCTION: Step
 *
 * DESCRIPTION: Allows Gremlins to go on step further then its
 *						set maximum.
 *
 * PARAMETERS: None.
 *
 * RETURNS: Nothing.
 * 
 * CALLED BY: ObeyCommand() in EmEmulatorApp.cp.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			David	8/1/95	Created.
 *
 *************************************************************/
void Gremlins::Step()
{
#ifndef forSimulator
	saveUntil = until;
#endif
	until = counter + 1;
}




/************************************************************
 *
 * FUNCTION: Resume
 *
 * DESCRIPTION: Resumes Gremlin
 *
 * PARAMETERS: None.
 *
 * RETURNS: Nothing.
 * 
 *************************************************************/
void
Gremlins::Resume (void)
{
	gremlinStartTime = Platform::GetMilliseconds () - (gremlinStopTime - gremlinStartTime);

	// Make sure we're all on the same page...
	::ResetCalibrationInfo ();

	// Make sure the app's awake.  Normally, we post events on a patch to
	// SysEvGroupWait.  However, if the Palm device is already waiting,
	// then that trap will never get called.  By calling EvtWakeup now,
	// we'll wake up the Palm device from its nap.

	Errors::ThrowIfPalmError (EvtWakeup ());
}




/************************************************************
 *
 * FUNCTION: Stop
 *
 * DESCRIPTION: Stops Gremlin
 *
 * PARAMETERS: None.
 *
 * RETURNS: Nothing.
 * 
 *************************************************************/
void
Gremlins::Stop (void)
{
	if (Hordes::IsOn())
	{
		Hordes::TurnOn(false);
		gremlinStopTime = Platform::GetMilliseconds ();

		unsigned short	number;
		unsigned int	step;
		unsigned int	until;
		this->Status (&number, &step, &until);

		if (LogGremlins ())
		{
			string	templ = Platform::GetString (kStr_GremlinEnded);
			LogAppendMsg (templ.c_str (),
				(int) number, step, until, (gremlinStopTime - gremlinStartTime));
		}

		LogDump ();
	}
}


/************************************************************
 *
 * FUNCTION: SendCharsToType
 *
 * DESCRIPTION: Send a char to the emulator if any are pending.
 *
 * PARAMETERS: None.
 *
 * RETURNS: 	true if a char was sent.
 * 
 * CALLED BY: 	GetFakeEvent
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			roger	10/04/95	Created.
 *			kwk	07/28/98	Queue double-byte characters correctly.
 *
 *************************************************************/
Boolean Gremlins::SendCharsToType()
{
	if (charsToType[0] != '\0')
	{
		WChar theChar;
		UInt16 charSize = TxtGetNextChar(charsToType, 0, &theChar);
		EmEventPlayback::RecordKeyEvent (theChar, 0, 0);
		StubAppEnqueueKey(theChar, 0, 0);
		PRINTF ("--- Gremlin #%d Gremlins::SendCharsToType: key = %d", number, theChar);
		strcpy(&charsToType[0], &charsToType[charSize]);
		return true;
	}

	return false;
}


/************************************************************
 *
 * FUNCTION: GetFakeEvent
 *
 * DESCRIPTION: Make a phony event for gremlin mode.
 *
 * PARAMETERS:  None
 *
 * RETURNS: TRUE if a key or point was enqueued, FALSE otherwise.
 * 
 * CALLED BY: TDEProcessMacEvents in EmEmulatorEvents.cp.
 *
 * REVISION HISTORY:
 *	06/01/95	rsf	Created by Roger Flores.
 *	07/31/95	David Moved to emulator level.
 *	08/28/98	kwk	Removed usage of TxtCharIsVirtual macro.
 *	07/03/99	kwk	Set command bit for page up/page down keydown
 *					events, since these are virtual (to match Graffiti behavior).
 *	06/04/01	kwk	Add support for Big-5 char encoding (Trad. Chinese).
 *
 *************************************************************/
Boolean Gremlins::GetFakeEvent()
{
	int chance;
	int i;
	int spaceLeft;
	PointType pen;
	const char *quote;

	PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: Entering", number);

	if (! inited)
	{
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: not initialized; leaving", number);
		return false;
	}

	// check to see if Gremlins has produced its max. # of "events."
	if (counter > until)
	{
		StubAppGremlinsOff ();
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: End of Days; leaving", number);
		return false;
	}

	// Added - during Gremlin runs, we found that the timeout
	// could get set to 30 seconds and that a Gremlin may type
	// characters for more than 30 seconds at a time.  EvtEnqueueKey
	// doesn't reset the event timer, so it was possible for the
	// device to go to sleep, even when typing was occuring.

	::EvtResetAutoOffTimer ();

	// check to see if the event loop needs time to catch up.
	if (catchUp)
	{
		EmEventPlayback::RecordNullEvent ();
		catchUp = false;
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: playing catchup; leaving", number);
		return false;
	}
#ifdef forSimulator
	counter++;
#endif

	// if no control object was entered, return a pen up.
	if (needPenUp)
	{
		pen.x = -1;
		pen.y = -1;
		lastPointX = pen.x;
		lastPointY = pen.y;
		lastPenDown = false;
		needPenUp = false;
		EmEventPlayback::RecordPenEvent (pen);
		StubAppEnqueuePt(&pen);
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: posted pen up; leaving", number);
		return true;
	}

	// If we've queued up a quote string, and there are still characters to
	// send, do so now.

	if (SendCharsToType())
	{
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: sent chars to type (1); leaving", number);
		return true;
	}

	chance = randPercent;

	// Now fake an input
	if ((chance < KEY_DOWN_EVENT_WITHOUT_FOCUS_CHANCE)
	 || (chance < KEY_DOWN_EVENT_WITH_FOCUS_CHANCE && IsFocus()))
	{
		if ((randPercent < TYPE_QUOTE_CHANCE) && IsFocus())
		{
			const char** quoteArray = kAsciiQuotes;
			
			// 80% of the time we'll use text that's appropriate for the device's
			// character encoding. The other 20%, we'll use 7-bit ASCII.
			if (randN(10) < 8)
			{
				UInt32 encoding;
				if (FtrGet(sysFtrCreator, sysFtrNumEncoding, &encoding) != errNone)
				{
					encoding = charEncodingPalmLatin;
				}
				
				for (UInt16 i = 0; i < sizeof(kQuotesInfo) / sizeof(QuotesInfoType); i++)
				{
					if (kQuotesInfo[i].charEncoding == encoding)
					{
						quoteArray = kQuotesInfo[i].strings;
						break;
					}
				}
			}

			quote = quoteArray[randN(NUM_OF_QUOTES)];
			strcat(charsToType, quote);
			
			// The full field functionality doesn't need to be tested much
			// If charsToType is more than the space remaining in the current
			// field, then for each char past the full point give 1/3 chance to
			// stop at that char.
			spaceLeft = SpaceLeftInFocus();
			if (strlen(charsToType) > (size_t) spaceLeft) {
				UInt32 charStart, charEnd;
				TxtCharBounds(charsToType, spaceLeft, &charStart, &charEnd);
				i = charStart;
				while (charsToType[i] != '\0') {
					if (randPercent < 33) {
						charsToType[i] = '\0';
						break;
					} else {
						i += TxtNextCharSize(charsToType, i);
					}
				}
			}

			Bool	result = SendCharsToType ();

			if (!result)
				EmEventPlayback::RecordNullEvent ();

			PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: sent chars to type (2); leaving", number);
			return result;
			}
		else
			{
			if (keyProbabilitiesSum == 0) {
				for (Int16 i = 0; i < NUM_OF_KEYS; i++) {
					if ((i > 0x00FF)
					|| ((TxtByteAttr(i) & byteAttrSingle) != 0)) {
						keyProbabilitiesSum += chanceForKey[i];
					}
				}
			}

			chance = randN(keyProbabilitiesSum);

			// Skip chars which cannot be single-byte, since we don't want to
			// generate bogus high-byte values.
			
			for (i = 0; i < NUM_OF_KEYS; i++) {
				if ((i < 0x0100)
				&& ((TxtByteAttr(i) & byteAttrSingle) == 0)) {
					continue;
				} else if (chance < chanceForKey[i]) {
					break;
				} else {
					chance -= chanceForKey[i];
				}
			}

			// There's a fractional chance for a greater number to be generated.  If
			// so we do nothing.
			if (i >= NUM_OF_KEYS)
				return false;
			
			if ((i > 255) || (i == chrPageUp) || (i == chrPageDown))
			{
				EmEventPlayback::RecordKeyEvent (i, 0, commandKeyMask);
				StubAppEnqueueKey(i, 0, commandKeyMask);
			}
			else
			{
				EmEventPlayback::RecordKeyEvent (i, 0, 0);
				StubAppEnqueueKey(i, 0, 0);
			}

			PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: posted key = %d; leaving", number, i);
			return true;
			}
	}		

	else if (chance < PEN_DOWN_EVENT_CHANCE)
	{
		needPenUp = true;

		FakeEventXY(&pen.x, &pen.y);
	
		lastPointX = pen.x;
		lastPointY = pen.y;
		lastPenDown = true;
		EmEventPlayback::RecordPenEvent (pen);
		StubAppEnqueuePt(&pen);
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: posted pen event = (%d, %d), leaving",
				number, pen.x, pen.y);

		// Draw a test pixel on the overlay				
		StubViewDrawPixel(pen.x, pen.y);
		return true;
	}


	else if (chance < MENU_EVENT_CHANCE)
	{
		EmEventPlayback::RecordKeyEvent (vchrMenu, vchrMenu, commandKeyMask);
		StubAppEnqueueKey(vchrMenu, vchrMenu, commandKeyMask);
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: posted key = vchrMenu, leaving", number);
		return true;
	}


	else if (chance < FIND_EVENT_CHANCE)
	{
		EmEventPlayback::RecordKeyEvent (vchrFind, vchrFind, commandKeyMask);
		StubAppEnqueueKey(vchrFind, vchrFind, commandKeyMask);
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: posted key = vchrFind, leaving", number);
		return true;
	}


	else if (chance < KEYBOARD_EVENT_CHANCE)
	{
		EmEventPlayback::RecordKeyEvent (vchrKeyboard, vchrKeyboard, commandKeyMask);
		StubAppEnqueueKey(vchrKeyboard, vchrKeyboard, commandKeyMask);
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: posted key = vchrKeyboard, leaving", number);
		return true;
	}


	else if (chance < LOW_BATTERY_EVENT_CHANCE)
	{
		EmEventPlayback::RecordKeyEvent (vchrLowBattery, vchrLowBattery, commandKeyMask);
		StubAppEnqueueKey(vchrLowBattery, vchrLowBattery, commandKeyMask);
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: posted key = vchrLowBattery, leaving", number);
		return true;
	}


	else if (chance < APP_SWITCH_EVENT_CHANCE)
	{
		// Modify the standard APP_SWITCH_EVENT_CHANCE by another factor
		// of 5%.  Without it, we enter this code way too often, and
		// Gremlins slows down a LOT! (Like, by a factor of 2.3).

		if (randPercent < 5)
		{
			DatabaseInfoList	appList = Hordes::GetAppList ();

			if (appList.size () > 0)
			{
				// Switch to a random app on the list.

				DatabaseInfo&	dbInfo = appList [randN (appList.size ())];

				//---------------------------------------------------------------------
				// If this is an executable, call SysUIAppSwitch
				//---------------------------------------------------------------------
				if (::IsExecutable (dbInfo.type, dbInfo.creator, dbInfo.dbAttrs))
				{
					EmuAppInfo		currentApp = EmPatchState::GetCurrentAppInfo ();
	
					EmEventPlayback::RecordSwitchEvent (dbInfo.cardNo, dbInfo.dbID,
						currentApp.fCardNo, currentApp.fDBID);

					Err err = ::SysUIAppSwitch (dbInfo.cardNo, dbInfo.dbID,
									sysAppLaunchCmdNormalLaunch, NULL);
					Errors::ThrowIfPalmError (err);

					PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: switched to app %s, leaving",
						number, dbInfo.name);

					return true;
				}

				//---------------------------------------------------------------------
				// else, say we tried and call it quits by falling through
				//---------------------------------------------------------------------

			}
		}
	}

/*
	else if (chance < POWER_OFF_CHANCE)
	{
		EmEventPlayback::RecordKeyEvent (vchrAutoOff, vchrAutoOff, commandKeyMask);
		StubAppEnqueueKey(vchrAutoOff, vchrAutoOff, commandKeyMask);
		PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: posted key = vchrAutoOff, leaving", number);
		return true;
	}
*/
	PRINTF ("--- Gremlin #%d Gremlins::GetFakeEvent: exiting with no event.",
			number);

	// If nothing happened fall out to generate a nilEvent	

	EmEventPlayback::RecordNullEvent ();
	return false;
}


/************************************************************
 *
 * FUNCTION: GetPenMovement
 *
 * DESCRIPTION: Make a phony pen movement.
 *
 * PARAMETERS: None.
 *
 * RETURNS: Nothing.
 * 
 * CALLED BY: 
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			Roger	6/1/95	Created.
 *			David	7/31/95	Moved to emulator level.
 *
 *************************************************************/
void Gremlins::GetPenMovement()	
{
	// This function is not called anywhere that I can see.  And since it
	// calls FakeLocalMovement, which calls WinGetDisplayExtent, which
	// doesn't exist in PalmDebugger, out it goes...

#ifndef __DEBUGGER_APPLICATION__
	PointType	pen;


	// check to see if Gremlins has produced its max. # of "events."
/*	if (counter > until)
	{
		testMode &= ~gremlinsOn;
		theApp->UpdateMenus();
	}
*/
#ifdef forSimulator
	counter++;
#endif

	needPenUp = false;
	if (randPercent < PEN_MOVE_CHANCE)
	{
		if (lastPenDown)
		{
			// move a small distance from the last pen position
			if (randPercent < PEN_BIG_MOVE_CHANCE)
			{
				RandomScreenXY(&pen.x, &pen.y);
			}
			else
			{
			FakeLocalMovement(&pen.x, &pen.y, lastPointX, lastPointY);
			}
		}
		else
		{
			// start the pen anywhere!
			RandomScreenXY(&pen.x, &pen.y);
		}
		StubViewDrawLine(pen.x, pen.y, lastPointX, lastPointY);
	}
	else
	{
		lastPenDown = false;
		pen.x = -1;
		pen.y = -1;
		catchUp = true;
	}
	lastPointX = pen.x;
	lastPointY = pen.y;
	EmEventPlayback::RecordPenEvent (pen);
	StubAppEnqueuePt(&pen);
#endif

	PRINTF ("--- Gremlin #%d Gremlins::GetPenMovement: pen = (%d, %d)",
			number, pen.x, pen.y);
}


/************************************************************
 *
 * FUNCTION: BumpCounter
 *
 * DESCRIPTION: Bumps Gremlin event counter
 *
 * PARAMETERS: None.
 *
 * RETURNS: Nothing.
 * 
 *************************************************************/
void Gremlins::BumpCounter()
{
	PRINTF ("--- Gremlin #%d: bumping counter", number);
	++counter;
}
