#ifndef __KEYS_H
#define __KEYS_H/*
-------------------------------------------------------------------------------
--
--  (C) COPYRIGHT 2007, Gideon's Hardware and Programmable Logic Solutions
--
-------------------------------------------------------------------------------
-- Project    : Ultimate 1541
-- Title      : keys.h
-------------------------------------------------------------------------------
-- Revision info:
--
--  $Date: $
--	$Revision: $
-- 	$Author: $
-------------------------------------------------------------------------------
-- Abstract:

	definition of VT100 and Commodore key codes.
	These benefit the console and menu

--
-------------------------------------------------------------------------------
*/

#define	VT100_ESC			0x1B		// Escape character
#define	VT100_BS			0x08		// Backspace key
#define	VT100_CR			0x0D		// Carriage return
#define	VT100_NL			0x0A		// Newline key
#define	VT100_DEL			0x7F		// Delete key
#define VT100_SPACE			0x20		// Space character
#define VT100_MAX			0x7F		// Last code

#define	VT100_CTRL_S		0x13		// Get next cmd
#define	VT100_CTRL_Z		0x1A		// Get previous cmd

#define	VT100_CTRL_C		0x03		// go right
#define	VT100_CTRL_X		0x18		// go left

#define	VT100_CTRL_U		0x15		// clear the command

#define	VT100_CTRL_Q		0x11		// quit the console


#define VT100_KEY_LEFT		VT100_CTRL_X
#define VT100_KEY_RIGHT		VT100_CTRL_C


#define VT100_KEY_PREVC		VT100_CTRL_Z
#define VT100_KEY_NEXTC		VT100_CTRL_S

#define VT100_CLEAR			VT100_CTRL_U
#define VT100_QUIT			VT100_CTRL_Q

#endif
