/* -*- mode: C++; tab-width: 4 -*- */
/* ===================================================================== *\
	Copyright (c) Retro68 contributors.

	This file is part of the Palm OS Emulator.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
\* ===================================================================== */

#include "EmCommon.h"
#include "EmBankSRAM.h"
#include "EmBankROM.h"
#include "EmErrCodes.h"
#include "EmHAL.h"
#include "EmPalmFunction.h"
#include "EmPalmStructs.h"
#include "EmPatchState.h"
#include "GDBRemote.h"
#include "Logging.h"
#include "SLP.h"
#include "SocketMessaging.h"
#include "SystemPacket.h"
#include "UAE.h"
#include <limits>

#define PRINTF	if (!LogHLDebugger ()) ; else LogAppendMsg

#define TRY(expr) do {					\
	ErrCode try_result;					\
	try_result = (expr);				\
	if (try_result != kError_NoError)	\
		return try_result;				\
} while (0)

static const char HOSTINFO_DESC[] =
	"cputype:6"
	";cpusubtype:1"
	";triple:m68k-none-palmos"
	";endian:big"
	";ptrsize:4";

static const char FEATURES_DESC[] =
	"PacketSize=%x"
	";multiprocess-"
	";QCatchSyscalls+"
	";ConditionalBreakpoints+"
	";qXfer:memory-map:read+"
	";qXfer:features:read+"
	";qXfer:exec-file:read+";

static const char TARGET_DESC[] = "<?xml version=\"1.0\"?>"
	"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
	"<target version=\"1.0\">"
		"<architecture>m68k:68000</architecture>"
		"<osabi>none</osabi>"
		"<feature name=\"org.gnu.gdb.m68k.core\">"
			"<reg name=\"d0\" bitsize=\"32\"/>"
			"<reg name=\"d1\" bitsize=\"32\"/>"
			"<reg name=\"d2\" bitsize=\"32\"/>"
			"<reg name=\"d3\" bitsize=\"32\"/>"
			"<reg name=\"d4\" bitsize=\"32\"/>"
			"<reg name=\"d5\" bitsize=\"32\"/>"
			"<reg name=\"d6\" bitsize=\"32\"/>"
			"<reg name=\"d7\" bitsize=\"32\"/>"
			"<reg name=\"a0\" bitsize=\"32\" type=\"data_ptr\"/>"
			"<reg name=\"a1\" bitsize=\"32\" type=\"data_ptr\"/>"
			"<reg name=\"a2\" bitsize=\"32\" type=\"data_ptr\"/>"
			"<reg name=\"a3\" bitsize=\"32\" type=\"data_ptr\"/>"
			"<reg name=\"a4\" bitsize=\"32\" type=\"data_ptr\"/>"
			"<reg name=\"a5\" bitsize=\"32\" type=\"data_ptr\"/>"
			"<reg name=\"fp\" bitsize=\"32\" type=\"data_ptr\"/>"
			"<reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>"
			"<reg name=\"ps\" bitsize=\"32\"/>"
			"<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
		"</feature>"
	"</target>";

static const char MEMORY_MAP_DESC[] = "<?xml version=\"1.0\"?>"
"<!DOCTYPE memory-map PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\" "
	"\"http://sourceware.org/gdb/gdb-memory-map.dtd\">"
	"<memory-map>"
		"<memory type=\"ram\" start=\"%d\" length=\"%d\"/>"
		"<memory type=\"rom\" start=\"%d\" length=\"%d\"/>"
		"<memory type=\"flash\" start=\"%d\" length=\"%d\">"
			// Guess based on dbgFlashCodeSize
			"<property name=\"blocksize\">512</property>"
		"</memory>"
	"</memory-map>";

enum { kInterrupt = '\x03' };

enum {
	errChecksum = kError_DbgErrBase + 1,
	errNoPktStart,
	errNoPktEnd,
	errBadRegSize,
	errBadRegVal,
	errBadHex,
	errBadOffsetArgs,
	errAgain    = kError_DbgErrBase + 11,
	errInval    = kError_DbgErrBase + 22,
	errFault    = kError_DbgErrBase + 33,
	errBadMsg   = kError_DbgErrBase + 74,
};

static const char* PrvErrString (ErrCode code)
{
	switch (code) {
		case errChecksum: return "Bad checksum";
		case errNoPktStart: return "Missing packet start '$'";
		case errNoPktEnd: return "Missing packet end '#'";
		case errBadRegSize: return "Bad register size";
		case errBadRegVal: return "Bad register value";
		case errBadHex: return "Bad hex binary string length";
		case errBadOffsetArgs: return "Bad offset/length";
		case errAgain: return "Try again";
		case errInval: return "Invalid data";
		case errFault: return "Fault";
		case errBadMsg: return "Bad message";
		default:
			if (IsEmuError (code))
				return "Emulator error";
			else if (IsPalmError (code))
				return "Palm error";
			else if (IsStdCError (code))
				return "C error";
			else
				return "Unknown error";
	}
}

