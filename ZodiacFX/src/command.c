/**
 * @file
 * command.c
 *
 * This file contains the command line functions
 *
 */

/*
 * This file is part of the Zodiac FX P4 firmware.
 * Copyright (c) 2019 Northbound Networks.
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
 *
 * Author: Paul Zanna <paul@northboundnetworks.com>
 *		 
 *
 */

#include <asf.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "config_zodiac.h"
#include "command.h"
#include "common.h"
#include "conf_eth.h"
#include "eeprom.h"
#include "switch.h"
#include "lwip/def.h"
#include "timers.h"
#include "lwip/ip_addr.h"
#include "lwip/tcp.h"

#define RSTC_KEY  0xA5000000

// Global variables
extern struct zodiac_config Zodiac_Config;

extern int charcount, charcount_last;
bool trace = false;
extern struct tcp_pcb *tcp_pcb;
extern int totaltime;
extern int32_t ul_temp;
extern uint32_t uid_buf[4];
extern bool restart_required_outer;

// Local Variables
bool showintro = true;
uint8_t uCLIContext = 0;
struct arp_header arp_test;
uint8_t esc_char = 0;

// Internal Functions
void saveConfig(void);
void command_root(char *command, char *param1, char *param2, char *param3);
void command_config(char *command, char *param1, char *param2, char *param3);
void command_debug(char *command, char *param1, char *param2, char *param3);
void printintro(void);
void printhelp(void);

/*
*	Load the configuration settings from EEPROM
*
*/
void loadConfig(void)
{
	eeprom_read();
	return;
}

/*
*	Save the configuration settings to EEPROM
*
*/
void saveConfig(void)
{
	eeprom_write();
	return;
}

/*
*	Restart Zodiac FX
*
*/
void software_reset(void)
{
	for(int x = 0;x<100000;x++);	// Let the above message get sent to the terminal before detaching
	udc_detach();	// Detach the USB device before restart
	rstc_start_software_reset(RSTC);	// Software reset
	while (1);
}

/*
*	Main command line loop
*
*	@param str - pointer to the current command string
*	@param str_last - pointer to the last command string
*/
void task_command(char *str, char *str_last)
{
	char ch;
	char *command;
	char *param1;
	char *param2;
	char *param3;
	char *pch;

	while(udi_cdc_is_rx_ready()){
		ch = udi_cdc_getc();

		if (trace == true) trace = false;

		if (showintro == true)	// Show the intro only on the first key press
		{
			printintro();
			showintro = false;
			ch = 13;
		}
		if (ch == 27) // Is this the start of an escape key sequence?
		{
			esc_char = 1;
			return;
		}
		if (ch == 91 && esc_char == 1) // Second key in the escape key sequence?
		{
			esc_char = 2;
			return;
		}
		if (ch == 65 && esc_char == 2 && charcount == 0)	// Last char for the escape sequence for the up arrow (ascii codes 27,91,65)
		{
			strcpy(str, str_last);
			charcount = charcount_last;
			printf("%s",str);
			esc_char = 0;
			return;
		}

		if (ch == 13)	// Enter Key
		{
			printf("\r\n");
			str[charcount] = '\0';
			strcpy(str_last, str);
			charcount_last = charcount;
			pch = strtok (str," ");
			command = pch;
			pch = strtok (NULL, " ");
			param1 = pch;
			pch = strtok (NULL, " ");
			param2 = pch;
			pch = strtok (NULL, " ");
			param3 = pch;

			if (charcount > 0)
			{
				switch(uCLIContext)
				{
					case CLI_ROOT:
					command_root(command, param1, param2, param3);
					break;

					case CLI_CONFIG:
					command_config(command, param1, param2, param3);
					break;

					case CLI_DEBUG:
					command_debug(command, param1, param2, param3);
					break;
				};
			}

			switch(uCLIContext)
			{
				case CLI_ROOT:
				printf("%s# ",Zodiac_Config.device_name);
				break;

				case CLI_CONFIG:
				printf("%s(config)# ",Zodiac_Config.device_name);
				break;

				case CLI_DEBUG:
				printf("%s(debug)# ",Zodiac_Config.device_name);
				break;
			};
			charcount = 0;
			str[0] = '\0';
			esc_char = 0;
			return;

		} else if ((ch == 127 || ch == 8) && charcount > 0)	// Backspace key
		{
			charcount--;
			char tempstr[64];
			tempstr[0] = '\0';
			strncat(tempstr,str,charcount);
			strcpy(str, tempstr);
			printf("%c",ch); // echo to output
			esc_char = 0;
			return;

		} else if (charcount < 63 && ch > 31 && ch < 127 && esc_char == 0)	// Alphanumeric key
		{
			strncat(str,&ch,1);
			charcount++;
			printf("%c",ch); // echo to output
			esc_char = 0;
			return;
		}

		if (esc_char > 0) esc_char = 0; // If the escape key wasn't up arrow (ascii 65) then clear to flag
	}
}

