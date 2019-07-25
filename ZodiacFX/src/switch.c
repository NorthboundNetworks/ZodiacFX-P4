/**
 * @file
 * switch.c
 *
 * This file contains the initialization and functions for KSZ8795
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
 */

#include <asf.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "switch.h"
#include "conf_eth.h"
#include "command.h"
#include "timers.h"
#include "P4/zodiacfx-p4.h"

#include "ksz8795clx/ethernet_phy.h"
#include "netif/etharp.h"

// Global variables
extern struct tcp_conn tcp_conn;
extern struct zodiac_config Zodiac_Config;
extern int OF_Version;
extern uint8_t pending_spi_command;
extern struct spi_packet *spi_packet;
extern uint8_t shared_buffer[SHARED_BUFFER_LEN];

// Local variables
gmac_device_t gs_gmac_dev;
uint8_t gmacbuffer[GMAC_FRAME_LENTGH_MAX];
static volatile uint8_t gs_uc_eth_buffer[GMAC_FRAME_LENTGH_MAX];
uint8_t stats_rr = 0;

/* GMAC HW configurations */
#define BOARD_GMAC_PHY_ADDR 0
/** First Status Command Register - Second Dummy Data */
#define USART_SPI                   USART0
#define USART_SPI_DEVICE_ID         1
#define USART_SPI_BAUDRATE          1000000

struct usart_spi_device USART_SPI_DEVICE = {
	 /* Board specific select ID. */
	 .id = USART_SPI_DEVICE_ID
 };

/*
*	Initialise the SPI interface for the switch
*
*/
void spi_init(void)
{
	/* Config the USART_SPI for KSZ8795 interface */
	usart_spi_init(USART_SPI);
	usart_spi_setup_device(USART_SPI, &USART_SPI_DEVICE, SPI_MODE_3, USART_SPI_BAUDRATE, 0);
	usart_spi_enable(USART_SPI);
}

/*
*	Read from the switch registers
*
*/
int switch_read(uint8_t param1)
{
	uint8_t reg[2];

	if (param1 < 128) {
		reg[0] = 96;
		} else {
		reg[0] = 97;
	}

	reg[1] = param1 << 1;

	/* Select the DF memory to check. */
	usart_spi_select_device(USART_SPI, &USART_SPI_DEVICE);

	/* Send the Manufacturer ID Read command. */
	usart_spi_write_packet(USART_SPI, reg, 2);

	/* Receive Manufacturer ID. */
	usart_spi_read_packet(USART_SPI, reg, 1);

	/* Deselect the checked DF memory. */
	usart_spi_deselect_device(USART_SPI, &USART_SPI_DEVICE);

	return reg[0];
}

/*
*	Write to the switch registers
*
*/
int switch_write(uint8_t param1, uint8_t param2)
{
	uint8_t reg[3];

	if (param1 < 128) {
		reg[0] = 64;
		} else {
		reg[0] = 65;
	}

	reg[1] = param1 << 1;
	reg[2] = param2;

	/* Select the DF memory to check. */
	usart_spi_select_device(USART_SPI, &USART_SPI_DEVICE);

	/* Send the Manufacturer ID Read command. */
	usart_spi_write_packet(USART_SPI, reg, 3);

	/* Deselect the checked DF memory. */
	usart_spi_deselect_device(USART_SPI, &USART_SPI_DEVICE);
	for(int x = 0;x<100000;x++);

	return switch_read(param1);
}

/*
*	Read the number of CRC errors from the switch
*
*	@param port - the number of the port to get the stats for.
*
*/
int readrxcrcerr(int port)
{
	int total = 0;
	uint8_t reg = (6 + (32*(port-1)));
	switch_write(110,28);
	switch_write(111, reg);
	total += (switch_read(119) * 256);
	total += switch_read(120);
	return total;
}

/*
*	Read the number of received bytes from the switch
*
*	@param port - the number of the port to get the stats for.
*
*/
int readtxbytes(int port)
{
	int total = 0;
	uint8_t reg = (1 + (4*(port-1)));
	switch_write(110,29);
	switch_write(111, reg);
	total += (switch_read(119) * 256);
	total += switch_read(120);
	return total;
}

