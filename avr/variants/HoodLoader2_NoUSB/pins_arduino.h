/*
Copyright(c) 2014 NicoHood
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

#include "../HoodLoader2/pins_arduino.h"

// temporary USB deactivation workaround
// you can still use this to get rid of all USB functions
#undef USBCON

/*
// workaround for undefined USBCON has to be placed in every sketch
// otherwise the timings wont work correctly
ISR(USB_GEN_vect)
{
	UDINT = 0;
}
*/