GDBParser::GDBParser (void)
	: fIndex (kInlineLenPadding)
	, fLen (kInlineLenPadding)
	, fInIndex (kInlineLenPadding)
	, fParseIndex (kInlineLenPadding)
	, fState (Idle)
	, fExpectedChecksum (0)
	, fActualChecksum (0)
{
}

GDBParser::Packet GDBParser::Next (void)
{
	Packet packet;
	if (fIndex == fLen)
	{
		packet.data = NULL;
		packet.len = 0;
	}
	else if (fBuf[fIndex] == kInterrupt)
	{
		packet.data = reinterpret_cast<char*> (fBuf) + fIndex++;
		packet.len = 1;
	}
	else
	{
		uint16 index = fIndex;
		uint16 len = (fBuf[index - 1] << 8 | fBuf[index]);
		fIndex += len;

		packet.data = reinterpret_cast<char*> (fBuf) + index + kGDBHeaderSize;
		packet.len = len - kGDBPacketFrameSize;
	}

	return packet;
}

static inline uint8 PrvHexNibbleToBinary (uint8 c)
{
	// Code golf to eliminate branching for no good reason
	// 0-9 low nibble is 0 to 9 and bit 6 is clear
	// A-F or a-f low nibble is 1 to 6 and bit 6 is set; add 9 to get
	// correct value by shifting bit 6 to add 1 + 8
	return ((c & 0xf) + (c >> 6) + ((c >> 3) & 0x8));
}

ErrCode GDBParser::Read (CSocket *socket)
{
	if (fIndex == fLen)
	{
		if (fInIndex == fLen)
		{
			fInIndex = kInlineLenPadding;
			fParseIndex = kInlineLenPadding;
		}
		else
		{
			memmove (fBuf + kInlineLenPadding, fBuf + fLen, fInIndex - fLen);
			fParseIndex -= fLen - kInlineLenPadding;
			fInIndex -= fLen - kInlineLenPadding;
		}

		fIndex = kInlineLenPadding;
		fLen = kInlineLenPadding;
	}

	int32 amtToRead = sizeof (fBuf) - fInIndex;
	if (amtToRead == 0)
		return kError_OutOfMemory;

	int32 amtRead;
	ErrCode result = socket->Read (fBuf + fInIndex, amtToRead, &amtRead, CSocket::kNoFlags);

	fInIndex += amtRead;

	while (result == kError_NoError && fParseIndex < fInIndex)
	{
		uint8& b = fBuf[fParseIndex++];
		switch (fState)
		{
			case InChecksum1:
				fState = InChecksum2;
				fExpectedChecksum = PrvHexNibbleToBinary (b) << 4;
				break;
			case InChecksum2: {
				fState = Idle;
				fExpectedChecksum |= PrvHexNibbleToBinary (b);
				char ack = (fActualChecksum == fExpectedChecksum) ? '+' : '-';
				fActualChecksum = 0;
				fExpectedChecksum = 0;
				result = socket->Write(&ack, 1, NULL);
				uint16 entryLen = fParseIndex - fLen;
				if (ack == '+')
				{
					// Length of packet is shoved in the space right before
					// the data of the packet for fast calculation of the next
					// packet location/size when pulling packets out of the
					// queue
					fBuf[fLen - 1] = entryLen >> 8;
					fBuf[fLen] = entryLen;
					fLen = fParseIndex;
				}
				else if (fParseIndex == fInIndex)
				{
					fInIndex = fLen;
					fParseIndex = fLen;
				}
				else
				{
					memmove (fBuf + fLen, fBuf + fParseIndex, fInIndex - fParseIndex);
					fInIndex -= entryLen;
					fParseIndex = fLen;
				}
				return result;
			}
			case InPacket:
				if (b == '#')
				{
					// Null-terminate the data for e.g. sscanf
					b = '\0';
					fState = InChecksum1;
				}
				else
					fActualChecksum += b;
				break;
			case Idle:
				if (b == kInterrupt)
					++fLen;
				else if (b == '$')
					fState = InPacket;
				else
				{
					// GDB sends acknowledgement '+' on initial connection
					// before sending any other message; at least some other
					// parsers seem to just ignore all trash between packets
					// instead of sending NAK, so just do that here too
					++fIndex;
					++fLen;
				}
				break;
		}
	}

	return result;
}

