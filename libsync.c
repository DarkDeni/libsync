/* 
 * Copyright (C) 2011 Chris McClelland
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <makestuff.h>
#include <libusbwrap.h>
#include <usb.h>
#include <liberror.h>
#include <vendorCommands.h>
#include "libsync.h"

// -------------------------------------------------------------------------------------------------
// Declaration of private types & functions
// -------------------------------------------------------------------------------------------------

#define MAX_TRIES 10

static SyncStatus trySync(
	struct usb_dev_handle *deviceHandle, int outEndpoint, int inEndpoint, const char **error
) WARN_UNUSED_RESULT;

// -------------------------------------------------------------------------------------------------
// Public functions
// -------------------------------------------------------------------------------------------------

// Sync with the device
//
SyncStatus syncBulkEndpoints(
	struct usb_dev_handle *deviceHandle, SyncMode syncMode, const char **error)
{
	SyncStatus returnCode, sStatus;
	int uStatus;

	// Put the device in sync mode
	uStatus = usb_control_msg(
		deviceHandle,
		USB_ENDPOINT_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		CMD_MODE_STATUS,          // bRequest
		MODE_SYNC,                // wValue
		MODE_SYNC,                // wMask
		NULL,
		0,                        // wLength
		100                       // timeout (ms)
	);
	if ( uStatus < 0 ) {
		errRender(error, "syncBulkEndpoints(): Unable to enable sync mode: %s", usb_strerror());
		FAIL(SYNC_ENABLE);
	}

	if ( syncMode == SYNC_24 || syncMode == SYNC_BOTH ) {
		// Try to sync EP2OUT->EP4IN
		sStatus = trySync(deviceHandle, 2, 4, error);
		if ( sStatus != SYNC_SUCCESS ) {
			FAIL(sStatus);
		}
	}

	if ( syncMode == SYNC_68 || syncMode == SYNC_BOTH ) {
		// Try to sync EP6OUT->EP8IN
		sStatus = trySync(deviceHandle, 6, 8, error);
		if ( sStatus != SYNC_SUCCESS ) {
			FAIL(sStatus);
		}
	}

	// Bring the device out of sync mode
	uStatus = usb_control_msg(
		deviceHandle,
		USB_ENDPOINT_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		CMD_MODE_STATUS,          // bRequest
		0x0000,                   // wValue : off
		MODE_SYNC,                // wMask
		NULL,
		0,                        // wLength
		100                       // timeout (ms)
	);
	if ( uStatus < 0 ) {
		errRender(
			error, "syncBulkEndpoints(): Unable to enable sync mode: %s", usb_strerror());
		FAIL(SYNC_DISABLE);
	}
	return SYNC_SUCCESS;
cleanup:
	return returnCode;
}

// -------------------------------------------------------------------------------------------------
// Implementation of private functions
// -------------------------------------------------------------------------------------------------

static SyncStatus trySync(
	struct usb_dev_handle *deviceHandle, int outEndpoint, int inEndpoint, const char **error)
{
	SyncStatus returnCode;
	const uint32 hackLower = 0x6861636B;
	const uint32 hackUpper = 0x4841434B;
	int uStatus;
	union {
		uint32 lword;
		char bytes[512];
	} u;
	uint8 count = 0;
	do {
		uStatus = usb_bulk_read(deviceHandle, USB_ENDPOINT_IN | inEndpoint, u.bytes, 512, 100);
		//printf("CLEAN: Read %d bytes, starting with 0x%08lX\n", uStatus, u.lword);
	} while ( uStatus >= 0 );
	u.lword = 0xDEADDEAD;
	usb_bulk_write(deviceHandle, USB_ENDPOINT_OUT | outEndpoint, u.bytes, 4, 100);
	usb_bulk_write(deviceHandle, USB_ENDPOINT_OUT | outEndpoint, u.bytes, 4, 100);
	u.lword = hackLower;
	usb_bulk_write(deviceHandle, USB_ENDPOINT_OUT | outEndpoint, u.bytes, 4, 100);
	do {
		uStatus = usb_bulk_read(deviceHandle, USB_ENDPOINT_IN | inEndpoint, u.bytes, 4, 100);
		//printf("WAIT: Read %d bytes, starting with 0x%08lX\n", uStatus, u.lword);
		count++;
	} while ( (u.lword != hackUpper || uStatus < 0) && count < MAX_TRIES );
	if ( uStatus < 0 ) {
		errRender(
			error,
			"syncBulkEndpoints(): Sync of EP%dOUT->EP%dIN failed after %d attempts: %s",
			outEndpoint, inEndpoint, count, usb_strerror());
		FAIL(SYNC_FAILED);
	}
	if ( u.lword != hackUpper ) {
		errRender(
			error,
			"syncBulkEndpoints(): Sync of EP%dOUT->EP%dIN read back 0x%08lX instead of the expected 0x%08lX",
			outEndpoint, inEndpoint, u.lword, hackUpper);
		FAIL(SYNC_FAILED);
	}
	return SYNC_SUCCESS;
cleanup:
	return returnCode;
}