/*
*	Read the number of transmitted bytes from the switch
*
*	@param port - the number of the port to get the stats for.
*
*/
int readrxbytes(int port)
{
	int total = 0;
	uint8_t reg = (4*(port-1));
	switch_write(110,29);
	switch_write(111, reg);
	total += (switch_read(119) * 256);
	total += switch_read(120);
	return total;
}

/*
*	Read the number of dropped RX packets from the switch
*
*	@param port - the number of the port to get the stats for.
*
*/
int readrxdrop(int port)
{
	int total = 0;
	uint8_t reg = (2 + (4*(port-1)));
	switch_write(110,29);
	switch_write(111, reg);
	total += (switch_read(119) * 256);
	total += switch_read(120);
	return total;
}

/*
*	Read the number of dropped TX packets from the switch
*
*	@param port - the number of the port to get the stats for.
*
*/
int readtxdrop(int port)
{
	int total = 0;
	uint8_t reg = (3 + (4*(port-1)));
	switch_write(110,29);
	switch_write(111, reg);
	total += (switch_read(119) * 256);
	total += switch_read(120);
	return total;
}

/*
*	GMAC write function
*
*	@param *p_buffer - pointer to the buffer containing the data to send.
*	@param ul_size - size of the data.
*	@param port - the port to send the data out from.
*
*/
void gmac_write(uint8_t *p_buffer, uint16_t ul_size, uint8_t port)
{
	if (ul_size > GMAC_FRAME_LENTGH_MAX)
	{
		return;
	}
	
	// Add padding
	if (ul_size < 60)
	{
		memset(&gmacbuffer, 0, 61);
		memcpy(&gmacbuffer,p_buffer, ul_size);
		uint8_t *last_byte;
		last_byte = gmacbuffer + 60;
		*last_byte = port;
		gmac_dev_write(&gs_gmac_dev, &gmacbuffer, 61, NULL);
	}
	else
	{
		memcpy(&gmacbuffer,p_buffer, ul_size);
		uint8_t *last_byte;
		last_byte = gmacbuffer + ul_size;
		*last_byte = port;
		ul_size++; // Increase packet size by 1 to allow for the tail tag.
		uint32_t write_size = gmac_dev_write(&gs_gmac_dev, &gmacbuffer, ul_size, NULL);
	}
	
	return;
}

/*
*	GMAC handler function
*
*/
void GMAC_Handler(void)
{
	gmac_handler(&gs_gmac_dev);
}