GDBWriter::GDBWriter (void)
	: fLen(kGDBHeaderSize)
{
	fBuf[0] = '$';
}

ErrCode GDBWriter::Push (const void* data, uint16 len)
{
	if (Avail () < len)
		return kError_OutOfMemory;
	memcpy (WritePtr (), data, len);
	fLen += len;
	return kError_NoError;
}

ErrCode GDBWriter::PushHex (const void* data, uint16 len)
{
	static const char LUT[] = "0123456789abcdef";

	int outLen = int (len) * 2;
	if (outLen > Avail ())
		return kError_OutOfMemory;

	const uint8* in = reinterpret_cast<const uint8*> (data);
	const uint8* const inEnd = in + len;
	char* out = WritePtr ();
	while (in != inEnd)
	{
		*out++ = LUT[*in >> 4];
		*out++ = LUT[*in++ & 0xf];
	}
	fLen += outLen;
	return kError_NoError;
}

ErrCode GDBWriter::PushFmt (const char* fmt, ...)
{
	enum { kAllowTrailingNul = 1 };
	uint16 maxWrite = Avail ();

	// TODO: This needs to handle escaping all of '}' '$' '#' '*'
	va_list args;
	va_start (args, fmt);
	int amtWritten = vsnprintf (WritePtr (), maxWrite + kAllowTrailingNul, fmt, args);
	va_end (args);

	if (amtWritten < 0)
		return errFault;

	if (amtWritten > maxWrite)
		return kError_OutOfMemory;

	fLen += amtWritten;
	return kError_NoError;
}

ErrCode GDBWriter::PushStr (const char* cstr)
{
	uint16 len = strlen (cstr);
	return Push (cstr, len);
}

ErrCode GDBWriter::PushXfer	(uint16 offset, uint16 len, const char* fmt, ...)
{
	// This could be much more complex and thus slightly more optimal by
	// parsing through the format string manually to skip stuff before/after
	// the request range instead of writing to a temporary buffer and then
	// extracting from there, but there is no reason for that complexity since
	// the only messages that use Xfer here are short enough to fit in a small
	// temporary buffer

	if (len == 0)
		return Push ('l');

	char tmp[1024];
	va_list args;
	va_start (args, fmt);
	int amtWritten = vsnprintf(tmp, sizeof (tmp), fmt, args);
	va_end (args);

	if (amtWritten < 0 || amtWritten >= int (sizeof (tmp)))
		return errInval;

	if (amtWritten <= offset)
		return Push('l');

	len = std::min<int> (amtWritten - offset, len);

	const char* in = tmp + offset;
	const char* const inEnd = in + len;

	enum { kStatusLen = 1 };
	char* state = WritePtr ();
	char* out = state + kStatusLen;
	char* const outEnd = state + Avail ();

	while (in != inEnd && out != outEnd)
	{
		char c = *in++;
		if (c == '$' || c == '#' || c == '}' || c == '*')
		{
			enum { kEscapeLen = 1 };
			if (out + kEscapeLen == outEnd)
				break;

			++len;
			*out++ = '}';
			*out++ = c ^ 0x20;
		}
		else
			*out++ = c;
	}

	fLen += kStatusLen + len;
	*state = (in == inEnd ? 'l' : 'm');

	return kError_NoError;
}

static inline uint8 PrvCalcChecksum (const void* data, size_t len)
{
	uint8 checksum = 0;
	const uint8* p = static_cast<const uint8*> (data);
	while (len--)
		checksum += *p++;
	return checksum;
}

ErrCode GDBWriter::Write (CSocket* socket)
{
	uint8 checksum = PrvCalcChecksum (Data (), DataSize ());
	sprintf(WritePtr (), "#%02x", checksum);

	ErrCode result = socket->Write (fBuf, DataSize () + kGDBPacketFrameSize, NULL);
	fLen = kGDBHeaderSize;

	return result;
}

GDBRemote::GDBRemote (CSocket* s) :
	CSocket (),
	fSocket (s),
	fGDBParser (),
	fGDBWriter (),
	fSLPRequest (),
	fSLPResponse (),
	fSocketType (kSocketUnknown)
{
}

GDBRemote::~GDBRemote (void)
{
}

ErrCode GDBRemote::Open (void)
{
	return fSocket->Open ();
}

ErrCode GDBRemote::Close (void)
{
	return fSocket->Close ();
}

