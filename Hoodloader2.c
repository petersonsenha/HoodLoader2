/*
			 LUFA Library
			 Copyright (C) Dean Camera, 2014.

			 dean [at] fourwalledcubicle [dot] com
			 www.lufa-lib.org
			 */

/*
  Copyright 2014  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaims all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
  */

/*
Copyright(c) 2014 NicoHood
See the readme for credit to other people.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/** \file
 *
 *  Main source file for the CDC class bootloader. This file contains the complete bootloader logic.
 */

#define  INCLUDE_FROM_BOOTLOADERCDC_C
#include "Hoodloader2.h"

/** Contains the current baud rate and other settings of the first virtual serial port. This must be retained as some
 *  operating systems will not open the port unless the settings can be set successfully.
 */
static CDC_LineEncoding_t LineEncoding = { .BaudRateBPS = 0,
.CharFormat = CDC_LINEENCODING_OneStopBit,
.ParityType = CDC_PARITY_None,
.DataBits = 8 };

/** Current address counter. This stores the current address of the FLASH or EEPROM as set by the host,
 *  and is used when reading or writing to the AVRs memory (either FLASH or EEPROM depending on the issued
 *  command.)
 */
static uint32_t CurrAddress;

/** Flag to indicate if the bootloader should be running, or should exit and allow the application code to run
 *  via a watchdog reset. When cleared the bootloader will exit, starting the watchdog and entering an infinite
 *  loop until the AVR restarts and the application runs.
 */
static bool RunBootloader = true;

/** Magic lock for forced application start. If the HWBE fuse is programmed and BOOTRST is unprogrammed, the bootloader
 *  will start if the /HWB line of the AVR is held low and the system is reset. However, if the /HWB line is still held
 *  low when the application attempts to start via a watchdog reset, the bootloader will re-start. If set to the value
 *  \ref MAGIC_BOOT_KEY the special init function \ref Application_Jump_Check() will force the application to start.
 */
uint16_t MagicBootKey ATTR_NO_INIT;


/** Special startup routine to check if the bootloader was started via a watchdog reset, and if the magic application
 *  start key has been loaded into \ref MagicBootKey. If the bootloader started via the watchdog and the key is valid,
 *  this will force the user application to start via a software jump.
 */
void Application_Jump_Check(void)
{
	bool JumpToApplication = false;

#if ((BOARD == BOARD_XPLAIN) || (BOARD == BOARD_XPLAIN_REV1))
	/* Disable JTAG debugging */
	JTAG_DISABLE();

	/* Enable pull-up on the JTAG TCK pin so we can use it to select the mode */
	PORTF |= (1 << 4);
	Delay_MS(10);

	/* If the TCK pin is not jumpered to ground, start the user application instead */
	JumpToApplication |= ((PINF & (1 << 4)) != 0);

	/* Re-enable JTAG debugging */
	JTAG_ENABLE();
#endif

	/* If the reset source was the bootloader and the key is correct, clear it and jump to the application */
	if ((MCUSR & (1 << WDRF)) && (MagicBootKey == MAGIC_BOOT_KEY))
		JumpToApplication |= true;

	/* If a request has been made to jump to the user application, honor it */
	if (JumpToApplication)
	{
		/* Turn off the watchdog */
		MCUSR &= ~(1 << WDRF);
		wdt_disable();

		/* Clear the boot key and jump to the user application */
		MagicBootKey = 0;

		// cppcheck-suppress constStatement
		((void(*)(void))0x0000)();
	}
}

/** Underlying data buffer for \ref USARTtoUSB_Buffer, where the stored bytes are located. */
#define BUFFER_SIZE 64 // 2^x is for better performence (32,64,128,256)
static uint8_t      USARTtoUSB_Buffer_Data[BUFFER_SIZE];
//TODO volatile count?
static uint8_t BufferCount = 0; // Number of bytes currently stored in the buffer
static uint8_t BufferIndex = 0; // position of the first buffer byte (Buffer out)
static uint8_t BufferEnd = 0; // position of the last buffer byte (Serial in)

// variable to determine if CDC baudrate is for the bootloader mode or not
//TODO volatile CDCAvtive?
static bool CDCActive = false;