/*
*	Commands within the root context
*
*	@param command - pointer to the command string
*	@param param1 - pointer to parameter 1
*	@param param2- pointer to parameter 2
*	@param param2 - pointer to parameter 3
*/
void command_root(char *command, char *param1, char *param2, char *param3)
{
	// Change context
	if (strcmp(command, "config")==0){
		uCLIContext = CLI_CONFIG;
		return;
	}

	if (strcmp(command, "debug")==0){
		uCLIContext = CLI_DEBUG;
		return;
	}

	
	// Display help
	if (strcmp(command, "help") == 0)
	{
		printhelp();
		return;

	}

	// Display firmware version
	if (strcmp(command, "show") == 0 && strcmp(param1, "version") == 0)
	{
		printf("Firmware version: %s\r\n\n",VERSION);
		return;
	}

	// Display ports statics
	if (strcmp(command, "show") == 0 && strcmp(param1, "ports") == 0)
	{
		int i;
		printf("\r\n-------------------------------------------------------------------------\r\n");
		for (i=0;i<TOTAL_PORTS;i++)
		{

			printf("\r\nPort %d\r\n",i+1);
			if (i > 3)
			{
				printf(" VLAN type: Default\r\n");
				printf(" VLAN ID: n/a\r\n");
			} else
			{
				for (int x=0;x<MAX_VLANS;x++)
				{
					if (Zodiac_Config.vlan_list[x].portmap[i] == 1)
					{
						if (Zodiac_Config.vlan_list[x].uVlanType == 0) printf(" VLAN type: Unassigned\r\n");
						if (Zodiac_Config.vlan_list[x].uVlanType == 1) printf(" VLAN type: Default\r\n");
						printf(" VLAN ID: %d\r\n", Zodiac_Config.vlan_list[x].uVlanID);
					}
				}
			}

		}
		printf("\r\n-------------------------------------------------------------------------\r\n\n");

		return;
	}

	// Display Config
	if (strcmp(command, "show")==0 && strcmp(param1, "status")==0){

		printf("\r\n-------------------------------------------------------------------------\r\n");
		printf("Device Status\r\n");
		printf(" CPU UID: %d-%d-%d-%d\r\n", uid_buf[0], uid_buf[1], uid_buf[2], uid_buf[3]);
		printf(" Firmware Version: %s\r\n",VERSION);
		printf(" CPU Temp: %d C\r\n", (int)ul_temp);
		printf("\r\n-------------------------------------------------------------------------\r\n\n");
		return;
	}

	// Build shortcut - b XX:XX, where XX:XX are the last 4 digits of the new mac address
	if (strcmp(command, "b")==0)
	{
		uint8_t mac5,mac6;
		sscanf(param1, "%x:%x", &mac5, &mac6);
		Zodiac_Config.MAC_address[0] = 0x70;
		Zodiac_Config.MAC_address[1] = 0xb3;
		Zodiac_Config.MAC_address[2] = 0xd5;
		Zodiac_Config.MAC_address[3] = 0x6c;
		Zodiac_Config.MAC_address[4] = mac5;
		Zodiac_Config.MAC_address[5] = mac6;

		struct zodiac_config reset_config =
		{
			"Zodiac_FX_P4",		// Name
			0,0,0,0,0,0,		// MAC Address
			10,0,1,99,			// IP Address
			255,255,255,0,		// Netmask
			10,0,1,1,			// Gateway Address
		};
		memset(&reset_config.vlan_list, 0, sizeof(struct virtlan)* MAX_VLANS); // Clear vlan array

		// Config VLAN 100
		sprintf(&reset_config.vlan_list[0].cVlanName, "Default");	// Vlan name
		reset_config.vlan_list[0].portmap[0] = 1;		// Assign port 1 to this vlan
		reset_config.vlan_list[0].portmap[1] = 1;		// Assign port 2 to this vlan
		reset_config.vlan_list[0].portmap[2] = 1;		// Assign port 3 to this vlan
		reset_config.vlan_list[0].portmap[3] = 1;		// Assign port 4 to this vlan
		reset_config.vlan_list[0].uActive = 1;		// Vlan is active
		reset_config.vlan_list[0].uVlanID = 100;	// Vlan ID is 100
		reset_config.vlan_list[0].uVlanType = 1;	// Set as the default Vlan
		reset_config.vlan_list[0].uTagged = 0;		// Set as untagged

		memcpy(&reset_config.MAC_address, &Zodiac_Config.MAC_address, 6);		// Copy over existing MAC address so it is not reset
		memcpy(&Zodiac_Config, &reset_config, sizeof(struct zodiac_config));
		saveConfig();
		printf("Setup complete, MAC Address = %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\r\n",Zodiac_Config.MAC_address[0], Zodiac_Config.MAC_address[1], Zodiac_Config.MAC_address[2], Zodiac_Config.MAC_address[3], Zodiac_Config.MAC_address[4], Zodiac_Config.MAC_address[5]);
		return;
	}

	// Build shortcut - c to show config is saved
	if (strcmp(command, "c")==0)
	{
		printf("\r\n");
		printf("Build Configuration Check\r\n");
		printf("-------------------------\r\n");
		printf(" Name: %s\r\n",Zodiac_Config.device_name);
		printf(" MAC Address: %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\r\n",Zodiac_Config.MAC_address[0], Zodiac_Config.MAC_address[1], Zodiac_Config.MAC_address[2], Zodiac_Config.MAC_address[3], Zodiac_Config.MAC_address[4], Zodiac_Config.MAC_address[5]);
		printf(" IP Address: %d.%d.%d.%d\r\n" , Zodiac_Config.IP_address[0], Zodiac_Config.IP_address[1], Zodiac_Config.IP_address[2], Zodiac_Config.IP_address[3]);
		printf(" Netmask: %d.%d.%d.%d\r\n" , Zodiac_Config.netmask[0], Zodiac_Config.netmask[1], Zodiac_Config.netmask[2], Zodiac_Config.netmask[3]);
		printf(" Gateway: %d.%d.%d.%d\r\n" , Zodiac_Config.gateway_address[0], Zodiac_Config.gateway_address[1], Zodiac_Config.gateway_address[2], Zodiac_Config.gateway_address[3]);
		printf("\r\n\n");
		return;
	}

	// Restart switch	
	if (strcmp(command, "restart")==0)
	{
		printf("Restarting the Zodiac FX, please reopen your terminal application.\r\n");
		software_reset();
	}

	// Unknown Command
	printf("Unknown command\r\n");
	return;
}

