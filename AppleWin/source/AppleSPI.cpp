/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Apple SPI Hardware Emulation including paged EEPROM support
 *            : Planned emulation of 65SPI interface by Daryl Rictor - http://sbc.rictor.org/io/SPI.html
 *            : EEPROM support based on CFFA hardware implementation by Rich Dreher (TBD)
 *            : Paging support design as discussed with Jonno Downes
 *
 * Author: Copyright (c) 2009, Glenn Jones
 * Note: Some code and formatting borrowed from other parts of Applewin
 *
 */


/* 05/14/2009 - RGJ - Initial File creation. 
					- Based on harddisk.cpp. 
					- Updated resources
						show enable slot 7 checkbox
						show Hardisk or Apple SPI radio button
					- First stab is to just model a 2KB EPROM (WIP)
					    

*/

#include "StdAfx.h"
#pragma  hdrstop
#include "..\resource\resource.h"

/*
Memory map:
Refer to 65SPI datasheet for detailed information

    C0F0	(r)   SPI Data In
    C0F0    (w)   SPI Data Out 
	C0F1	(r)   SPI Status
	C0F1	(w)   SPI Control
	C0F2	(r/w) SCLK Divisor
	C0F3	(r/w) Slave Select
	C0F4	(r/w) EEPROM Bank select
*/

/*
Serial Peripheral Interface/ Pagable EEPROM emulation in Applewin.

Concept
	- Stage One: To emulate a pagable 28C256 (32KB) EEPROM. Plan to model after Rich Dreher's CFFA EEPROM support as much as possible (TBD)
		This will alow multiple devices to have there own C800 Space
		Map as follows
			0:c000	Alternate $cn00 space cannot have slot specific code 256B
			0:c100 Slot1 specific code (ProDOS block driver entry + Ip65 dispatch) 256B
			0:c200 Slot2 specific code (ProDOS block driver entry + Ip65 dispatch) 256B
			0:c300 Slot3 specific code (ProDOS block driver entry + Ip65 dispatch) 256B
			0:c400 Slot4 specific code (ProDOS block driver entry + Ip65 dispatch) 256B
			0:c500 Slot5 specific code (ProDOS block driver entry + Ip65 dispatch) 256B
			0:c600 Slot6 specific code (ProDOS block driver entry + Ip65 dispatch) 256B
			0:c700 Slot7 specific code (ProDOS block driver entry + Ip65 dispatch) 256B
			1:c800 $C800 common code bank 1 (ProDOS block driver) 2K
			2:c800 $C800 common code bank 2 (IP65 routines) 2K
			3:c800 $C800 common code bank 3 (IP65 routines) 2K
			4:c800 $C800 common code bank 4 (IP65 routines) 2K
			5:c800 $C800 common code bank 5 (IP65 routines) 2K
			6:c800 $C800 common code bank 6 (IP65 routines) 2K
			7:c800 $C800 common code bank 7 (IP65 routines) 2K

		EEPROM images supplied as ROM file, updateable via emulator (eventually)

    - Stage Two: To emulate 65SPI so tha muliple devices can be supported from one slot
		Possibilites include:
			SDcard support (Hard disk drive images)
			Ethernet Interface ENC28J60

Implemntation Specifics

  1. EEPROM

  2. 65SPI 

  3. Bugs
		None so far

*/




static bool g_bAPLSPI_Enabled = false;
static bool	g_bAPLSPI_RomLoaded = false;
static UINT g_uSlot = 7;

static BYTE g_spidata = 0;
static 	BYTE g_spistatus = 0;
static 	BYTE g_spiclkdiv = 0;
static 	BYTE g_spislaveSel = 0;
static 	BYTE g_c800bank = 0;

static BYTE __stdcall APLSPI_IO_EMUL (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);

static const DWORD APLSPIDRVR_SIZE = APPLE_SLOT_SIZE;

bool APLSPI_CardIsEnabled()
{
	return g_bAPLSPI_RomLoaded && g_bAPLSPI_Enabled;
}


void APLSPI_SetEnabled(bool bEnabled)
{
	if(g_bAPLSPI_Enabled == bEnabled)
		return;

	g_bAPLSPI_Enabled = bEnabled;

	SLOT7_SetType(SL7_APLSPI);

	// FIXME: For LoadConfiguration(), g_uSlot=7 (see definition at start of file)
	// . g_uSlot is only really setup by HD_Load_Rom(), later on
	RegisterIoHandler(g_uSlot, APLSPI_IO_EMUL, APLSPI_IO_EMUL, NULL, NULL, NULL, NULL);

	LPBYTE pCxRomPeripheral = MemGetCxRomPeripheral();
	if(pCxRomPeripheral == NULL)	// This will be NULL when called after loading value from Registry
		return;

	//

	if(g_bAPLSPI_Enabled)
		APLSPI_Load_Rom(pCxRomPeripheral, g_uSlot);
	else
		memset(pCxRomPeripheral + g_uSlot*256, 0, APLSPIDRVR_SIZE);
}

VOID APLSPI_Load_Rom(LPBYTE pCxRomPeripheral, UINT uSlot)
{
	if(!g_bAPLSPI_Enabled)
		return;

	HRSRC hResInfo = FindResource(NULL, MAKEINTRESOURCE(IDR_APLSPIDRVR_FW), "FIRMWARE");
	if(hResInfo == NULL)
		return;

	DWORD dwResSize = SizeofResource(NULL, hResInfo);
	if(dwResSize != APLSPIDRVR_SIZE)
		return;

	HGLOBAL hResData = LoadResource(NULL, hResInfo);
	if(hResData == NULL)
		return;

	BYTE* pData = (BYTE*) LockResource(hResData);	// NB. Don't need to unlock resource
	if(pData == NULL)
		return;

	g_uSlot = uSlot;
	memcpy(pCxRomPeripheral + uSlot*256, pData, APLSPIDRVR_SIZE);
	g_bAPLSPI_RomLoaded = true;
}


#define DEVICE_OK				0x00
//#define DEVICE_UNKNOWN_ERROR	0x03
//#define DEVICE_IO_ERROR			0x08

static BYTE __stdcall APLSPI_IO_EMUL (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft)
{
	BYTE r = DEVICE_OK;
	addr &= 0xFF;

	if (!APLSPI_CardIsEnabled())
		return r;


	if (bWrite == 0) // read
	{
	
		switch (addr)

		{
		case 0xF0:  // SPI Read Data
			{
				 r = g_spidata;
			}
			break;
		case 0xF1: // SPI Read Status
			{
				r = g_spistatus;
			}
			break;
		case 0xF2:  // Read SCLK Divisor
			{
				r = g_spiclkdiv;
			}
			break;
		case 0xF3:  // Read Slave Select
			{
				r = g_spislaveSel;
			}
			break;
		case 0xF4: // Read C800 Bank register
			{
				r = g_c800bank ;
			}
			break;

		default:
			return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
		
		}
	}
	else // write
	{
		switch (addr)
		{
		case 0xF0:  // SPI WRITE Data
			{
				 g_spidata = d;
			}
			break;
		case 0xF1: // SPI Write Control
			{
				g_spistatus = d;
			}
			break;
		case 0xF2:  // Write SCLK Divisor
			{
				g_spiclkdiv = d; 
			}
			break;
		case 0xF3:  // Write Slave Select
			{
				g_spislaveSel = d;
			}
			break;
		case 0xF4: // Write C800 Bank register
			{
				g_c800bank = d;
			}
			break;

		default:
			return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
		}
	}

	return r;
}