/** Main program entry point. This routine configures the hardware required by the bootloader, then continuously
 *  runs the bootloader processing routine until instructed to soft-exit, or hard-reset via the watchdog to start
 *  the loaded application code.
 */
int main(void)
{
	/* Setup hardware required for the bootloader */
	SetupHardware();

	/* Enable global interrupts so that the USB stack can function */
	GlobalInterruptEnable();

	while (RunBootloader)
	{
		CDC_Task();
		USB_USBTask();
	}

	/* Disconnect from the host - USB interface will be reset later along with the AVR */
	USB_Detach();

	/* Unlock the forced application start mode of the bootloader if it is restarted */
	MagicBootKey = MAGIC_BOOT_KEY;

	/* Enable the watchdog and force a timeout to reset the AVR */
	wdt_enable(WDTO_250MS);

	for (;;);
}

/** Configures all hardware required for the bootloader. */
static void SetupHardware(void)
{
	/* Disable watchdog if enabled by bootloader/fuses */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	/* Disable clock division */
	clock_prescale_set(clock_div_1);

	/* Relocate the interrupt vector table to the bootloader section */
	MCUCR = (1 << IVCE);
	MCUCR = (1 << IVSEL);

	/* Initialize the USB and other board hardware drivers */
	USB_Init();

	// compacter setup for Leds, RX, TX, Reset Line
	ARDUINO_DDR |= LEDS_ALL_LEDS | (1 << PD3) | AVR_RESET_LINE_MASK;
	ARDUINO_PORT |= LEDS_ALL_LEDS | (1 << 2) | AVR_RESET_LINE_MASK;
}

/** Event handler for the USB_ConfigurationChanged event. This configures the device's endpoints ready
 *  to relay data to and from the attached USB host.
 */
void EVENT_USB_Device_ConfigurationChanged(void)
{
	/* Setup CDC Notification, Rx and Tx Endpoints */
	Endpoint_ConfigureEndpoint(CDC_NOTIFICATION_EPADDR, EP_TYPE_INTERRUPT,
		CDC_NOTIFICATION_EPSIZE, 1);

	Endpoint_ConfigureEndpoint(CDC_TX_EPADDR, EP_TYPE_BULK, CDC_TX_EPSIZE, CDC_TX_BANK_SIZE);

	Endpoint_ConfigureEndpoint(CDC_RX_EPADDR, EP_TYPE_BULK, CDC_RX_EPSIZE, CDC_RX_BANK_SIZE);
}

/** Event handler for the USB_ControlRequest event. This is used to catch and process control requests sent to
 *  the device from the USB host before passing along unhandled control requests to the library for processing
 *  internally.
 */
