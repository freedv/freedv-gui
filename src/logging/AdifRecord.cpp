//==========================================================================
// Name:            AdifRecord.cpp
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

#include "AdifRecord.h"

#include <cstdio>
#include <utility>

const AdifRecord::Band AdifRecord::BANDS_[] = {
    { "2190m",          135700,          137800 },
    { "630m",           472000,          479000 },
    { "560m",           501000,          504000 },
    { "160m",          1800000,         2000000 },
    { "80m",           3500000,         4000000 },
    { "60m",           5060000,         5450000 },
    { "40m",           7000000,         7300000 },
    { "30m",          10100000,        10150000 },
    { "20m",          14000000,        14350000 },
    { "17m",          18068000,        18168000 },
    { "15m",          21000000,        21450000 },
    { "12m",          24890000,        24990000 },
    { "10m",          28000000,        29700000 },
    { "8m",           40000000,        45000000 },
    { "6m",           50000000,        54000000 },
    { "5m",           54000001,        69900000 },
    { "4m",           70000000,        71000000 },
    { "2m",          144000000,       148000000 },
    { "1.25m",       222000000,       225000000 },
    { "70cm",        420000000,       450000000 },
    { "33cm",        902000000,       928000000 },
    { "23cm",       1240000000,      1300000000 },
    { "13cm",       2300000000ULL,   2450000000ULL },
    { "9cm",        3300000000ULL,   3500000000ULL },
    { "6cm",        5650000000ULL,   5925000000ULL },
    { "3cm",       10000000000ULL,  10500000000ULL },
    { "1.25cm",    24000000000ULL,  24250000000ULL },
    { "6mm",       47000000000ULL,  47200000000ULL },
    { "4mm",       75500000000ULL,  81000000000ULL },
    { "2.5mm",    119980000000ULL, 123000000000ULL },
    { "2mm",      134000000000ULL, 149000000000ULL },
    { "1mm",      241000000000ULL, 250000000000ULL },
    { "submm",    300000000000ULL, 7500000000000ULL },
};

AdifRecord::AdifRecord(
    std::chrono::system_clock::time_point logTime, std::string dxCall, std::string dxGrid,
    std::string myCall, std::string myGrid, uint64_t freqHz, std::string reportRx,
    std::string reportTx, std::string name, std::string comments)
    : logTime_(logTime)
    , dxCall_(std::move(dxCall))
    , dxGrid_(std::move(dxGrid))
    , myCall_(std::move(myCall))
    , myGrid_(std::move(myGrid))
    , freqHz_(freqHz)
    , reportRx_(std::move(reportRx))
    , reportTx_(std::move(reportTx))
    , name_(std::move(name))
    , comments_(std::move(comments))
{
    // empty
}

void AdifRecord::appendField_(std::string& record, const char* name, std::string const& value)
{
    if (value.empty())
    {
        return;
    }
    if (!record.empty())
    {
        record += ' ';
    }
    record += '<';
    record += name;
    record += ':';
    record += std::to_string(value.size());
    record += '>';
    record += value;
}

std::string AdifRecord::BandForFrequency(uint64_t freqHz)
{
    for (auto const& band : BANDS_)
    {
        if (freqHz >= band.lowerHz && freqHz <= band.upperHz)
        {
            return band.name;
        }
    }
    return "";
}

std::string AdifRecord::FrequencyMHz(uint64_t freqHz)
{
    // Integer arithmetic, so that it's exact (no 14.235999...).
    std::string result = std::to_string(freqHz / 1000000);
    uint64_t fraction = freqHz % 1000000;
    if (fraction != 0)
    {
        char digits[8];
        snprintf(digits, sizeof(digits), "%06u", (unsigned)fraction);
        std::string fractionDigits(digits);
        fractionDigits.erase(fractionDigits.find_last_not_of('0') + 1);
        result += '.';
        result += fractionDigits;
    }
    return result;
}

std::string AdifRecord::toLoggedAdif(std::string const& programVersion) const
{
    using namespace std::chrono;

    // ADIF dates and times are UTC, which system_clock is. Contacts are
    // logged when they end, so that's both the start and end time as far
    // as what's sent goes (as in the QSO Logged message).
    auto logTime = floor<seconds>(logTime_);
    auto day = floor<days>(logTime);
    year_month_day date{day};
    hh_mm_ss<seconds> time{logTime - day};

    char dateStr[16];
    snprintf(dateStr, sizeof(dateStr), "%04d%02u%02u",
        (int)date.year(), (unsigned)date.month(), (unsigned)date.day());
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%02d%02d%02d",
        (int)time.hours().count(), (int)time.minutes().count(), (int)time.seconds().count());

    // Fields in the order WSJT-X sends them.
    std::string record;
    appendField_(record, "call", dxCall_);
    appendField_(record, "gridsquare", dxGrid_);
    // FREEDV has been a DIGITALVOICE submode since ADIF 3.1.4.
    appendField_(record, "mode", "DIGITALVOICE");
    appendField_(record, "submode", "FREEDV");
    appendField_(record, "rst_sent", reportTx_);
    appendField_(record, "rst_rcvd", reportRx_);
    appendField_(record, "qso_date", dateStr);
    appendField_(record, "time_on", timeStr);
    appendField_(record, "qso_date_off", dateStr);
    appendField_(record, "time_off", timeStr);
    appendField_(record, "band", BandForFrequency(freqHz_));
    appendField_(record, "freq", FrequencyMHz(freqHz_));
    appendField_(record, "station_callsign", myCall_);
    appendField_(record, "my_gridsquare", myGrid_);
    appendField_(record, "comment", comments_);
    appendField_(record, "name", name_);

    std::string header;
    header += "\n<adif_ver:5>3.1.4\n";
    header += "<programid:6>FreeDV\n";
    if (!programVersion.empty())
    {
        header += "<programversion:" + std::to_string(programVersion.size()) + ">" + programVersion + "\n";
    }
    header += "<EOH>\n";

    return header + record + " <EOR>";
}