ErrCode GDBRemote::Write (const void* buffer, int32 amountToWrite, int32* amtWritten)
{
	if (fSocketType == kSocketSLP)
		return fSocket->Write (buffer, amountToWrite, amtWritten);

	int32 dummy;
	if (amtWritten == NULL)
		amtWritten = &dummy;

	ErrCode result = fSLPResponse.Push (buffer, amountToWrite);

	size_t packetSize;
	while (result == kError_NoError && (packetSize = SLPResponseSize ()) != 0)
		result = SLPResponseOut (packetSize);

	return result;
}

static bool PrvIsSLPPacket (CSocket* s)
{
	uint8 sig[3];
	int32 amtRead;

	if (s->Read (sig, sizeof (sig), &amtRead, CSocket::kFlagPeek) == errNone &&
		amtRead == sizeof (sig))
	{
		return sig[0] == slkPktHeaderSigFirst &&
			sig[1] == slkPktHeaderSigSecond &&
			sig[2] == slkPktHeaderSigThird;
	}

	return false;
}

ErrCode GDBRemote::Read (void* buffer, int32 sizeOfBuffer, int32* amtRead, int flags)
{
	if (fSocketType == kSocketUnknown)
		fSocketType = PrvIsSLPPacket (fSocket) ? kSocketSLP : kSocketGDB;

	if (fSocketType == kSocketSLP)
		return fSocket->Read (buffer, sizeOfBuffer, amtRead, flags);

	ErrCode result;

	while (fSocket->HasUnreadData (0))
		if ((result = GDBPacketIn ()) != kError_NoError)
		{
			GDBSendError (result);
			return result;
		}

	int32 dummy;
	if (amtRead == NULL)
		amtRead = &dummy;

	if (fSLPRequest.Size () != 0)
		SLPRequestOut (buffer, sizeOfBuffer, amtRead, flags);
	else
	{
		*amtRead = 0;
		result = errAgain;
	}

	return result;
}

Bool GDBRemote::HasUnreadData (int32 timeout)
{
	return fSocket->HasUnreadData (timeout);
}

ErrCode GDBRemote::Idle (void)
{
	return fSocket->Idle ();
}

Bool GDBRemote::ShortPacketHack (void)
{
	return fSocket->ShortPacketHack ();
}

Bool GDBRemote::ByteswapHack (void)
{
	return fSocket->ByteswapHack ();
}

static inline bool PrvIsCommand (const char *name, size_t nameLen, const char*& in, size_t& len)
{
	if (len >= nameLen && strncmp (name, in, nameLen) == 0)
	{
		in += nameLen;
		len -= nameLen;
		return true;
	}

	return false;
}

#define COMMAND(name) (PrvIsCommand (name, sizeof (name) - 1, in, len))

#define TRY_SLP_COMMAND(name, slpCommand) do {								\
	if (COMMAND (name)) 													\
		return GDBCommand ((slpCommand), in, len);							\
} while (0)

static ErrCode PrvGDBHexToBinary (void* dest, size_t destSize, const void* src, size_t len)
{
	if ((len & 1) != 0 || destSize < len / 2)
		return errBadHex;

	uint8* out = static_cast<uint8*> (dest);
	const char* in = static_cast<const char*> (src);
	const char* end = in + len;

	while (in != end)
	{
		uint8 value = 0;
		for (int i = 0; i < 2; ++i)
		{
			char c = *in++;
			value = (value << 4) | PrvHexNibbleToBinary (c);
		}
		*out++ = value;
	}

	return kError_NoError;
}

static ErrCode PrvSetRegister (EmAliasUInt32<LAS> reg, const char*& data, size_t& len, UInt32 value)
{
	enum { kUint32HexLen = 8 };

	if (len < kUint32HexLen)
		return errBadRegSize;

	if (strncmp ("xxxxxxxx", data, kUint32HexLen) != 0 &&
		sscanf (data, "%8x", &value) != 1)
			return errBadRegVal;

	reg = value;
	data += kUint32HexLen;
	len -= kUint32HexLen;
	return kError_NoError;
}

static inline bool PrvInSupervisorMode (uint16 sr)
{
	enum { kSupervisorMode = 1 << 13 };
	return (sr & kSupervisorMode) != 0;
}