/*
*	Commands within the config context
*
*	@param command - pointer to the command string
*	@param param1 - pointer to parameter 1
*	@param param2- pointer to parameter 2
*	@param param2 - pointer to parameter 3
*/
void command_config(char *command, char *param1, char *param2, char *param3)
{
	// Return to root context
	if (strcmp(command, "exit")==0){
		uCLIContext = CLI_ROOT;
		return;
	}

	// Display help
	if (strcmp(command, "help") == 0)
	{
		printhelp();
		return;
	}

	// Load config
	if (strcmp(command, "load")==0){
		loadConfig();
		return;
	}

	// Save config
	if (strcmp(command, "save")==0){
		saveConfig();
		return;
	}

	// Restart switch
	if (strcmp(command, "restart")==0)
	{
		printf("Restarting the Zodiac FX, please reopen your terminal application.\r\n");
		software_reset();
	}
	
	// Display Config
	if (strcmp(command, "show")==0 && strcmp(param1, "config")==0){
		printf("\r\n-------------------------------------------------------------------------\r\n");
		printf("Configuration\r\n");
		printf(" Name: %s\r\n",Zodiac_Config.device_name);
		printf(" MAC Address: %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\r\n",Zodiac_Config.MAC_address[0], Zodiac_Config.MAC_address[1], Zodiac_Config.MAC_address[2], Zodiac_Config.MAC_address[3], Zodiac_Config.MAC_address[4], Zodiac_Config.MAC_address[5]);
		printf(" IP Address: %d.%d.%d.%d\r\n" , Zodiac_Config.IP_address[0], Zodiac_Config.IP_address[1], Zodiac_Config.IP_address[2], Zodiac_Config.IP_address[3]);
		printf(" Netmask: %d.%d.%d.%d\r\n" , Zodiac_Config.netmask[0], Zodiac_Config.netmask[1], Zodiac_Config.netmask[2], Zodiac_Config.netmask[3]);
		printf(" Gateway: %d.%d.%d.%d\r\n" , Zodiac_Config.gateway_address[0], Zodiac_Config.gateway_address[1], Zodiac_Config.gateway_address[2], Zodiac_Config.gateway_address[3]);
		printf("\r\n-------------------------------------------------------------------------\r\n\n");
		return;
	}

	// Display VLANs
	if (strcmp(command, "show")==0 && strcmp(param1, "vlans")==0){
		int x;
		printf("\r\n\tVLAN ID\t\tName\t\t\tType\t\tTag\r\n");
		printf("-------------------------------------------------------------------------------\r\n");
		for (x=0;x<MAX_VLANS;x++)
		{
			if (Zodiac_Config.vlan_list[x].uActive == 1)
			{
				printf("\t%d\t\t'%s'\t\t",Zodiac_Config.vlan_list[x].uVlanID, Zodiac_Config.vlan_list[x].cVlanName);
				if (Zodiac_Config.vlan_list[x].uVlanType == 0) printf("Undefined\t");
				if (Zodiac_Config.vlan_list[x].uVlanType == 1) printf("Default\t");
				if (Zodiac_Config.vlan_list[x].uTagged == 0) printf("Untagged\r\n");
				if (Zodiac_Config.vlan_list[x].uTagged == 1) printf("Tagged\r\n");
			}
		}
		printf("\r\n-------------------------------------------------------------------------------\r\n\n");
		return;
	}

//
//
// VLAN commands
//
//

	// Add new VLAN
	if (strcmp(command, "add")==0 && strcmp(param1, "vlan")==0)
	{
		int v;
		for(v=0;v<MAX_VLANS;v++)
		{
			if(Zodiac_Config.vlan_list[v].uActive != 1)
			{
				int namelen = strlen(param3);
				Zodiac_Config.vlan_list[v].uActive = 1;
				sscanf(param2, "%d", &Zodiac_Config.vlan_list[v].uVlanID);
				if (namelen > 15 ) namelen = 15; // Make sure name is less then 16 characters
				sprintf(Zodiac_Config.vlan_list[v].cVlanName, param3, namelen);
				printf("Added VLAN %d '%s'\r\n",Zodiac_Config.vlan_list[v].uVlanID, Zodiac_Config.vlan_list[v].cVlanName);
				return;
			}
		}
		// Can't find an empty VLAN slot
		printf("No more VLANs available\r\n");
		return;
	}

	// Delete an existing VLAN
	if (strcmp(command, "delete")==0 && strcmp(param1, "vlan")==0)
	{
		int vlanid;
		sscanf(param2, "%d", &vlanid);
		for (int x=0;x<MAX_VLANS;x++)
		{
			if(Zodiac_Config.vlan_list[x].uVlanID == vlanid)
			{
				Zodiac_Config.vlan_list[x].uActive = 0;
				Zodiac_Config.vlan_list[x].uVlanType = 0;
				Zodiac_Config.vlan_list[x].uTagged = 0;
				Zodiac_Config.vlan_list[x].uVlanID = 0;
				printf("VLAN %d deleted\r\n",vlanid);
				return;
			}
		}
			printf("Unknown VLAN ID\r\n");
			return;
	}

	// Set VLAN type
	if (strcmp(command, "set")==0 && strcmp(param1, "vlan-type")==0)
	{
		int vlanid;
		sscanf(param2, "%d", &vlanid);
		for (int x=0;x<MAX_VLANS;x++)
		{
			if(Zodiac_Config.vlan_list[x].uVlanID == vlanid)
			{
				if(strcmp(param3, "default")==0){
					Zodiac_Config.vlan_list[x].uVlanType = 1;
					printf("VLAN %d set as Default\r\n",vlanid);
					return;
				}
				printf("Unknown VLAN type\r\n");
				return;
			}
		}
			printf("Unknown VLAN ID\r\n");
			return;
	}

	// Set VLAN tagging
	if (strcmp(command, "set")==0 && strcmp(param1, "vlan-tag")==0)
	{
		int vlanid;
		sscanf(param2, "%d", &vlanid);
		for (int x=0;x<MAX_VLANS;x++)
		{
			if(Zodiac_Config.vlan_list[x].uVlanID == vlanid)
			{
				if(strcmp(param3, "untagged")==0){
					Zodiac_Config.vlan_list[x].uTagged = 0;
					printf("VLAN %d set to untagged\r\n",vlanid);
					return;
				}
				if(strcmp(param3, "tagged")==0){
					Zodiac_Config.vlan_list[x].uTagged = 1;
					printf("VLAN %d set to tagged\r\n",vlanid);
					return;
				}
				printf("Unknown VLAN tag setting\r\n");
				return;
			}
		}
		printf("Unknown VLAN ID\r\n");
		return;
	}
	
	// Add port to VLAN
	if (strcmp(command, "add")==0 && strcmp(param1, "vlan-port")==0)
	{
		int vlanid, port, x;
		sscanf(param2, "%d", &vlanid);
		sscanf(param3, "%d", &port);

		if (port < 1 || port > 4){
			printf("Invalid port number, ports are numbered 1 - 4\r\n");
			return;
		}

		// Check if the port is already assigned to a VLAN
		for (x=0;x<MAX_VLANS;x++){
			if(Zodiac_Config.vlan_list[x].portmap[port-1] == 1)
			{
				printf("Port %d is already assigned to VLAN %d\r\n", port, Zodiac_Config.vlan_list[x].uVlanID);
				return;
			}
		}

		// Assign the port to the requested VLAN
		for (x=0;x<MAX_VLANS;x++)
		{
			if(Zodiac_Config.vlan_list[x].uVlanID == vlanid)
			{
				if(Zodiac_Config.vlan_list[x].portmap[port-1] == 0  || Zodiac_Config.vlan_list[x].portmap[port-1] > 1 ){
					Zodiac_Config.vlan_list[x].portmap[port-1] = 1;
					printf("Port %d is now assigned to VLAN %d\r\n", port, vlanid);
					return;
				}
			}
		}
			printf("Unknown VLAN ID\r\n");
			return;
	}

	// Delete a port from a VLAN
	if (strcmp(command, "delete")==0 && strcmp(param1, "vlan-port")==0)
	{
		int port, x;
		sscanf(param2, "%d", &port);

		if (port < 1 || port > 4){
			printf("Invalid port number, ports are numbered 1 - 4\r\n");
			return;
		}

		// Check if the port is already assigned to a VLAN
		for (x=0;x<MAX_VLANS;x++){
			if(Zodiac_Config.vlan_list[x].portmap[port-1] == 1)
			{
				Zodiac_Config.vlan_list[x].portmap[port-1] = 0;
				return;
			}
		}
			printf("Port %d is not assigned to this VLAN\r\n",port);
			return;
	}

//
//
// Configuration commands
//
//

	// Set Device Name
	if (strcmp(command, "set")==0 && strcmp(param1, "name")==0)
	{
		uint8_t namelen = strlen(param2);
		if (namelen > 15 ) namelen = 15; // Make sure name is less then 16 characters
		sprintf(Zodiac_Config.device_name, param2, namelen);
		printf("Device name set to '%s'\r\n",Zodiac_Config.device_name);
		return;
	}

	// Set MAC Address
	if (strcmp(command, "set")==0 && strcmp(param1, "mac-address")==0)
	{
		int mac1,mac2,mac3,mac4,mac5,mac6;
		if (strlen(param2) != 17 )
		{
			printf("incorrect format\r\n");
			return;
		}
		sscanf(param2, "%x:%x:%x:%x:%x:%x", &mac1, &mac2, &mac3, &mac4, &mac5, &mac6);
		Zodiac_Config.MAC_address[0] = mac1;
		Zodiac_Config.MAC_address[1] = mac2;
		Zodiac_Config.MAC_address[2] = mac3;
		Zodiac_Config.MAC_address[3] = mac4;
		Zodiac_Config.MAC_address[4] = mac5;
		Zodiac_Config.MAC_address[5] = mac6;
		printf("MAC Address set to %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\r\n",Zodiac_Config.MAC_address[0], Zodiac_Config.MAC_address[1], Zodiac_Config.MAC_address[2], Zodiac_Config.MAC_address[3], Zodiac_Config.MAC_address[4], Zodiac_Config.MAC_address[5]);
		return;
	}

	// Set IP Address
	if (strcmp(command, "set")==0 && strcmp(param1, "ip-address")==0)
	{
		int ip1,ip2,ip3,ip4;
		if (strlen(param2) > 15 )
		{
			printf("incorrect format\r");
			return;
		}
		sscanf(param2, "%d.%d.%d.%d", &ip1, &ip2,&ip3,&ip4);
		Zodiac_Config.IP_address[0] = ip1;
		Zodiac_Config.IP_address[1] = ip2;
		Zodiac_Config.IP_address[2] = ip3;
		Zodiac_Config.IP_address[3] = ip4;
		printf("IP Address set to %d.%d.%d.%d\r\n" , Zodiac_Config.IP_address[0], Zodiac_Config.IP_address[1], Zodiac_Config.IP_address[2], Zodiac_Config.IP_address[3]);
		return;
	}

	// Set Netmask Address
	if (strcmp(command, "set")==0 && strcmp(param1, "netmask")==0)
	{
		int nm1,nm2,nm3,nm4;
		if (strlen(param2) > 15 )
		{
			printf("incorrect format\r\n");
			return;
		}
		sscanf(param2, "%d.%d.%d.%d", &nm1, &nm2,&nm3,&nm4);
		Zodiac_Config.netmask[0] = nm1;
		Zodiac_Config.netmask[1] = nm2;
		Zodiac_Config.netmask[2] = nm3;
		Zodiac_Config.netmask[3] = nm4;
		printf("Netmask set to %d.%d.%d.%d\r\n" , Zodiac_Config.netmask[0], Zodiac_Config.netmask[1], Zodiac_Config.netmask[2], Zodiac_Config.netmask[3]);
		return;
	}

	// Set Gateway Address
	if (strcmp(command, "set")==0 && strcmp(param1, "gateway")==0)
	{
		int gw1,gw2,gw3,gw4;
		if (strlen(param2) > 15 )
		{
			printf("incorrect format\r\n");
			return;
		}
		sscanf(param2, "%d.%d.%d.%d", &gw1, &gw2,&gw3,&gw4);
		Zodiac_Config.gateway_address[0] = gw1;
		Zodiac_Config.gateway_address[1] = gw2;
		Zodiac_Config.gateway_address[2] = gw3;
		Zodiac_Config.gateway_address[3] = gw4;
		printf("Gateway set to %d.%d.%d.%d\r\n" , Zodiac_Config.gateway_address[0], Zodiac_Config.gateway_address[1], Zodiac_Config.gateway_address[2], Zodiac_Config.gateway_address[3]);
		return;
	}

	// Reset the device to a basic configuration
	if (strcmp(command, "factory")==0 && strcmp(param1, "reset")==0)
	{
		struct zodiac_config reset_config =
		{
			"Zodiac_FX_P4",		// Name
			0,0,0,0,0,0,		// MAC Address
			10,0,1,99,			// IP Address
			255,255,255,0,		// Netmask
			10,0,1,1,			// Gateway Address
		};
		memset(&reset_config.vlan_list, 0, sizeof(struct virtlan)* MAX_VLANS); // Clear vlan array

		// Config VLAN 100
		sprintf(&reset_config.vlan_list[0].cVlanName, "Default");	// Vlan name
		reset_config.vlan_list[0].portmap[0] = 1;		// Assign port 1 to this vlan
		reset_config.vlan_list[0].portmap[1] = 1;		// Assign port 2 to this vlan
		reset_config.vlan_list[0].portmap[2] = 1;		// Assign port 3 to this vlan
		reset_config.vlan_list[0].portmap[3] = 1;		// Assign port 4 to this vlan
		reset_config.vlan_list[0].uActive = 1;		// Vlan is active
		reset_config.vlan_list[0].uVlanID = 100;	// Vlan ID is 100
		reset_config.vlan_list[0].uVlanType = 1;	// Set as the default Vlan
		reset_config.vlan_list[0].uTagged = 0;		// Set as untagged
		
		memcpy(&reset_config.MAC_address, &Zodiac_Config.MAC_address, 6);		// Copy over existing MAC address so it is not reset
		memcpy(&Zodiac_Config, &reset_config, sizeof(struct zodiac_config));
		saveConfig();
		return;
	}


	
		
	// Unknown Command
	printf("Unknown command\r\n");
	return;
}

