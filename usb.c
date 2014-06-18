#include "usb.h"

#include <stdbool.h>
#include <stdint.h>
#include <8051.h>
#include "mg84fl54bd.h"
#include "timer.h"

typedef uint8_t string_id;
typedef uint16_t bcd;

// USB to host byte order
#define utohs(x) (x) // Both USB and SDCC use little-endian order
// Host to USB byte order
#define htous(x) (x)

// Data types and constants from:
// [USB] Universal Serial Bus Specification, Revision 2.0, Chapter 9

// [USB] Table 9-2
typedef enum UsbRequestType {
	host_to_device = 0x00,
	device_to_host = 0x80,

	rtype_mask = 0x60,
	rtype_standard = 0x00,
	rtype_class = 0x20,
	rtype_vendor = 0x40,

	recipient_mask = 0x1F,
	recipient_device = 0x00,
	recipient_interface = 0x01,
	recipient_endpoint = 0x02,
	recipient_other = 0x03
} UsbRequestType;

// [USB] Table 9-4
typedef enum UsbRequest
{
	request_GET_STATUS = 0,
	request_CLEAR_FEATURE = 1,
	request_SET_FEATURE = 3,
	request_SET_ADDRESS = 5,
	request_GET_DESCRIPTOR = 6,
	request_SET_DESCRIPTOR = 7,
	request_GET_CONFIGURATION = 8,
	request_SET_CONFIGURATION = 9,
	request_GET_INTERFACE = 10,
	request_SET_INTERFACE = 11,
	request_SYNCH_FRAME = 12
} UsbRequest;

// [USB] Table 9-2
typedef struct UsbRequestSetup
{
	UsbRequestType bmRequestType;
	UsbRequest bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
} UsbRequestSetup;


// USB device state management

typedef enum UsbState {
	state_powered,
	state_default,
	state_address,
	state_configured
} UsbState;
UsbState usb_state = state_powered;

void set_state(UsbState new_state) __using(3)
{
	usb_state = new_state;
	P3_5 = !(new_state & 1);
	P3_6 = !(new_state & 2);
}


// Low-level functions

void usb_reset(void) __using(3)
{
	// Enable transmit and receive for endpoint 0
	EPINDEX = 0;
	EPCON = RXEPEN | TXEPEN;
	TXCON = TXCLR;
	RXCON = RXCLR;

	// Enable USB interrupts for reset/suspend/resume and endpoint 0 transmit/receive
	IEN = EFSR | EF;
	UIE = URXIE0 | UTXIE0;

	set_state(state_default);
}

void usb_receive(uint8_t __near * buffer, uint8_t size) __using(3)
{
	if (size < RXCNT) size = RXCNT;
	while (size --> 0)
	{
		*buffer++ = RXDAT;
	}
}

void usb_init(void)
{
	set_state(state_powered);

	// Set up clock
	CKCON2 |= EN_PLL;
	while (!(CKCON2 & PLL_RDY)) {}
	delay(2);
	// Enable USB
	CKCON2 |= EN_USB;

	// Enable USB interrupts for reset/suspend/resume
	IEN = EFSR;
	AUXIE |= EUSB;
	// Connect to USB host
	UPCON = CONEN;
}


// USB request handling

UsbRequestSetup request;

bool usb_request(void) __using(3)
{
	switch (request.bmRequestType & recipient_mask)
	{
	case recipient_device:
		return usb_device_request();
	}
	return false;
}


// USB interrupt handling

void usb_isr(void) __interrupt(15) __using(3)
{
	unsigned char upcon, uiflg;

	upcon = UPCON;
	if (upcon & URST)
	{
		UPCON = upcon & ~(URST | URSM | USUS) | URST;
		usb_reset();
		return;
	}

	uiflg = UIFLG;
	if (uiflg & URXD0) // Received some bytes
	{
		UIFLG = URXD0; // Acknowledge them
		EPINDEX = 0;
		if (RXSTAT & RXSETUP) // Got a complete Setup token
		{
			EPCON &= ~(TXSTL | RXSTL); // Recover from previous error if any
			RXSTAT &= ~EDOVW;
			usb_receive((unsigned char __near *)&request, sizeof(request));
			RXSTAT &= ~RXSETUP;
			RXCON |= RXFFRC;
			if (!usb_request())
			{
				EPCON |= TXSTL | RXSTL; // Signal error
			}
		}
		return;
	}
}