ErrCode GDBRemote::GDBCommand (int command, const char* data, size_t len)
{
	EmAliasSlkPktHeaderType<LAS> header (fSLPRequest.End ());
	header.signature1 = slkPktHeaderSignature1;
	header.signature2 = slkPktHeaderSignature2;
	header.dest = slkSocketDebugger;
	header.src = slkSocketDebugger;
	header.type = slkPktTypeSystem;
	header.transId = 0;

	uint8* bodyPtr = fSLPRequest.End () + header.GetSize ();
	EmAliasSysPktBodyType<LAS> body (bodyPtr);
	body.command = command;

	header.bodySize = uint16 (body.offsetof_data ());

	switch (command)
	{
		case sysPktStateCmd:
		case sysPktSingleStepCmd:
			break;
		case sysPktContinueCmd: {
			EmAliasSysPktContinueCmdType<LAS> body (bodyPtr);
			header.bodySize = uint16 (body.GetSize ());
			M68KRegsType current;
			SystemPacket::GetRegs (current);
			body.regs.d[0]	= current.d[0];
			body.regs.d[1]	= current.d[1];
			body.regs.d[2]	= current.d[2];
			body.regs.d[3]	= current.d[3];
			body.regs.d[4]	= current.d[4];
			body.regs.d[5]	= current.d[5];
			body.regs.d[6]	= current.d[6];
			body.regs.d[7]	= current.d[7];
			body.regs.a[0]	= current.a[0];
			body.regs.a[1]	= current.a[1];
			body.regs.a[2]	= current.a[2];
			body.regs.a[3]	= current.a[3];
			body.regs.a[4]	= current.a[4];
			body.regs.a[5]	= current.a[5];
			body.regs.a[6]	= current.a[6];
			body.regs.usp	= current.usp;
			body.regs.ssp	= current.ssp;
			body.regs.pc	= current.pc;
			body.regs.sr	= current.sr;
			body.stepSpy = false;
			body.ssAddr = 0;
			body.ssCount = 0;
			body.ssCheckSum = 0;
			break;
		}
		case sysPktReadMemCmd:
		case sysPktWriteMemCmd: {
			EmAliasSysPktWriteMemCmdType<LAS> body (bodyPtr);

			emuptr addr;
			uint32 numBytes;
			uint32 bytesRead;

			if (sscanf (data, "%x,%x%n", &addr, &numBytes, &bytesRead) != 2)
				return errBadOffsetArgs;

			body.address = addr;
			body.numBytes = numBytes;

			if (command == sysPktWriteMemCmd)
			{
				header.bodySize = uint16 (body.offsetof_data ()) + numBytes;
				data += bytesRead;
				len -= bytesRead;
				uint8* out = bodyPtr + body.offsetof_data ();
				size_t outSize = fSLPRequest.Avail (out);
				TRY (PrvGDBHexToBinary (out, outSize, data, len));
			}
			else
				header.bodySize = uint16 (EmAliasSysPktReadMemCmdType<LAS>::GetSize ());

			break;
		}

		case sysPktReadRegsCmd:
		case sysPktWriteRegsCmd: {
			EmAliasSysPktWriteRegsCmdType<LAS> body (bodyPtr);
			if (command == sysPktWriteRegsCmd)
			{
				header.bodySize = uint16 (body.GetSize ());
				M68KRegsType current;
				SystemPacket::GetRegs (current);

#define COPY_GDB_REGISTER(which) \
				TRY (PrvSetRegister ((body.reg.which), data, len, (current.which)));

				for (size_t i = 0; i < countof (current.d); ++i)
					COPY_GDB_REGISTER (d[i]);
				for (size_t i = 0; i < countof (current.a); ++i)
					COPY_GDB_REGISTER (a[i]);
				if (PrvInSupervisorMode (current.sr))
				{
					COPY_GDB_REGISTER (ssp);
					COPY_GDB_REGISTER (usp);
				}
				else
				{
					COPY_GDB_REGISTER (usp);
					COPY_GDB_REGISTER (ssp);
				}
				COPY_GDB_REGISTER (pc);
#undef COPY_GDB_REGISTER
			} else
				header.bodySize = uint16 (body.offsetof_reg ());
			break;
		}
		default: return errInval;
	}

	header.checksum	= PrvCalcChecksum (header.GetPtr (), header.offsetof_checksum ());

	EmAliasSlkPktFooterType<LAS> footer (bodyPtr + header.bodySize);
	footer.crc16 = ::Crc16CalcBlock (header.GetPtr (), header.GetSize (), 0);
	footer.crc16 = ::Crc16CalcBlock (body.GetPtr (), header.bodySize, footer.crc16);
	fSLPRequest.Commit (header.GetSize () + header.bodySize + footer.GetSize ());
	return kError_NoError;
}

