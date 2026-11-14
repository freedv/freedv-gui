#include <chrono>
#include <cstdint>
#include <string>

#include "../AdifRecord.h"
#include "AdifParser.h"
#include "../../test/UnitTestCommon.h"

// The ADIF record sent in the WSJT-X "Logged ADIF" message. See
// https://adif.org.uk/316/ADIF_316.htm for the format.

namespace {

std::chrono::system_clock::time_point fromUnixMs(int64_t ms)
{
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::milliseconds(ms)));
}

// What's logged about one contact, to make AdifRecords from.
struct Contact
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

std::string loggedAdif(Contact const& c, std::string const& programVersion)
{
    AdifRecord record(
        c.logTime, c.dxCall, c.dxGrid, c.myCall, c.myGrid, c.freqHz, c.reportRx, c.reportTx, c.name, c.comments);
    return record.toLoggedAdif(programVersion);
}

Contact sampleContact()
{
    Contact contact;
    contact.logTime = fromUnixMs(1765542896789LL); // 2025-12-12 12:34:56.789 UTC
    contact.dxCall = "K6AQ";
    contact.dxGrid = "DM12";
    contact.myCall = "K6ABC";
    contact.myGrid = "CM97";
    contact.freqHz = 14236000;
    contact.reportRx = "59";
    contact.reportTx = "57";
    contact.name = "Mooneer";
    contact.comments = "Nice QSO";
    return contact;
}

bool check(const char* what, std::string const& actual, std::string const& expected)
{
    if (actual == expected) return true;
    std::cout << "[" << what << " was \"" << actual << "\", expected \"" << expected << "\"] ";
    return false;
}

bool parses(std::string const& adif, AdifFile& file)
{
    file = ParseAdif(adif);
    if (!file.error.empty())
    {
        std::cout << "[ADIF didn't parse: " << file.error << "] ";
        return false;
    }
    return true;
}

std::string field(AdifFile const& file, const char* name)
{
    auto it = file.record.find(name);
    return it == file.record.end() ? "(missing)" : it->second;
}

bool bandIs(uint64_t freqHz, const char* expected)
{
    return check(("band for " + std::to_string(freqHz) + " Hz").c_str(), AdifRecord::BandForFrequency(freqHz), expected);
}

bool frequencyIs(uint64_t freqHz, const char* expected)
{
    return check(("frequency for " + std::to_string(freqHz) + " Hz").c_str(), AdifRecord::FrequencyMHz(freqHz), expected);
}

// Every field of a fully filled in contact, as a logging program reads it.
bool recordHasEveryField()
{
    AdifFile file;
    if (!parses(loggedAdif(sampleContact(), "2.2.0"), file)) return false;

    bool result = true;
    result &= check("call", field(file, "call"), "K6AQ");
    result &= check("gridsquare", field(file, "gridsquare"), "DM12");
    result &= check("mode", field(file, "mode"), "DIGITALVOICE");
    result &= check("submode", field(file, "submode"), "FREEDV");
    result &= check("rst_sent", field(file, "rst_sent"), "57");
    result &= check("rst_rcvd", field(file, "rst_rcvd"), "59");
    result &= check("qso_date", field(file, "qso_date"), "20251212");
    result &= check("time_on", field(file, "time_on"), "123456");
    result &= check("qso_date_off", field(file, "qso_date_off"), "20251212");
    result &= check("time_off", field(file, "time_off"), "123456");
    result &= check("band", field(file, "band"), "20m");
    result &= check("freq", field(file, "freq"), "14.236");
    result &= check("station_callsign", field(file, "station_callsign"), "K6ABC");
    result &= check("my_gridsquare", field(file, "my_gridsquare"), "CM97");
    result &= check("comment", field(file, "comment"), "Nice QSO");
    result &= check("name", field(file, "name"), "Mooneer");
    result &= check("number of fields", std::to_string(file.record.size()), "16");
    return result;
}

// Laid out as WSJT-X lays it out, so that anything that reads WSJT-X's
// can read ours.
bool layoutMatchesWsjtx()
{
    std::string adif = loggedAdif(sampleContact(), "2.2.0");
    std::string expected =
        "\n<adif_ver:5>3.1.4\n<programid:6>FreeDV\n<programversion:5>2.2.0\n<EOH>\n"
        "<call:4>K6AQ <gridsquare:4>DM12 <mode:12>DIGITALVOICE <submode:6>FREEDV "
        "<rst_sent:2>57 <rst_rcvd:2>59 <qso_date:8>20251212 <time_on:6>123456 "
        "<qso_date_off:8>20251212 <time_off:6>123456 <band:3>20m <freq:6>14.236 "
        "<station_callsign:5>K6ABC <my_gridsquare:4>CM97 <comment:8>Nice QSO <name:7>Mooneer <EOR>";
    return check("ADIF", adif, expected);
}

bool headerNamesFreeDV()
{
    AdifFile file;
    if (!parses(loggedAdif(sampleContact(), "2.2.0"), file)) return false;

    bool result = true;
    // ADIF 3.1.4 is the first with the FREEDV submode.
    result &= check("adif_ver", file.header["adif_ver"], "3.1.4");
    result &= check("programid", file.header["programid"], "FreeDV");
    result &= check("programversion", file.header["programversion"], "2.2.0");

    AdifFile noVersion;
    if (!parses(loggedAdif(sampleContact(), ""), noVersion)) return false;
    result &= check("programversion when unknown", std::to_string(noVersion.header.count("programversion")), "0");
    return result;
}

