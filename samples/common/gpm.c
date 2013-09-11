/*
 * Copyright (c) 2007-2013 Digi International Inc.,
 * All rights not expressly granted are reserved.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Digi International Inc. 11001 Bren Road East, Minnetonka, MN 55343
 * =======================================================================
 */
/*
 * Sample program demonstrating the General Purpose Memory API, used to access
 * flash storage on some XBee modules (868LP, 900HP, Wi-Fi).  This space is
 * available for host use, and is also used to stage firmware images before
 * installation.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../common/_atinter.h"
#include "parse_serial_args.h"

#include "xbee/byteorder.h"
#include "xbee/gpm.h"
#include "xbee/wpan.h"

xbee_dev_t my_xbee;
wpan_envelope_t envelope_self;

uint16_t blocks = 0;
uint16_t blocksize = 0;

FILE *upload_file = NULL;
uint32_t upload_offset = 0;
uint16_t upload_pagesize = 128;

int upload_next_page()
{
	uint16_t bytecount, maxcount, bytesread;
	int result;
	char buffer[XBEE_MAX_RFPAYLOAD];
	bool_t complete;

	complete = (upload_file == NULL || feof( upload_file));
	if (! complete)
	{
		bytecount = blocksize - (upload_offset % blocksize);
		maxcount = upload_pagesize == 0 ? xbee_gpm_max_write( &my_xbee.wpan_dev)
				: upload_pagesize;
		if (bytecount > maxcount)
		{
			bytecount = maxcount;
		}

		bytesread = fread( buffer, 1, bytecount, upload_file);
		complete = (bytesread == 0 && feof( upload_file));
	}

	if (complete)
	{
		puts( "upload complete");
		fclose( upload_file);
		upload_file = NULL;

		return 0;
	}

	result = xbee_gpm_write( &envelope_self, upload_offset / blocksize,
			upload_offset % blocksize, bytesread, buffer);
	printf( "sending %u bytes from offset %" PRIu32 " (result = %d)\n",
			bytesread, upload_offset, result);
	if (result == 0)
	{
		upload_offset += bytesread;
	}

	return result;
}

int start_upload( const char *file)
{
	if (blocksize == 0)
	{
		puts( "Can't start upload until after successful 'info' response.");
		return -EPERM;
	}

	if (file == NULL)
	{
		return -EINVAL;
	}

	upload_offset = 0;
	if (upload_file != NULL)
	{
		fclose( upload_file);
	}
	upload_file = fopen( file, "rb");
	if (upload_file == NULL)
	{
		printf( "Error %d, could not open '%s'.\n", errno, file);
		return -errno;
	}

	printf( "Uploading '%s' to GPM...\n", file);

	return upload_next_page();
}

int write_block( const char *file, uint16_t block)
{
    if (blocksize == 0)
    {
        puts( "Can't start upload until after successful 'info' response.");
        return -EPERM;
    }

    if (file == NULL)
    {
        return -EINVAL;
    }

    upload_offset = block * blocksize;
    if (upload_file != NULL)
    {
        fclose( upload_file);
    }
    upload_file = fopen( file, "rb");
    if (upload_file == NULL)
    {
        printf( "Error %d, could not open '%s'.\n", errno, file);
        return -errno;
    }

    printf( "Uploading '%s' to GPM...\n", file);

    return upload_next_page();
}

// function receiving GPM responses
int gpm_response( const wpan_envelope_t FAR *envelope, void FAR *context)
{
	const xbee_gpm_frame_t FAR *frame = envelope->payload;

	switch (frame->header.response.cmd_id)
	{
		case XBEE_GPM_CMD_PLATFORM_INFO_RESP:
		{
			blocks = be16toh( frame->header.response.block_num_be);
			blocksize = be16toh( frame->header.response.start_index_be);

			printf( "Platform Info: status 0x%02X, %u blocks, %u bytes/block = %"
					PRIu32 " bytes\n", frame->header.response.status,
					blocks, blocksize, (uint32_t) blocks * blocksize);
		}
		break;

		case XBEE_GPM_CMD_READ_RESP:
		{
			uint16_t block = be16toh( frame->header.response.block_num_be);
			uint16_t offset = be16toh( frame->header.response.start_index_be);
			uint16_t bytes = be16toh( frame->header.response.byte_count_be);

			printf( "Read %u bytes from offset %u of block %u:\n",
					bytes, offset, block);
			hex_dump( frame->data, bytes, HEX_DUMP_FLAG_OFFSET);
		}
		break;

		case XBEE_GPM_CMD_ERASE_RESP:
			printf( "Erase block %u response: status 0x%02X\n",
					be16toh( frame->header.response.block_num_be),
					frame->header.response.status);
			break;

		case XBEE_GPM_CMD_WRITE_RESP:
		case XBEE_GPM_CMD_ERASE_THEN_WRITE_RESP:
			printf( "Write to offset %u of block %u response: status 0x%02X\n",
					be16toh( frame->header.response.start_index_be),
					be16toh( frame->header.response.block_num_be),
					frame->header.response.status);
			if (upload_file != NULL && frame->header.response.status == 0)
			{
				upload_next_page();
			}
			break;

		case XBEE_GPM_CMD_FIRMWARE_VERIFY_RESP:
			printf( "Verify firmware response: status 0x%02X\n",
					frame->header.response.status);
			break;

		case XBEE_GPM_CMD_FIRMWARE_INSTALL_RESP:
			printf( "Install firmware response: status 0x%02X\n",
					frame->header.response.status);
			break;

		default:
			printf( "%u-byte GPM response:\n", envelope->length);
			hex_dump( envelope->payload, envelope->length, HEX_DUMP_FLAG_OFFSET);
			break;
	}

	return 0;
}

/////// endpoint table

// must be sorted by cluster ID
const wpan_cluster_table_entry_t digi_device_clusters[] =
{
	{ DIGI_CLUST_MEMORY_ACCESS, gpm_response, NULL,
		WPAN_CLUST_FLAG_INOUT | WPAN_CLUST_FLAG_NOT_ZCL },

	WPAN_CLUST_ENTRY_LIST_END
};

const wpan_endpoint_table_entry_t sample_endpoints[] = {
	{	WPAN_ENDPOINT_DIGI_DEVICE,		// endpoint
		WPAN_PROFILE_DIGI,				// profile ID
		NULL,									// endpoint handler
		NULL,									// ep_state
		0x0000,								// device ID
		0x00,									// version
		digi_device_clusters				// clusters
	},

	{ WPAN_ENDPOINT_END_OF_LIST }
};

////////// end of endpoint table

int parse_uint16( uint16_t *parsed, const char *text, int param_count)
{
	int index = 0;
	char *tail;
	unsigned long ul;

	if (text == NULL || parsed == NULL)
	{
		return -EINVAL;
	}

	for (;;)
	{
		ul = strtoul( text, &tail, 0);
		if (tail == text)
		{
			break;
		}
		if (index == param_count)
		{
			return -E2BIG;
		}
		parsed[index++] = (uint16_t) ul;
		text = tail;
	}

	return index;
}

void print_menu( void)
{
	puts( "help                           This list of options.");
	puts( "scan                           Initiate active scan.");
	puts( "quit                           Quit the program.");
	puts( "info                           Send platform info request.");
	puts( "read <block> <offset> <bytes>  Read data from GPM.");
	puts( "erase all                      Erase all of GPM.");
	puts( "erase <block>                  Erase a single block of GPM.");
	puts( "upload <filename>              Upload file to GPM.");
    puts( "block <block> <filename>       Upload file to specified block.");
	puts( "verify                         Verify firmware copied to GPM.");
	puts( "install                        Install firmware copied to GPM.");
	puts( "pagesize                       Report on current upload page size.");
	puts( "pagesize <bytes>               Page size for firmware uploads.");
	puts( "");
}

/*
	main

	Initiate communication with the XBee module, then accept AT commands from
	STDIO, pass them to the XBee module and print the result.
*/
int main( int argc, char *argv[])
{
   char cmdstr[80];
	int status;
	xbee_serial_t XBEE_SERPORT;
	uint16_t params[3];

	parse_serial_arguments( argc, argv, &XBEE_SERPORT);

	// initialize the serial and device layer for this XBee device
	if (xbee_dev_init( &my_xbee, &XBEE_SERPORT, NULL, NULL))
	{
		printf( "Failed to initialize device.\n");
		return 0;
	}

	// Initialize the WPAN layer of the XBee device driver.  This layer enables
	// endpoints and clusters, and is required for all ZigBee layers.
	xbee_wpan_init( &my_xbee, sample_endpoints);

	// Initialize the AT Command layer for this XBee device and have the
	// driver query it for basic information (hardware version, firmware version,
	// serial number, IEEE address, etc.)
	xbee_cmd_init_device( &my_xbee);
	printf( "Waiting for driver to query the XBee device...\n");
	do {
		xbee_dev_tick( &my_xbee);
		status = xbee_cmd_query_status( &my_xbee);
	} while (status == -EBUSY);
	if (status)
	{
		printf( "Error %d waiting for query to complete.\n", status);
	}

	// report on the settings
	xbee_dev_dump_settings( &my_xbee, XBEE_DEV_DUMP_FLAG_DEFAULT);

	xbee_gpm_envelope_local( &envelope_self, &my_xbee.wpan_dev);
	if (envelope_self.ieee_address.u[0] == 0x0000)
	{
		// We're connected to a Wi-Fi XBee and need to use an IP address
		// (localhost, 127.0.0.1) as the target.
		envelope_self.ieee_address.l[0] = 0;
		envelope_self.ieee_address.l[1] = htobe32( 0x7F000001);
	}

	// get flash info, for use by later commands
	xbee_gpm_get_flash_info( &envelope_self);

   while (1)
   {
      while (xbee_readline( cmdstr, sizeof cmdstr) == -EAGAIN)
      {
      	xbee_dev_tick( &my_xbee);
      }

		if (! strcmpi( cmdstr, "help") || ! strcmp( cmdstr, "?"))
		{
			print_menu();
		}
      else if (! strcmpi( cmdstr, "quit"))
      {
			return 0;
		}
      else if (! strcmpi( cmdstr, "info"))
      {
      	printf( "Sending platform info request (result %d)\n",
      			xbee_gpm_get_flash_info( &envelope_self));
      }
      else if (! strcmpi( cmdstr, "erase all"))
      {
   		printf( "Erasing entire GPM (result %d)\n",
   				xbee_gpm_erase_flash( &envelope_self));
      }
      else if (! strncmpi( cmdstr, "erase ", 6))
      {
      	if (blocksize == 0)
      	{
      		puts( "Need to get 'info' response to learn blocksize before"
      				"erasing a page.");
      	}
      	else if (parse_uint16( params, &cmdstr[6], 1) == 1)
      	{
      		printf( "Erasing block %u (result %d)\n", params[0],
      				xbee_gpm_erase_block( &envelope_self, params[0], blocksize));
      	}
      	else
      	{
      		printf( "Couldn't parse block number from [%s]\n", &cmdstr[6]);
      	}
      }
      else if (! strncmpi( cmdstr, "read", 4))
      {
      	if (parse_uint16( params, &cmdstr[5], 3) == 3)
      	{
      		printf( "Read %u bytes from offset %u of block %u (result %d)\n",
      				params[2], params[1], params[0],
      				xbee_gpm_read( &envelope_self,
      						params[0], params[1], params[2]));
      	}
      	else
      	{
      		printf( "Couldn't parse three values from [%s]\n", &cmdstr[5]);
      	}
      }
      else if (! strcmpi( cmdstr, "pagesize"))
      {
      	printf( "upload page size is %u\n", upload_pagesize);
      }
      else if (! strncmpi( cmdstr, "pagesize ", 9))
      {
      	if (parse_uint16( params, &cmdstr[9], 1) == 1)
			{
				if (params[0] > xbee_gpm_max_write( &my_xbee.wpan_dev))
				{
					printf( "page size of %u exceeds maximum of %u\n",
							params[0], xbee_gpm_max_write( &my_xbee.wpan_dev));
				}
				else
				{
					upload_pagesize = params[0];
					printf( "upload page size is now %u\n", upload_pagesize);
				}
			}
			else
			{
				printf( "Couldn't parse page size from [%s]\n", &cmdstr[9]);
			}
      }
      else if (! strncmpi( cmdstr, "upload ", 7))
      {
      	start_upload( &cmdstr[7]);
      }
      else if (! strcmpi( cmdstr, "verify"))
      {
   		printf( "Verify firmware in GPM (result %d)\n",
   				xbee_gpm_firmware_verify( &envelope_self));
      }
      else if (! strcmpi( cmdstr, "install"))
      {
   		printf( "Install firmware in GPM (result %d)\n",
   				xbee_gpm_firmware_install( &envelope_self));
      }
		else if (! strncmpi( cmdstr, "AT", 2))
		{
			process_command( &my_xbee, &cmdstr[2]);
		}
      else if (! strncmpi( cmdstr, "block ", 6))
	  {
          int i = 6;
          //find end of block param
          while((cmdstr[i] != ' ') && (cmdstr[i] != '\0'))i++;

          i++;
          
	      if (parse_uint16( params, &cmdstr[6], 1) == 1)
          {
              //filename, block, offset
              printf( "write file %s to block %u\n", &cmdstr[i], params[0]);
              
              write_block( &cmdstr[i],  params[0]);
          }
          else
          {
              printf( "Couldn't parse block number from %s\n", cmdstr);
          }
	  }
	  else
      {
        printf( "unknown command: '%s'\n", cmdstr);
	  }
   }
}

// Since we're not using a dynamic frame dispatch table, we need to define
// it here.
#include "xbee/atcmd.h"			// for XBEE_FRAME_HANDLE_LOCAL_AT
#include "xbee/device.h"		// for XBEE_FRAME_HANDLE_TX_STATUS
const xbee_dispatch_table_entry_t xbee_frame_handlers[] =
{
	XBEE_FRAME_HANDLE_LOCAL_AT,
	XBEE_FRAME_MODEM_STATUS_DEBUG,
	XBEE_FRAME_TRANSMIT_STATUS_DEBUG,
	XBEE_FRAME_HANDLE_RX_EXPLICIT,
	XBEE_FRAME_TABLE_END
};