ErrCode GDBRemote::GDBCommandRead (const char* in, size_t len)
{
	if (len == 0)
		return errBadMsg;

	--len;
	switch (*in++)
	{
		case 'q': return GDBQuery (in, len);
		case kInterrupt:
			EmASSERT(false && "TODO; like '?' except should stop with signal");
		case '?': return GDBCommand (sysPktStateCmd, in, len);
		case 'k':
		case 'c':
		case 'C': return GDBCommand (sysPktContinueCmd, in, len);
		case 'g': return GDBCommand (sysPktReadRegsCmd, in, len);
		case 'G': return GDBCommand (sysPktWriteRegsCmd, in, len);
		case 'H': return GDBThread (in, len);
		case 'i':
		case 's': return GDBCommand (sysPktSingleStepCmd, in, len);
		case 'm': return GDBCommand (sysPktReadMemCmd, in, len);
		case 'M': return GDBCommand (sysPktWriteMemCmd, in, len);
		case 'p': return GDBSingleRegister (false, in, len);
		case 'P': return GDBSingleRegister (true, in, len);
		case 'v': return GDBExtendedCommand (in, len);
		// TODO: Should be possible by 'M', if needed
		// case 'X': EmAssert (false);
		case 'z': return GDBCommand (sysPktGetBreakpointsCmd, in, len);
		case 'Z': return GDBCommand (sysPktSetBreakpointsCmd, in, len);
		default: return fGDBWriter.Write (fSocket);
	}
}

ErrCode GDBRemote::GDBExtendedCommand (const char* in, size_t len)
{
	TRY_SLP_COMMAND ("CtrlC", sysPktStateCmd);
	TRY_SLP_COMMAND ("FlashWrite", sysPktExecFlashCmd);

	if (COMMAND ("Cont?"))
		fGDBWriter.PushStr ("vCont;c;s");
	else if (COMMAND ("Cont"))
	{
		if (len < 1)
			return errBadMsg;
		else if (*in == 'c')
			return GDBCommand (sysPktContinueCmd, in, len);
		else if (*in == 's')
			return GDBCommand (sysPktSingleStepCmd, in, len);
		else
			return errBadMsg;
	}

	return fGDBWriter.Write (fSocket);
}

ErrCode GDBRemote::GDBPacketIn (void)
{
	TRY (fGDBParser.Read (fSocket));

	GDBParser::Packet packet;
	while ((packet = fGDBParser.Next ()))
		TRY (GDBCommandRead (packet.data, packet.len));

	return kError_NoError;
}

ErrCode GDBRemote::GDBQuery (const char* in, size_t len)
{
	TRY_SLP_COMMAND ("CRC", sysPktChecksumCmd);
	TRY_SLP_COMMAND ("Search", sysPktFindCmd);

	if (COMMAND ("Supported"))
		TRY (fGDBWriter.PushFmt (FEATURES_DESC, GDBParser::kPacketSize));
	else if (COMMAND ("Xfer:memory-map:read::"))
	{
		uint32 offset, length;
		if (sscanf(in, "%x,%x", &offset, &length) != 2)
			return errBadOffsetArgs;

		TRY (fGDBWriter.PushXfer (offset, length, MEMORY_MAP_DESC,
			gMemoryStart, gRAMBank_Size,
			EmBankROM::GetMemoryStart (), EmHAL::GetROMSize (),
			EmBankFlash::GetMemoryStart (), EmHAL::GetROMSize ()
		));
	}
	else if (COMMAND ("Xfer:features:read:target.xml:"))
	{
		uint32 offset, length;
		if (sscanf(in, "%x,%x", &offset, &length) != 2)
			return errBadOffsetArgs;

		TRY (fGDBWriter.PushXfer(offset, length, TARGET_DESC));
	}
	else if (COMMAND ("Xfer:exec-file:read:"))
	{
		uint32 annex, offset, length;
		if (sscanf (in, "%x:%x,%x", &annex, &offset, &length) != 3 &&
			sscanf (in, ":%x,%x", &offset, &length) != 2)
			return errBadOffsetArgs;

		EmuAppInfo appInfo = EmPatchState::GetCurrentAppInfo ();

		TRY (fGDBWriter.PushXfer(offset, length, "%s", appInfo.fName));
	}
	else if (COMMAND ("HostInfo"))
		TRY (fGDBWriter.PushStr (HOSTINFO_DESC));
	else if (COMMAND ("poser.Frame:"))
	{
		emuptr pc;
		if (sscanf (in, "%x", &pc) != 1)
			return errBadMsg;
		char name[256];
		emuptr start, end;
		::FindFunctionName(pc & ~1, name, &start, &end, sizeof (name) - 1);
		TRY (fGDBWriter.PushFmt ("%08x%08x%s", start, end, name));
	}

	// TODO: These thread identifiers probably should change on SysAppLaunch
	else if (COMMAND ("C"))
		TRY (fGDBWriter.PushStr ("QC1"));
	else if (COMMAND ("fThreadInfo"))
		TRY (fGDBWriter.PushStr ("m1"));
	else if (COMMAND ("sThreadInfo"))
		TRY (fGDBWriter.PushStr ("l"));

	else if (COMMAND ("Offsets"))
		EmASSERT (false && "TODO");
	else if (COMMAND ("CatchSyscalls"))
		// return GDBCommand (sysPktGetTrapBreaksCmd) ???
		EmASSERT (false && "TODO");

	return fGDBWriter.Write (fSocket);
}

