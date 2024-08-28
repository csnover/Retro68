/* -*- mode: C++; tab-width: 4 -*- */
/* ===================================================================== *\
	Copyright (c) Retro68 contributors.

	This file is part of the Palm OS Emulator.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
\* ===================================================================== */

#ifndef _GDBREMOTE_H_
#define _GDBREMOTE_H_

#include "EmTypes.h"
#include "SocketMessaging.h"
#include <cstddef>
#include <vector>

class CSocket;

enum {
	kGDBPacketFrameSize = 4,
	kGDBHeaderSize = 1,
	kGDBTrailerSizeWithNul = 4,
	kGDBPacketFrameSizeWithNul = kGDBHeaderSize + kGDBTrailerSizeWithNul
};

class GDBParser
{
	public:
		enum {
			kSize = 0x400,
			kPacketSize = kSize - kGDBPacketFrameSizeWithNul
		};

		struct Packet
		{
			const char*		data;
			uint16			len;

			inline operator bool () const;
		};

							GDBParser			(void);

		Packet				Next				(void);
		ErrCode				Read				(CSocket *);

	private:
		enum State { Idle, InPacket, InChecksum1, InChecksum2 };
		enum { kInlineLenPadding = 1 };

		uint8	fBuf[kSize];
		uint16	fIndex;			//< start of first unread packet
		uint16	fLen;			//< end of last committed packet

		uint16	fInIndex;		//< end of received data
		uint16	fParseIndex;	//< parser location

		State	fState;
		uint8	fExpectedChecksum;
		uint8	fActualChecksum;
};

class GDBWriter
{
	public:
		enum { kSize = GDBParser::kSize, kPacketSize = GDBParser::kPacketSize };

							GDBWriter			(void);

		inline ErrCode		Push				(char c);
		uint16				Push				(const void* data, uint16 len);
		ErrCode				PushFile			(FILE* fp, uint32 offset, uint32 len);
		ErrCode 			PushFmt				(const char* fmt, ...);
		ErrCode 			PushHex				(const void* data, uint16 len);
		ErrCode				PushStr				(const char* cstr);
		ErrCode 			PushXfer			(uint16 offset, uint16 len, const char* fmt, ...);
		inline void			Reset				(void);
		ErrCode 			Write				(CSocket* socket);

	private:
		inline uint16		Avail				(void) const;
		inline uint8*		Data				(void);
		inline uint16		DataSize			(void) const;
		inline char*		WritePtr			(void);

		uint8	fBuf[kSize];
		uint16	fLen;
};

class GDBRemote : public CSocket
{
	public:
							GDBRemote			(CSocket* s);

		virtual ErrCode 	Open				(void);
		virtual ErrCode 	Close				(void);
		virtual ErrCode 	Write				(const void* buffer, int32 amountToWrite, int32* amtWritten);
		virtual ErrCode 	Read				(void* buffer, int32 sizeOfBuffer, int32* amtRead, int flags = kFlagReadExact);
		virtual Bool		HasUnreadData		(int32 timeout);
		virtual ErrCode 	Idle				(void);

		virtual Bool		ShortPacketHack 	(void);
		virtual Bool		ByteswapHack		(void);

		inline CSocket*		Socket				(void);

	protected:
		virtual 			~GDBRemote			(void);

	private:
							GDBRemote			(const GDBRemote&);
		GDBRemote&			operator=			(const GDBRemote&);

		ErrCode				GDBBreakpoint		(const char* command, size_t len);
		ErrCode				GDBCommand			(int slpType, const char* command, size_t len);
		ErrCode				GDBCommandRead		(const char* command, size_t len);
		ErrCode				GDBHostIO			(const char* command, size_t len);
		ErrCode				GDBPacketIn			(void);
		ErrCode				GDBQuery			(const char* command, size_t len);
		ErrCode				GDBSendError		(ErrCode code);
		ErrCode				GDBSingleRegister	(bool set, const char* command, size_t len);
		ErrCode				GDBThread			(const char* command, size_t len);
		ErrCode				GDBVerboseCommand	(const char* command, size_t len);

		void				SLPRequestOut		(void* buffer, int32 sizeOfBuffer, int32* amtRead, int flags);
		ErrCode				SLPResponseOut		(size_t packetSize);
		size_t				SLPResponseSize		(void);

		struct FixedBuffer
		{
			enum { kSize = 0x1000 };

			inline size_t	Avail				(void) const;
			inline size_t	Avail				(const uint8* p) const;
			inline void		Commit				(size_t amount);
			inline uint8*	End					(void);
			inline uint8*	Get					(void);
			ErrCode			Push				(const void* data, size_t dataSize);
			uint8*			Shift				(size_t amount);
			inline size_t	Size				(void) const;

			private:
				uint8		fBuf[kSize];
				size_t		fIndex;
				size_t		fLen;
		};

		enum SocketType { kSocketUnknown, kSocketGDB, kSocketSLP };
		typedef std::vector<FILE*> FileTable;

		CSocket*		fSocket;
		GDBParser		fGDBParser;
		GDBWriter		fGDBWriter;
		FixedBuffer		fSLPRequest;
		FixedBuffer		fSLPResponse;
		SocketType		fSocketType;
		FileTable		fROMSymbolTable;
};

GDBParser::Packet::operator bool () const
{
	return data != NULL;
}

uint16 GDBWriter::Avail (void) const
{
	return sizeof (fBuf) - kGDBTrailerSizeWithNul - fLen;
}

uint8* GDBWriter::Data (void)
{
	return fBuf + kGDBHeaderSize;
}

uint16 GDBWriter::DataSize (void) const
{
	return fLen - kGDBHeaderSize;
}

ErrCode GDBWriter::Push (char c)
{
	 return Push (&c, 1);
}

void GDBWriter::Reset (void)
{
	fLen = kGDBHeaderSize;
}

char* GDBWriter::WritePtr (void)
{
	return reinterpret_cast<char*> (fBuf) + fLen;
}

CSocket* GDBRemote::Socket (void)
{
	return fSocket;
}

size_t GDBRemote::FixedBuffer::Avail (void) const
{
	return sizeof (fBuf) - fLen;
}

size_t GDBRemote::FixedBuffer::Avail (const uint8* p) const
{
	return sizeof (fBuf) - (p - fBuf);
}

void GDBRemote::FixedBuffer::Commit (size_t amount)
{
	fLen += amount;
}

uint8* GDBRemote::FixedBuffer::End (void)
{
	return fBuf + fLen;
}

uint8* GDBRemote::FixedBuffer::Get (void)
{
	return fBuf + fIndex;
}

size_t GDBRemote::FixedBuffer::Size (void) const
{
	return fLen - fIndex;
}

#endif
