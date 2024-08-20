/* -*- mode: C++; tab-width: 4 -*- */
/* ===================================================================== *\
	Copyright (c) 1999-2001 Palm, Inc. or its subsidiaries.
	All rights reserved.

	This file is part of the Palm OS Emulator.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
\* ===================================================================== */

#ifndef EmApplicationFltk_h
#define EmApplicationFltk_h

#include "EmApplication.h"		// EmApplication
#include "EmStructs.h"			// ByteList

class EmWindowFltk;
class Fl_Widget;

class EmApplicationFltk : public EmApplication
{
	public:
								EmApplicationFltk		(void);
		virtual					~EmApplicationFltk		(void);

	public:
		virtual Bool			Startup					(int argc, char** argv);
		void					Run						(void);
		void					HandleIdle				(void);

	private:
		Bool					PrvIdleClipboard		(void);
		static void				PrvClipboardPeriodic	(void* data);
		Fl_Widget*				PrvGetClipboardWidget	(void);

		EmCommandID				PrvStartupScreen		(void);
		void					HandleStartupDialog		(void);

	private:
		Fl_Widget*				fClipboardWidget;
		ByteList				fClipboardData;
};

extern EmApplicationFltk*	gHostApplication;

#endif	// EmApplicationFltk_h