#undef TRY_SLP_COMMAND
#undef COMMAND

ErrCode GDBRemote::GDBSendError (ErrCode code)
{
	fGDBWriter.Reset ();
	TRY (fGDBWriter.PushFmt ("E.%s (%d)", PrvErrString (code), code));
	return fGDBWriter.Write (fSocket);
}

ErrCode GDBRemote::GDBSingleRegister (bool set, const char* command, size_t len)
{
	int which;
	uint32 value;
	if (set && sscanf(command, "%x=%x", &which, &value) != 2)
		return errBadMsg;
	else if (!set && sscanf(command, "%x", &which) != 1)
		return errBadMsg;

	// GetRegs does some extra stuff to make sure registers are correct, and
	// this call seems unlikely to be a performance issue
	M68KRegsType current;
	SystemPacket::GetRegs (current);

	uint32* reg;
	if (which >= 0 && which < 8)
		reg = &current.d[which];
	else if (which >= 8 && which < 15)
		reg = &current.a[which - 8];
	else if (which == 15)
		reg = PrvInSupervisorMode (current.sr) ? &current.ssp : &current.usp;
	else if (which == 16)
		reg = PrvInSupervisorMode (current.sr) ? &current.ssp : &current.usp;
	else if (which == 17)
		reg = &current.pc;
	else
		return errBadMsg;

	if (set)
	{
		*reg = value;
		SystemPacket::SetRegs (current);
		TRY (fGDBWriter.PushStr ("OK"));
	}
	else
	{
		value = *reg;
		TRY (fGDBWriter.PushFmt("%08x", value));
	}

	return fGDBWriter.Write (fSocket);
}

ErrCode GDBRemote::GDBThread (const char* command, size_t len)
{
	char op;
	int tid;
	if (sscanf (command, "%c%x", &op, &tid) != 2 &&
		(sscanf (command, "%c%d", &op, &tid) != 2 || tid != -1))
			return errBadMsg;

	if (op != 'g' && op != 'c')
		return errBadMsg;

	TRY (fGDBWriter.PushStr ("OK"));
	return fGDBWriter.Write (fSocket);
}

void GDBRemote::SLPRequestOut (void* buffer, int32 sizeOfBuffer, int32* amtRead, int flags)
{
	*amtRead = std::min<int32> (sizeOfBuffer, fSLPRequest.Size ());
	memcpy (buffer, fSLPRequest.Get (), *amtRead);
	if ((flags & kFlagPeek) == 0)
		fSLPRequest.Shift (*amtRead);
}

static int PrvExceptionToSignal (int exceptionId)
{
	// Conversion values taken from prc-remix remote-palmos.c
	switch (exceptionId)
	{
		case 0:			return /* 0       */ 0;		// running
		case 2: 									// bus error
		case 3: 		return /* SIGBUS  */ 10;	// address error
		case 4: 		return /* SIGILL  */ 4;		// illegal instruction
		case 5:										// divide by zero
		case 6:										// CHK instruction
		case 7:			return /* SIGFPE  */ 8;		// TRAPV instruction
		case 8:			return /* SIGSEGV */ 11;	// privilege violation
		case 9:			return /* SIGTRAP */ 5;		// trace
		case 10:									// line 1010 emulator
		case 11:		return /* SIGILL  */ 4;		// line 1111 emulator
		case 13:		return /* SIGBUS  */ 10;	// coprocessor protocol violation
		case 31:		return /* SIGQUIT */ 2;		// interrupt
		case 32:		return /* SIGTRAP */ 5;		// breakpoint
		case 40:									// trap #8
		case 48:									// floating point error
		case 49:									// floating point error
		case 50:									// zero divide
		case 51:									// underflow
		case 52:									// operand error
		case 53:									// overflow
		case 54:		return /* SIGFPE  */ 8;		// NaN
		default:		return /* SIGEMT  */ 7;		// "software generated"
	}
}

