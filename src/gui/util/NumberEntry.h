//==========================================================================
// Name:            NumberEntry.h
// Purpose:         Reads whole numbers typed into the dialogs' fields.
//
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//==========================================================================

#ifndef NUMBER_ENTRY_H
#define NUMBER_ENTRY_H

#include <algorithm>
#include <wx/string.h>

// A whole number typed into a field (in base), or false if what's there
// isn't one: a field left empty or filled with something else keeps the
// setting it had, rather than being given a made-up value. (wxString's
// ToLong() and friends leave their output unset when nothing parses, so
// ignoring what they return saves whatever was on the stack.) Read as a long
// long, so a long run of digits isn't wrapped around into some other number
// on its way to the setting (see Clamped() below).
inline bool ReadWholeNumber(wxString text, long long& value, int base = 10)
{
    text.Trim(true).Trim(false);
    return text.ToLongLong(&value, base);
}

// value, brought within [minVal, maxVal] and then into the setting's type.
// For a setting with limits of its own (ConfigurationDataElement::setLimits()),
// [minVal, maxVal] is the type's range, and the setting does the rest.
template<typename T>
inline T Clamped(long long value, long long minVal, long long maxVal)
{
    return (T)std::clamp(value, minVal, maxVal);
}

// The serial rate in a rig control dialog's rate box: 0 ("default", Hamlib's
// own) or a rate. False if it's neither, so that the setting stays as it was.
inline bool ReadSerialRate(wxString const& text, unsigned int& rate)
{
    long long number = 0;
    if (text == "default")
    {
        rate = 0;
        return true;
    }
    if (ReadWholeNumber(text, number) && number > 0 && number <= 4000000)
    {
        rate = (unsigned int)number;
        return true;
    }
    return false;
}

// The Icom CI-V address in a rig control dialog's (hexadecimal) box: empty
// for Hamlib's own (0), otherwise a byte. False if it's neither: one that's
// too big, like a port, isn't rounded to the nearest one.
inline bool ReadCIVAddress(wxString text, unsigned int& address)
{
    long long number = 0;
    text.Trim(true).Trim(false);
    if (text.IsEmpty())
    {
        address = 0;
        return true;
    }
    if (ReadWholeNumber(text, number, 16) && number >= 0 && number <= 0xFF)
    {
        address = (unsigned int)number;
        return true;
    }
    return false;
}

// A sample rate in the audio options' rate box, which lists the device's
// rates but can also be typed into. "None" (shown for a device that lists no
// rates) and -1 are the device's own default. False if what's there isn't a
// rate, so that the setting keeps the rate it had instead of becoming 0.
// Nothing below 8 kHz is a rate FreeDV can use (FreeDV's modems run at 8 kHz
// and up), and the top of the range is the setting's limit.
inline bool ReadSampleRate(wxString text, int& rate)
{
    long long number = 0;
    text.Trim(true).Trim(false);
    if (text == "None")
    {
        rate = -1;
        return true;
    }
    // (Already trimmed for the comparison above, so read directly.)
    if (text.ToLongLong(&number) && number <= 768000 && (number == -1 || number >= 8000))
    {
        rate = (int)number;
        return true;
    }
    return false;
}

#endif // NUMBER_ENTRY_H