void EVENT_USB_Device_ControlRequest(void)
{
	/* Ignore any requests that aren't directed to the CDC interface */
	if ((USB_ControlRequest.bmRequestType & (CONTROL_REQTYPE_TYPE | CONTROL_REQTYPE_RECIPIENT)) !=
		(REQTYPE_CLASS | REQREC_INTERFACE))
	{
		return;
	}

	/* Process CDC specific control requests */
	switch (USB_ControlRequest.bRequest)
	{
	case CDC_REQ_GetLineEncoding:
		if (USB_ControlRequest.bmRequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
		{
			Endpoint_ClearSETUP();

			/* Write the line coding data to the control endpoint */
			// this one is not inline because its already used somewhere in the usb core, so it will dupe code
			Endpoint_Write_Control_Stream_LE(&LineEncoding, sizeof(CDC_LineEncoding_t));
			Endpoint_ClearOUT();
		}

		break;
	case CDC_REQ_SetLineEncoding:
		if (USB_ControlRequest.bmRequestType == (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
		{
			Endpoint_ClearSETUP();

			// Read the line coding data in from the host into the global struct (made inline)
			//Endpoint_Read_Control_Stream_LE(&LineEncoding, sizeof(CDC_LineEncoding_t));

			uint8_t Length = sizeof(CDC_LineEncoding_t);
			uint8_t* DataStream = (uint8_t*)&LineEncoding;

			bool skip = false;
			while (Length)
			{
				uint8_t USB_DeviceState_LCL = USB_DeviceState;

				if ((USB_DeviceState_LCL == DEVICE_STATE_Unattached) || (USB_DeviceState_LCL == DEVICE_STATE_Suspended) || (Endpoint_IsSETUPReceived())){
					skip = true;
					break;
				}

				if (Endpoint_IsOUTReceived())
				{
					while (Length && Endpoint_BytesInEndpoint())
					{
						*DataStream = Endpoint_Read_8();
						DataStream++;
						Length--;
					}

					Endpoint_ClearOUT();
				}
			}

			if (!skip)
				while (!(Endpoint_IsINReady()))
				{
				uint8_t USB_DeviceState_LCL = USB_DeviceState;

				if ((USB_DeviceState_LCL == DEVICE_STATE_Unattached) || (USB_DeviceState_LCL == DEVICE_STATE_Suspended))
					break;
				}

			// end of inline Endpoint_Read_Control_Stream_LE

			Endpoint_ClearIN();

			if (LineEncoding.BaudRateBPS == BAUDRATE_CDC_BOOTLOADER)
				CDCActive = true;
			else
				CDCActive = false;

			// reset buffer
			BufferCount = 0;
			BufferIndex = 0;
			BufferEnd = 0;

			// TODO else part?
			CDC_Device_LineEncodingChanged();

		}

		break;
	case CDC_REQ_SetControlLineState:
		if (USB_ControlRequest.bmRequestType == (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
		{
			Endpoint_ClearSETUP();
			Endpoint_ClearStatusStage();

			// check DTR state and reset the MCU
			if (!CDCActive && (USB_ControlRequest.wValue & CDC_CONTROL_LINE_OUT_DTR))
				AVR_RESET_LINE_PORT &= ~AVR_RESET_LINE_MASK;
			else
				AVR_RESET_LINE_PORT |= AVR_RESET_LINE_MASK;
		}

		break;
	}
}

/** Event handler for the CDC Class driver Line Encoding Changed event.
*
*  \param[in] CDCInterfaceInfo  Pointer to the CDC class interface configuration structure being referenced
*/
static void CDC_Device_LineEncodingChanged(void)
{
	uint8_t ConfigMask = 0;

	switch (LineEncoding.ParityType)
	{
	case CDC_PARITY_Odd:
		ConfigMask = ((1 << UPM11) | (1 << UPM10));
		break;
	case CDC_PARITY_Even:
		ConfigMask = (1 << UPM11);
		break;
	}

	if (LineEncoding.CharFormat == CDC_LINEENCODING_TwoStopBits)
		ConfigMask |= (1 << USBS1);

	switch (LineEncoding.DataBits)
	{
	case 6:
		ConfigMask |= (1 << UCSZ10);
		break;
	case 7:
		ConfigMask |= (1 << UCSZ11);
		break;
	case 8:
		ConfigMask |= ((1 << UCSZ11) | (1 << UCSZ10));
		break;
	}

	/* Keep the TX line held high (idle) while the USART is reconfigured */
	PORTD |= (1 << 3);

	/* Must turn off USART before reconfiguring it, otherwise incorrect operation may occur */
	UCSR1B = 0;
	UCSR1A = 0;
	UCSR1C = 0;

	/* Set the new baud rate before configuring the USART */
	UBRR1 = SERIAL_2X_UBBRVAL(LineEncoding.BaudRateBPS);

	//TODO needed?
	/* Set the new baud rate before configuring the USART */
	/* Special case 57600 baud for compatibility with the ATmega328 bootloader. */
	//UBRR1 = (LineEncoding.BaudRateBPS == 57600)
	//	? SERIAL_UBBRVAL(LineEncoding.BaudRateBPS)
	//	: SERIAL_2X_UBBRVAL(LineEncoding.BaudRateBPS);

	/* Reconfigure the USART in double speed mode for a wider baud rate range at the expense of accuracy */
	UCSR1C = ConfigMask;
	UCSR1A = (1 << U2X1);
	//UCSR1A = (LineEncoding.BaudRateBPS == 57600) ? 0 : (1 << U2X1);
	UCSR1B = ((1 << RXCIE1) | (1 << TXEN1) | (1 << RXEN1));

	/* Release the TX line after the USART has been reconfigured */
	PORTD &= ~(1 << 3);
}


/** ISR to manage the reception of data from the serial port, placing received bytes into a circular buffer
*  for later transmission to the host.
*/
ISR(USART1_RX_vect, ISR_BLOCK)
{
	// read the newest byte from the UART, important to clear interrupt flag!
	uint8_t ReceivedByte = UDR1;

	// only save the new byte if USB device is ready and buffer is not full
	if (!CDCActive && (USB_DeviceState == DEVICE_STATE_Configured) && (BufferCount <= BUFFER_SIZE)){
		// save new byte
		USARTtoUSB_Buffer_Data[BufferEnd++] = ReceivedByte;

		// increase the buffer position and wrap around if needed
		BufferEnd %= BUFFER_SIZE;

		// increase buffer count
		BufferCount++;
	}
}

/** Retrieves the next byte from the host in the CDC data OUT endpoint, and clears the endpoint bank if needed
 *  to allow reception of the next data packet from the host.
 *
 *  \return Next received byte from the host in the CDC data OUT endpoint
 */
static uint8_t FetchNextCommandByte(void)
{
	/* Select the OUT endpoint so that the next data byte can be read */
	Endpoint_SelectEndpoint(CDC_RX_EPADDR);

	/* If OUT endpoint empty, clear it and wait for the next packet from the host */
	while (!(Endpoint_IsReadWriteAllowed()))
	{
		Endpoint_ClearOUT();

		while (!(Endpoint_IsOUTReceived()))
		{
			if (USB_DeviceState == DEVICE_STATE_Unattached)
				return 0;
		}
	}

	/* Fetch the next byte from the OUT endpoint */
	return Endpoint_Read_8();
}

/** Writes the next response byte to the CDC data IN endpoint, and sends the endpoint back if needed to free up the
 *  bank when full ready for the next byte in the packet to the host.
 *
 *  \param[in] Response  Next response byte to send to the host
 */
static void WriteNextResponseByte(const uint8_t Response)
{
	/* Select the IN endpoint so that the next data byte can be written */
	Endpoint_SelectEndpoint(CDC_TX_EPADDR);

	/* If IN endpoint full, clear it and wait until ready for the next packet to the host */
	if (!(Endpoint_IsReadWriteAllowed()))
	{
		Endpoint_ClearIN();

		while (!(Endpoint_IsINReady()))
		{
			if (USB_DeviceState == DEVICE_STATE_Unattached)
				return;
		}
	}

	/* Write the next byte to the IN endpoint */
	Endpoint_Write_8(Response);
}


/** Task to read in AVR109 commands from the CDC data OUT endpoint, process them, perform the required actions
 *  and send the appropriate response back to the host.
 */
static void CDC_Task(void)
{
	/* Select the OUT endpoint */
	Endpoint_SelectEndpoint(CDC_RX_EPADDR);

	/* Check if endpoint has a command in it sent from the host */
	if (Endpoint_IsOUTReceived()){

		/* Read in the bootloader command (first byte sent from host) */
		uint8_t Command = FetchNextCommandByte();

		// USB-Serial Mode
		if (!CDCActive){
			/* Store received byte into the USART transmit buffer */
			//if (Endpoint_BytesInEndpoint())
			Serial_SendByte(Command);
			//if (Endpoint_BytesInEndpoint())
			//	Serial_SendByte(Endpoint_Read_8());

			// if endpoint is completely empty/read acknowledge that to the host
			if (!(Endpoint_BytesInEndpoint()))
				Endpoint_ClearOUT();
		}

		// Bootloader Mode
		else{

			if (Command == AVR109_COMMAND_ExitBootloader)
			{
				RunBootloader = false;

				/* Send confirmation byte back to the host */
				WriteNextResponseByte('\r');
			}
			//else if ((Command == AVR109_COMMAND_SetLED) || (Command == AVR109_COMMAND_ClearLED) ||
			//	(Command == AVR109_COMMAND_SelectDeviceType))
			//{
			//	FetchNextCommandByte();

			//	/* Send confirmation byte back to the host */
			//	WriteNextResponseByte('\r');
			//}
			//else if ((Command == AVR109_COMMAND_EnterProgrammingMode) || (Command == AVR109_COMMAND_LeaveProgrammingMode))
			//{
			//	/* Send confirmation byte back to the host */
			//	WriteNextResponseByte('\r');
			//}
			//else if (Command == AVR109_COMMAND_ReadPartCode)
			//{
			//	//TODO needed?
			//	/* Return ATMEGA128 part code - this is only to allow AVRProg to use the bootloader */
			//	WriteNextResponseByte(0x44);
			//	WriteNextResponseByte(0x00);
			//}
			//else if (Command == AVR109_COMMAND_ReadAutoAddressIncrement)
			//{
			//	/* Indicate auto-address increment is supported */
			//	WriteNextResponseByte('Y');
			//}
			//else if (Command == AVR109_COMMAND_SetCurrentAddress)
			//{
			//	/* Set the current address to that given by the host (translate 16-bit word address to byte address) */
			//	CurrAddress = (FetchNextCommandByte() << 9);
			//	CurrAddress |= (FetchNextCommandByte() << 1);

			//	/* Send confirmation byte back to the host */
			//	WriteNextResponseByte('\r');
			//}
			//else if (Command == AVR109_COMMAND_ReadBootloaderInterface)
			//{
			//	/* Indicate serial programmer back to the host */
			//	WriteNextResponseByte('S');
			//}
			//else if (Command == AVR109_COMMAND_ReadBootloaderIdentifier)
			//{
			//	/* Write the 7-byte software identifier to the endpoint */
			//	for (uint8_t CurrByte = 0; CurrByte < 7; CurrByte++)
			//		WriteNextResponseByte(SOFTWARE_IDENTIFIER[CurrByte]);
			//}
			//else if (Command == AVR109_COMMAND_ReadBootloaderSWVersion)
			//{
			//	WriteNextResponseByte('0' + BOOTLOADER_VERSION_MAJOR);
			//	WriteNextResponseByte('0' + BOOTLOADER_VERSION_MINOR);
			//}
			//else if (Command == AVR109_COMMAND_ReadSignature)
			//{
			//	WriteNextResponseByte(AVR_SIGNATURE_3);
			//	WriteNextResponseByte(AVR_SIGNATURE_2);
			//	WriteNextResponseByte(AVR_SIGNATURE_1);
			//}
			else if (Command == AVR109_COMMAND_EraseFLASH)
			{
				/* Clear the application section of flash */
				for (uint32_t CurrFlashAddress = 0; CurrFlashAddress < (uint32_t)BOOT_START_ADDR; CurrFlashAddress += SPM_PAGESIZE)
				{
					boot_page_erase(CurrFlashAddress);
					boot_spm_busy_wait();
					boot_page_write(CurrFlashAddress);
					boot_spm_busy_wait();
				}

				/* Send confirmation byte back to the host */
				WriteNextResponseByte('\r');
			}
#if !defined(NO_LOCK_BYTE_WRITE_SUPPORT)
			else if (Command == AVR109_COMMAND_WriteLockbits)
			{
				/* Set the lock bits to those given by the host */
				boot_lock_bits_set(FetchNextCommandByte());

				/* Send confirmation byte back to the host */
				WriteNextResponseByte('\r');
			}
#endif
			//else if (Command == AVR109_COMMAND_ReadLockbits)
			//{
			//	WriteNextResponseByte(boot_lock_fuse_bits_get(GET_LOCK_BITS));
			//}
			//else if (Command == AVR109_COMMAND_ReadLowFuses)
			//{
			//	WriteNextResponseByte(boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS));
			//}
			//else if (Command == AVR109_COMMAND_ReadHighFuses)
			//{
			//	WriteNextResponseByte(boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS));
			//}
			//else if (Command == AVR109_COMMAND_ReadExtendedFuses)
			//{
			//	WriteNextResponseByte(boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS));
			//}
#if !defined(NO_BLOCK_SUPPORT)
			else if (Command == AVR109_COMMAND_GetBlockWriteSupport)
			{
				WriteNextResponseByte('Y');

				/* Send block size to the host */
				WriteNextResponseByte(SPM_PAGESIZE >> 8);
				WriteNextResponseByte(SPM_PAGESIZE & 0xFF);
			}
			else if ((Command == AVR109_COMMAND_BlockWrite) || (Command == AVR109_COMMAND_BlockRead))
			{
				/* Delegate the block write/read to a separate function for clarity */
				ReadWriteMemoryBlock(Command);
			}
#endif
#if !defined(NO_FLASH_BYTE_SUPPORT)
			else if (Command == AVR109_COMMAND_FillFlashPageWordHigh)
			{
				/* Write the high byte to the current flash page */
				boot_page_fill(CurrAddress, FetchNextCommandByte());

				/* Send confirmation byte back to the host */
				WriteNextResponseByte('\r');
			}
			else if (Command == AVR109_COMMAND_FillFlashPageWordLow)
			{
				/* Write the low byte to the current flash page */
				boot_page_fill(CurrAddress | 0x01, FetchNextCommandByte());

				/* Increment the address */
				CurrAddress += 2;

				/* Send confirmation byte back to the host */
				WriteNextResponseByte('\r');
			}
			else if (Command == AVR109_COMMAND_WriteFlashPage)
			{
				/* Commit the flash page to memory */
				boot_page_write(CurrAddress);

				/* Wait until write operation has completed */
				boot_spm_busy_wait();

				/* Send confirmation byte back to the host */
				WriteNextResponseByte('\r');
			}
			else if (Command == AVR109_COMMAND_ReadFLASHWord)
			{
#if (FLASHEND > 0xFFFF)
				uint16_t ProgramWord = pgm_read_word_far(CurrAddress);
#else
				uint16_t ProgramWord = pgm_read_word(CurrAddress);
#endif

				WriteNextResponseByte(ProgramWord >> 8);
				WriteNextResponseByte(ProgramWord & 0xFF);
			}
#endif
#if !defined(NO_EEPROM_BYTE_SUPPORT)
			else if (Command == AVR109_COMMAND_WriteEEPROM)
			{
				/* Read the byte from the endpoint and write it to the EEPROM */
				eeprom_write_byte((uint8_t*)((intptr_t)(CurrAddress >> 1)), FetchNextCommandByte());

				/* Increment the address after use */
				CurrAddress += 2;

				/* Send confirmation byte back to the host */
				WriteNextResponseByte('\r');
			}
			else if (Command == AVR109_COMMAND_ReadEEPROM)
			{
				/* Read the EEPROM byte and write it to the endpoint */
				WriteNextResponseByte(eeprom_read_byte((uint8_t*)((intptr_t)(CurrAddress >> 1))));

				/* Increment the address after use */
				CurrAddress += 2;
			}
#endif
			else if (Command != AVR109_COMMAND_Sync)
			{
				/* Unknown (non-sync) command, return fail code */
				WriteNextResponseByte('?');
			}
		}
	}
	// nothing received in Bootloader mode
	else if (CDCActive)
		return;


	// Select the Serial Tx Endpoint
	Endpoint_SelectEndpoint(CDC_TX_EPADDR);

	bool IsEndpointFull;

	// USB-Serial
	if (!CDCActive){
		IsEndpointFull = false;

		// check if endpoint is ready for new data, last sending flushed without errors
		if (BufferCount && Endpoint_IsINReady()){

			// Read bytes from the USART receive buffer into the USB IN endpoint, max 1 bank size
			while (BufferCount && !IsEndpointFull){
				// Write the Data to the Endpoint */
				//Endpoint_Write_8(USARTtoUSB_Buffer_Data[BufferIndex++]);
				WriteNextResponseByte(USARTtoUSB_Buffer_Data[BufferIndex++]);

				// increase the buffer position and wrap around if needed
				BufferIndex %= BUFFER_SIZE;

				// turn off interrupts to save the value properly
				uint_reg_t CurrentGlobalInt = GetGlobalInterruptMask();
				GlobalInterruptDisable();

				// decrease buffer count
				BufferCount--;

				SetGlobalInterruptMask(CurrentGlobalInt);

				// Remember if the endpoint is completely full before clearing it
				IsEndpointFull = !(Endpoint_IsReadWriteAllowed());
			}
		}
		// no input / USB not ready
		else return;
	}

	// Bootloader mode
	else{
		// Remember if the endpoint is completely full before clearing it
		IsEndpointFull = !(Endpoint_IsReadWriteAllowed());
	}

	// flush the data if there was any

	// Remember if the endpoint is completely full before clearing it
	//bool IsEndpointFull = !(Endpoint_IsReadWriteAllowed());

	// Send the endpoint data to the host */
	Endpoint_ClearIN();

	// If a full endpoint's worth of data was sent, we need to send an empty packet afterwards to signal end of transfer
	if (IsEndpointFull)
	{
		// wait for the sending to flush
		while (!(Endpoint_IsINReady()))
		{
			if (USB_DeviceState == DEVICE_STATE_Unattached)
				return;
		}
		// send a zero length package
		Endpoint_ClearIN();
	}


	// Wait until the data has been sent to the host
	while (!(Endpoint_IsINReady()))
	{
		if (USB_DeviceState == DEVICE_STATE_Unattached)
			return;
	}

	if (CDCActive){

		/* Select the OUT endpoint */
		Endpoint_SelectEndpoint(CDC_RX_EPADDR);

		/* Acknowledge the command from the host */
		Endpoint_ClearOUT();
	}
}

#if !defined(NO_BLOCK_SUPPORT)
/** Reads or writes a block of EEPROM or FLASH memory to or from the appropriate CDC data endpoint, depending
*  on the AVR109 protocol command issued.
*
*  \param[in] Command  Single character AVR109 protocol command indicating what memory operation to perform
*/
static void ReadWriteMemoryBlock(const uint8_t Command)
{
	uint16_t BlockSize;
	char     MemoryType;

	uint8_t  HighByte = 0;
	uint8_t  LowByte = 0;

	BlockSize = (FetchNextCommandByte() << 8);
	BlockSize |= FetchNextCommandByte();

	MemoryType = FetchNextCommandByte();

	if ((MemoryType != MEMORY_TYPE_FLASH) && (MemoryType != MEMORY_TYPE_EEPROM))
	{
		/* Send error byte back to the host */
		WriteNextResponseByte('?');

		return;
	}

	/* Check if command is to read a memory block */
	if (Command == AVR109_COMMAND_BlockRead)
	{
		/* Re-enable RWW section */
		boot_rww_enable();

		while (BlockSize--)
		{
			if (MemoryType == MEMORY_TYPE_FLASH)
			{
				/* Read the next FLASH byte from the current FLASH page */
#if (FLASHEND > 0xFFFF)
				WriteNextResponseByte(pgm_read_byte_far(CurrAddress | HighByte));
#else
				WriteNextResponseByte(pgm_read_byte(CurrAddress | HighByte));
#endif

				/* If both bytes in current word have been read, increment the address counter */
				if (HighByte)
					CurrAddress += 2;

				HighByte = !HighByte;
			}
			else
			{
				/* Read the next EEPROM byte into the endpoint */
				WriteNextResponseByte(eeprom_read_byte((uint8_t*)(intptr_t)(CurrAddress >> 1)));

				/* Increment the address counter after use */
				CurrAddress += 2;
			}
		}
	}
	else
	{
		uint32_t PageStartAddress = CurrAddress;

		if (MemoryType == MEMORY_TYPE_FLASH)
		{
			boot_page_erase(PageStartAddress);
			boot_spm_busy_wait();
		}

		while (BlockSize--)
		{
			if (MemoryType == MEMORY_TYPE_FLASH)
			{
				/* If both bytes in current word have been written, increment the address counter */
				if (HighByte)
				{
					/* Write the next FLASH word to the current FLASH page */
					boot_page_fill(CurrAddress, ((FetchNextCommandByte() << 8) | LowByte));

					/* Increment the address counter after use */
					CurrAddress += 2;
				}
				else
				{
					LowByte = FetchNextCommandByte();
				}

				HighByte = !HighByte;
			}
			else
			{
				/* Write the next EEPROM byte from the endpoint */
				eeprom_write_byte((uint8_t*)((intptr_t)(CurrAddress >> 1)), FetchNextCommandByte());

				/* Increment the address counter after use */
				CurrAddress += 2;
			}
		}

		/* If in FLASH programming mode, commit the page after writing */
		if (MemoryType == MEMORY_TYPE_FLASH)
		{
			/* Commit the flash page to memory */
			boot_page_write(PageStartAddress);

			/* Wait until write operation has completed */
			boot_spm_busy_wait();
		}

		/* Send response byte back to the host */
		WriteNextResponseByte('\r');
	}
}
#endif