#define FOR_EACH_REGISTER_DO(regs, expr, ...) do {				\
	for (int i = 0; i < 8; ++i)									\
		TRY ((expr) (regs.d[i], __VA_ARGS__));					\
	for (int i = 0; i < 7; ++i)									\
		TRY ((expr) (regs.a[i], __VA_ARGS__));					\
	if (PrvInSupervisorMode (regs.sr))							\
	{															\
		TRY ((expr) (regs.ssp, __VA_ARGS__));					\
		TRY ((expr) (regs.usp, __VA_ARGS__));					\
	}															\
	else														\
	{															\
		TRY ((expr) (regs.usp, __VA_ARGS__));					\
		TRY ((expr) (regs.ssp, __VA_ARGS__));					\
	}															\
	TRY ((expr) (regs.pc, __VA_ARGS__));						\
} while (0)

static ErrCode PrvRegisterToReadList (uint32 reg, GDBWriter& writer)
{
	return writer.PushFmt ("%08x", reg);
}

static ErrCode PrvRegisterToStopList (uint32 reg, int &regNum, char*& out)
{
	int len = sprintf (out, "%x:%08x;", regNum, reg);
	if (len < 0)
		return errFault;
	out += len;
	++regNum;
	return kError_NoError;
}

enum {
	kMaxRegStringLen =
		( 2		// register index string
		+ 8		// register value string
		+ 2		// delimiter characters
		) * 18	// total number of registers
};

static ErrCode PrvRegistersToGDB (EmAliasM68KRegsType<LAS> regs, char buf[kMaxRegStringLen])
{
	char* out = buf;
	int regNum = 0;

	FOR_EACH_REGISTER_DO (regs, PrvRegisterToStopList, regNum, out);

	return kError_NoError;
}

ErrCode GDBRemote::SLPResponseOut (size_t packetSize)
{
	uint8* packet = fSLPResponse.Shift (packetSize);
	EmAliasSlkPktHeaderType<LAS> header (packet);
	EmAliasSysPktBodyType<LAS> body (packet + header.GetSize ());
	switch (body.command)
	{
		case sysPktReadMemRsp: {
			EmAliasSysPktReadMemRspType<LAS> body (packet + header.GetSize ());
			uint16 numBytes = header.bodySize - body.offsetof_data ();
			TRY (fGDBWriter.PushHex (body.data.GetPtr (), numBytes));
			break;
		}
		case sysPktReadRegsRsp: {
			EmAliasSysPktReadRegsRspType<LAS> body (packet + header.GetSize ());
			FOR_EACH_REGISTER_DO (body.reg, PrvRegisterToReadList, fGDBWriter);
			break;
		}
		case sysPktWriteMemRsp:
		case sysPktWriteRegsRsp:
			TRY (fGDBWriter.PushStr ("OK"));
			break;
		case sysPktStateRsp: {
			EmAliasSysPktStateRspType<LAS> body (packet + header.GetSize ());

			char regs[kMaxRegStringLen];
			TRY (PrvRegistersToGDB (body.reg, regs));

			// TODO: Seems like body.resetted should be communicated to
			// the debugger but there does not seem to be an appropriate
			// mapping for it
			// char state = body.resetted ? 'X' : 'T';
			char state = 'T';
			int signal = PrvExceptionToSignal (body.exceptionId);

			TRY (fGDBWriter.PushFmt ("%c%02x%s", state, signal, regs));
			break;
		}

		default: return errInval;
	}

	return fGDBWriter.Write (fSocket);
}

size_t GDBRemote::SLPResponseSize (void)
{
	size_t inSize = fSLPResponse.Size ();
	size_t packetSize = EmAliasSlkPktHeaderType<LAS>::GetSize ();
	if (inSize < packetSize)
		return 0;

	EmAliasSlkPktHeaderType<LAS> header (fSLPResponse.Get ());
	packetSize += header.bodySize;

	if (inSize < packetSize)
		return 0;

	if (!fSocket->ShortPacketHack ())
		packetSize += EmAliasSlkPktFooterType<LAS>::GetSize ();

	if (inSize < packetSize)
		return 0;

	return packetSize;
}

ErrCode GDBRemote::FixedBuffer::Push (const void* data, size_t dataSize)
{
	if (fIndex + dataSize > Avail ())
		return kError_OutOfMemory;

	memcpy (End (), data, dataSize);
	fLen += dataSize;
	return kError_NoError;
}

uint8* GDBRemote::FixedBuffer::Shift (size_t amount)
{
	uint8* out = NULL;
	if (fIndex + amount <= fLen)
	{
		out = fBuf + fIndex;
		fIndex += amount;
		if (fIndex == fLen)
		{
			fIndex = 0;
			fLen = 0;
		}
	}
	return out;
}