/*
*	Commands within the debug context
*
*	@param command - pointer to the command string
*	@param param1 - pointer to parameter 1
*	@param param2- pointer to parameter 2
*	@param param2 - pointer to parameter 3
*/
void command_debug(char *command, char *param1, char *param2, char *param3)
{
	if (strcmp(command, "exit")==0){
		uCLIContext = CLI_ROOT;
		return;
	}

	// Display help
	if (strcmp(command, "help") == 0)
	{
		printhelp();
		return;

	}

	if (strcmp(command, "read")==0)
	{
		int n = switch_read(atoi(param1));
		printf("Register %d = 0x%.2X\r\n\n", atoi(param1), n);
		return;
	}

	if (strcmp(command, "write")==0)
	{
		int n = switch_write(atoi(param1),atoi(param2));
		printf("Register %d = 0x%.2X\r\n\n", atoi(param1),n);
		return;
	}

	if (strcmp(command, "trace")==0)
	{
		trace = true;
		printf("Starting trace...\r\n");
		return;
	}	
	
	// Unknown Command response
	printf("Unknown command\r\n");
	return;
}
/*
*	Print the intro screen
*	ASCII art generated using Slant font from http://patorjk.com/software/taag/
*
*/
void printintro(void)
{
	printf("\r\n");
	printf(" _____             ___               _______  __             ____  __ __\r\n");
	printf("/__  /  ____  ____/ (_)___ ______   / ____/ |/ /            / __ \\/ // /\r\n");
	printf("  / /  / __ \\/ __  / / __ `/ ___/  / /_   |   /   ______   / /_/ / // /_\r\n");
	printf(" / /__/ /_/ / /_/ / / /_/ / /__   / __/  /   |   /_____/  / ____/__  __/\r\n");
	printf("/____/\\____/\\__,_/_/\\__,_/\\___/  /_/    /_/|_|           /_/      /_/\r\n");
	printf("by Northbound Networks\r\n");
	printf("\r\n\n");
	printf("Type 'help' for a list of available commands\r\n");
	return;
}

