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

// One logged contact as an ADIF (https://adif.org.uk/) record.
class AdifRecord
{
public:
    AdifRecord(
        std::chrono::system_clock::time_point logTime, std::string dxCall, std::string dxGrid,
        std::string myCall, std::string myGrid, uint64_t freqHz, std::string reportRx,
        std::string reportTx, std::string name, std::string comments);

    // A one-record ADIF file for the contact, in the form WSJT-X sends in
    // its "Logged ADIF" network message: a header, then the record's fields
    // separated by spaces and ended by " <EOR>". Fields with no value are
    // left out. Field lengths are in bytes, so text that isn't ASCII is sent
    // as UTF-8 with its UTF-8 length.
    std::string toLoggedAdif(std::string const& programVersion) const;

    // The ADIF Band enumeration value for a frequency, e.g. "20m", or "" if
    // it's in none of the bands ADIF defines.
    static std::string BandForFrequency(uint64_t freqHz);

    // A frequency in MHz as ADIF's FREQ field has it, exactly, e.g. "14.236".
    static std::string FrequencyMHz(uint64_t freqHz);

private:
    struct Band
    {
        const char* name;
        uint64_t lowerHz;
        uint64_t upperHz;
    };

    // ADIF 3.1.6's Band enumeration, edges inclusive.
    static const Band BANDS_[];

    // Appends "<name:length>value" to the record, or nothing if there's no
    // value.
    static void appendField_(std::string& record, const char* name, std::string const& value);

    std::chrono::system_clock::time_point logTime_;
    std::string dxCall_;
    std::string dxGrid_;
    std::string myCall_;
    std::string myGrid_;
    uint64_t freqHz_;
    std::string reportRx_;
    std::string reportTx_;
    std::string name_;
    std::string comments_;
};

#endif // ADIF_RECORD_H
