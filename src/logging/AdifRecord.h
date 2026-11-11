//==========================================================================
// Name:            AdifRecord.h
//
// Purpose:         Builds the ADIF record sent in WSJT-X's "Logged ADIF"
//                  network message
// Created:         November 10, 2026
// Authors:         Mooneer Salem
//
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU Lesser General Public License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
//  License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//==========================================================================

#ifndef ADIF_RECORD_H
#define ADIF_RECORD_H

#include <chrono>
#include <cstdint>
#include <string>

// What's logged about one contact.
struct AdifContact
{
    std::chrono::system_clock::time_point logTime;
    std::string dxCall;
    std::string dxGrid;
    std::string myCall;
    std::string myGrid;
    uint64_t freqHz = 0;
    std::string reportRx;
    std::string reportTx;
    std::string name;
    std::string comments;
};

// The ADIF (https://adif.org.uk/) Band enumeration value for a frequency,
// e.g. "20m", or "" if it's in none of the bands ADIF defines.
std::string AdifBandForFrequency(uint64_t freqHz);

// A frequency in MHz as ADIF's FREQ field has it, exactly, e.g. "14.236".
std::string AdifFrequencyMHz(uint64_t freqHz);

// A one-record ADIF file for the contact, in the form WSJT-X sends in its
// "Logged ADIF" network message: a header, then the record's fields
// separated by spaces and ended by " <EOR>". Fields with no value are left
// out. Field lengths are in bytes, so text that isn't ASCII is sent as
// UTF-8 with its UTF-8 length.
std::string BuildLoggedAdif(AdifContact const& contact, std::string const& programVersion);

#endif // ADIF_RECORD_H