/*
*	Print a list of available commands
*
*
*/
void printhelp(void)
{
	printf("\r\n");
	printf("The following commands are currently available:\r\n");
	printf("\r\n");
	printf("Base:\r\n");
	printf(" config\r\n");
	printf(" debug\r\n");
	printf(" update\r\n");
	printf(" show status\r\n");
	printf(" show version\r\n");
	printf(" show ports\r\n");
	printf(" restart\r\n");
	printf(" help\r\n");
	printf("\r\n");
	printf("Config:\r\n");
	printf(" save\r\n");
	printf(" restart\r\n");
	printf(" show config\r\n");
	printf(" show vlans\r\n");
	printf(" set name <name>\r\n");
	printf(" set mac-address <mac address>\r\n");
	printf(" set ip-address <ip address>\r\n");
	printf(" set netmask <netmasks>\r\n");
	printf(" set gateway <gateway ip address>\r\n");
	printf(" set failstate <secure|safe>\r\n");
	printf(" add vlan <vlan id> <vlan name>\r\n");
	printf(" delete vlan <vlan id>\r\n");
	printf(" set vlan-type <vlan id> <default>\r\n");
	printf(" set vlan-tag <vlan id> <tagged|untagged>\r\n");
	printf(" add vlan-port <vlan id> <port>\r\n");
	printf(" delete vlan-port <port>\r\n");
	printf(" set of-version <version(0|1|4)>\r\n");
	printf(" set ethertype-filter <enable|disable>\r\n");
	printf(" factory reset\r\n");
	printf(" exit\r\n");
	printf("\r\n");
	printf("Debug:\r\n");
	printf(" read <register>\r\n");
	printf(" write <register> <value>\r\n");
	printf(" trace\r\n");
	printf(" exit\r\n");
	printf("\r\n");
	return;
}