/*
*	Switch initialization function
*
*/
void switch_init(void)
{
		volatile uint32_t ul_delay;
		gmac_options_t gmac_option;

		/* Wait for PHY to be ready (CAT811: Max400ms) */
		ul_delay = sysclk_get_cpu_hz() / 1000 / 3 * 400;
		while (ul_delay--);

		/* Enable GMAC clock */
		pmc_enable_periph_clk(ID_GMAC);

		/* Fill in GMAC options */
		gmac_option.uc_copy_all_frame = 1;
		gmac_option.uc_no_boardcast = 0;
		memcpy(gmac_option.uc_mac_addr, Zodiac_Config.MAC_address, 6);
		gs_gmac_dev.p_hw = GMAC;

		/* Init KSZ8795 registers */
		switch_write(86,232);	// Set CPU interface to MII
		switch_write(12,70);	// Turn on tail tag mode

		/* Because we use the tail tag mode on the KS8795 the additional
		byte on the end makes the frame size 1519 bytes. This causes the packet
		to fail the Max Legal size check, so setting byte 1 on global register 4
		disables the check */
		switch_write(4,242);

		/* Init GMAC driver structure */
		gmac_dev_init(GMAC, &gs_gmac_dev, &gmac_option);

		/* Enable Interrupt */
		NVIC_EnableIRQ(GMAC_IRQn);

		/* Init MAC PHY driver */
		if (ethernet_phy_init(GMAC, BOARD_GMAC_PHY_ADDR, sysclk_get_cpu_hz()) != GMAC_OK) {
			return;
		}

		while (ethernet_phy_set_link(GMAC, BOARD_GMAC_PHY_ADDR, 1) != GMAC_OK) {
			return;
		}


		/* Create KSZ8795 VLANs */
		switch_write(5,0);		// Disable 802.1q

		for (int x=0;x<MAX_VLANS;x++)
		{
			if (Zodiac_Config.vlan_list[x].uActive == 1)
			{
				if (Zodiac_Config.vlan_list[x].uVlanType == 1) switch_write(84,Zodiac_Config.vlan_list[x].uVlanID);	// If the VLAN is type Default then add the CPU port
				/* Assign the default ingress VID */
				for (int i=0;i<4;i++)
				{
					if (Zodiac_Config.vlan_list[x].portmap[i] == 1)
					{
						switch_write(20 + (i*16),Zodiac_Config.vlan_list[x].uVlanID);	// Default ingress VID
					}
				}
				/* Add entry into the VLAN table */
				int vlanoffset = Zodiac_Config.vlan_list[x].uVlanID / 4;
				int vlanindex = Zodiac_Config.vlan_list[x].uVlanID - (vlanoffset*4);
				switch_write(110,20);	// Set read VLAN flag
				switch_write(111, vlanoffset);	// Read entries 0-3

				/* Calculate format */
				uint8_t vlanmaphigh;
				uint8_t vlanmaplow;
				vlanmaphigh = 16; // Set valid bit
				if (Zodiac_Config.vlan_list[x].uVlanType == 1) vlanmaphigh += 8; 
				if (Zodiac_Config.vlan_list[x].portmap[3] == 1) // Port 4
				{
					vlanmaphigh += 4;
					if (Zodiac_Config.vlan_list[x].uTagged == 1) switch_write(64,4);	// Set port as VLAN tagged
					if (Zodiac_Config.vlan_list[x].uTagged == 0) switch_write(64,0);	// Set port as VLAN untagged
				}
				if (Zodiac_Config.vlan_list[x].portmap[2] == 1) // Port 3;
				{
					vlanmaphigh += 2;
					if (Zodiac_Config.vlan_list[x].uTagged == 1) switch_write(48,4);	// Set port as VLAN tagged
					if (Zodiac_Config.vlan_list[x].uTagged == 0) switch_write(48,0);	// Set port as VLAN untagged
				}
				if (Zodiac_Config.vlan_list[x].portmap[1] == 1) // Port 2;
				{
					vlanmaphigh += 1;
					if (Zodiac_Config.vlan_list[x].uTagged == 1) switch_write(32,4);	// Set port as VLAN tagged
					if (Zodiac_Config.vlan_list[x].uTagged == 0) switch_write(32,0);	// Set port as VLAN untagged
				}
				vlanmaplow = x+1;	// FID = VLAN index number
				if (Zodiac_Config.vlan_list[x].portmap[0] == 1) // Port 1;
				{
					vlanmaplow += 128;
					if (Zodiac_Config.vlan_list[x].uTagged == 1) switch_write(16,4);	// Set port as VLAN tagged
					if (Zodiac_Config.vlan_list[x].uTagged == 0) switch_write(16,0);	// Set port as VLAN untagged
				}
				/* Write settings back to registers */
				switch_write((119-(vlanindex*2)),vlanmaphigh);
				switch_write((120-(vlanindex*2)),vlanmaplow);
				switch_write(110,4);	// Set read VLAN flag
				switch_write(111,vlanoffset);	// Read entries 0-3
			}
		}

		switch_write(5,128);	// Enable 802.1q
		
		/* Trap all packets using Authentication_mode and send them to the CPU. */
		switch_write(21,3);
		switch_write(37,3);
		switch_write(53,3);
		switch_write(69,3);
		return;
}
/*
*	Main switching loop
*
*	@param *netif - pointer to the network interface struct.
*
*/
void task_switch(struct netif *netif)
{
	uint16_t ul_rcv_size = 0;

	/* Main packet processing loop */
	uint32_t dev_read = gmac_dev_read(&gs_gmac_dev, (uint8_t *) gs_uc_eth_buffer, sizeof(gs_uc_eth_buffer), &ul_rcv_size);
	if (dev_read == GMAC_OK)
	{		
		// Process packet
		if (ul_rcv_size > 0)
		{
			uint8_t* tail_tag = (uint8_t*)(gs_uc_eth_buffer + (int)(ul_rcv_size)-1);
			uint8_t tag = *tail_tag + 1;
			ul_rcv_size--; // remove the tail first
			packet_in((uint8_t *) gs_uc_eth_buffer, ul_rcv_size, tag);
			return;
		}
	}
	return;

}