// An empty value isn't a value: a zero-length field would overwrite what
// the logging program might fill in itself (e.g. a grid from a callbook).
bool leavesOutEmptyFields()
{
    Contact contact = sampleContact();
    contact.dxGrid = "";
    contact.myGrid = "";
    contact.reportRx = "";
    contact.reportTx = "";
    contact.name = "";
    contact.comments = "";

    AdifFile file;
    if (!parses(loggedAdif(contact, "2.2.0"), file)) return false;

    bool result = true;
    for (const char* name : { "gridsquare", "my_gridsquare", "rst_sent", "rst_rcvd", "name", "comment" })
    {
        if (file.record.count(name) != 0)
        {
            std::cout << "[empty " << name << " was sent] ";
            result = false;
        }
    }
    result &= check("call", field(file, "call"), "K6AQ");
    result &= check("number of fields", std::to_string(file.record.size()), "10");
    return result;
}

// Lengths are byte counts, so a logging program reads UTF-8 text whole
// and the fields after it in the right place.
bool nonAsciiTextHasItsUtf8Length()
{
    Contact contact = sampleContact();
    contact.name = "Jos\xc3\xa9 \xe5\xa4\xaa\xe9\x83\x8e"; // José 太郎
    contact.comments = std::string(500, 'x');

    std::string adif = loggedAdif(contact, "2.2.0");
    AdifFile file;
    if (!parses(adif, file)) return false;

    bool result = true;
    result &= check("name", field(file, "name"), contact.name);
    result &= check("comment", field(file, "comment"), contact.comments);
    result &= check("name's length", std::to_string(adif.find("<name:12>") != std::string::npos), "1");
    return result;
}

// Times are UTC and to the second: the contact's second, not rounded to
// the next one.
bool timesAreUtcToTheSecond()
{
    struct Case { int64_t unixMs; const char* date; const char* time; };
    const Case cases[] = {
        { 0,               "19700101", "000000" },
        { 946728000000LL,  "20000101", "120000" },
        { 1709164800000LL + 3723004, "20240229", "010203" }, // leap day
        { 1765583999999LL, "20251212", "235959" },             // last ms of the day
        { 1765584000000LL, "20251213", "000000" },
    };

    bool result = true;
    for (auto const& c : cases)
    {
        Contact contact = sampleContact();
        contact.logTime = fromUnixMs(c.unixMs);
        AdifFile file;
        if (!parses(loggedAdif(contact, "2.2.0"), file)) return false;
        result &= check("qso_date", field(file, "qso_date"), c.date);
        result &= check("time_on", field(file, "time_on"), c.time);
        result &= check("qso_date_off", field(file, "qso_date_off"), c.date);
        result &= check("time_off", field(file, "time_off"), c.time);
    }
    return result;
}

// ADIF's Band enumeration, at and just past each edge of a few bands.
bool bandFollowsAdifEnumeration()
{
    bool result = true;
    result &= bandIs(135700, "2190m");
    result &= bandIs(472000, "630m");
    result &= bandIs(1799999, "");
    result &= bandIs(1800000, "160m");
    result &= bandIs(2000000, "160m");
    result &= bandIs(2000001, "");
    result &= bandIs(3625000, "80m");
    result &= bandIs(5060000, "60m");
    result &= bandIs(5403500, "60m");
    result &= bandIs(7177000, "40m");
    result &= bandIs(10120000, "30m");
    result &= bandIs(14236000, "20m");
    result &= bandIs(14350001, "");
    result &= bandIs(18118000, "17m");
    result &= bandIs(21313000, "15m");
    result &= bandIs(24933000, "12m");
    result &= bandIs(28330000, "10m");
    result &= bandIs(29700000, "10m");
    result &= bandIs(50000000, "6m");
    result &= bandIs(54000000, "6m");
    result &= bandIs(54000001, "5m");
    result &= bandIs(144500000, "2m");
    result &= bandIs(222000000, "1.25m");
    result &= bandIs(432100000, "70cm");
    result &= bandIs(1296000000, "23cm");
    result &= bandIs(10368100000ULL, "3cm"); // doesn't fit in 32 bits
    result &= bandIs(0, "");
    return result;
}

bool bandLeftOutOutsideEveryBand()
{
    Contact contact = sampleContact();
    contact.freqHz = 11000000; // between 30m and 20m

    AdifFile file;
    if (!parses(loggedAdif(contact, "2.2.0"), file)) return false;

    bool result = true;
    result &= check("band sent", std::to_string(file.record.count("band")), "0");
    result &= check("freq", field(file, "freq"), "11");
    return result;
}

// FREQ is in MHz, exactly as given (no floating point rounding).
bool frequencyIsExactMHz()
{
    bool result = true;
    result &= frequencyIs(14236000, "14.236");
    result &= frequencyIs(14000000, "14");
    result &= frequencyIs(7177500, "7.1775");
    result &= frequencyIs(14236001, "14.236001");
    result &= frequencyIs(135700, "0.1357");
    result &= frequencyIs(10368100000ULL, "10368.1");
    result &= frequencyIs(0, "0");
    return result;
}

}

int main()
{
    executeTestCase("recordHasEveryField", recordHasEveryField);
    executeTestCase("layoutMatchesWsjtx", layoutMatchesWsjtx);
    executeTestCase("headerNamesFreeDV", headerNamesFreeDV);
    executeTestCase("leavesOutEmptyFields", leavesOutEmptyFields);
    executeTestCase("nonAsciiTextHasItsUtf8Length", nonAsciiTextHasItsUtf8Length);
    executeTestCase("timesAreUtcToTheSecond", timesAreUtcToTheSecond);
    executeTestCase("bandFollowsAdifEnumeration", bandFollowsAdifEnumeration);
    executeTestCase("bandLeftOutOutsideEveryBand", bandLeftOutOutsideEveryBand);
    executeTestCase("frequencyIsExactMHz", frequencyIsExactMHz);
    return testResult();
}
