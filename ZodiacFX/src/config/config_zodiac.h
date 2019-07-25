/**
 * @file
 * config_zodiac.h
 *
 * This file contains the configuration for the Zodiac FX
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

#ifndef CONFIG_ZODIAC_H_
#define CONFIG_ZODIAC_H_


#define VERSION "0.1"		// Firmware version number

#define TOTAL_PORTS 4		// Total number of physical ports on the Zodiac FX
#define MAX_VLANS	4	// Maximum number of VLANS, default is 1 per port (4)

#endif /* CONFIG_ZODIAC_H_ */
