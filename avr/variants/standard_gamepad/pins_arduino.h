/*
Copyright(c) 2014-2015 NicoHood
See the readme for credit to other people.

This file is part of Hoodloader2.

Hoodloader2 is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Hoodloader2 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Hoodloader2.  If not, see <http://www.gnu.org/licenses/>.
*/

// include the standard Uno board definition file
#include "../standard/pins_arduino.h"

//================================================================================
// HID Settings
//================================================================================

// HID Project needs to be installed https://github.com/NicoHood/HID

// pre selected hid reports with autoinclude of the api
#define HID_MOUSE_ENABLE // normal mouse with buttons + wheel
//#define HID_MOUSE_ABSOLUTE_ENABLE // only works with system and without gamepad
#define HID_KEYBOARD_LEDS_ENABLE // leds OR keys
//#define HID_KEYBOARD_KEYS_ENABLE
//#define HID_RAWHID_ENABLE // currently not working
//#define HID_CONSUMERCONTROL_ENABLE
//#define HID_SYSTEMCONTROL_ENABLE
#define HID_GAMEPAD_ENABLE // only works without mouse absolute
