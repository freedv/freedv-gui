#include <cmath>

#include <wx/regex.h>

#include "GridSquare.h"

// M_PI is not available on some compilers, so define it here just in case.
#ifndef M_PI
    #define M_PI 3.1415926535897932384626433832795
#endif

static double DegreesToRadians_(double degrees)
{
    return degrees * (M_PI / 180.0);
}

static double RadiansToDegrees_(double radians)
{
    auto result = (radians > 0 ? radians : (2*M_PI + radians)) * 360 / (2*M_PI);
    return (result == 360) ? 0 : result;
}

namespace
{
    // The pairs of a Maidenhead locator, from least to most precise: field,
    // square, subsquare and extended square. In each pair the first
    // character is longitude and the second latitude.
    struct LocatorPair
    {
        char first;       // character for index 0 ('A' or '0')
        int count;        // number of values each character can take
        double lonSize;   // degrees of longitude each value covers
        double latSize;   // degrees of latitude each value covers
    };

    const LocatorPair LocatorPairs_[] = {
        { 'A', 18, 20.0,       10.0 },        // field: A-R
        { '0', 10, 2.0,        1.0 },         // square: 0-9
        { 'A', 24, 5.0 / 60,   2.5 / 60 },    // subsquare: A-X
        { '0', 10, 0.5 / 60,   0.25 / 60 },   // extended square: 0-9
    };
}

bool GridSquare::IsValid(wxString const& gridSquare)
{
    // A field (A-R), then optionally a square (0-9), a subsquare (A-X) and
    // an extended square (0-9), each only after the one before it. Pairs past
    // the extended square aren't formally defined, so they aren't accepted.
    // Letters should be uppercase, but lowercase is widely used and is
    // accepted too.
    const wxRegEx gridSquareRegex("^[A-Ra-r]{2}([0-9]{2}([A-Xa-x]{2}([0-9]{2})?)?)?$");
    return gridSquareRegex.Matches(gridSquare);
}

bool GridSquare::IsAcceptable(wxString entered)
{
    entered.Trim(true).Trim(false);
    return entered.IsEmpty() || IsValid(entered);
}

wxString GridSquare::AcceptedValue(wxString entered, wxString const& previous)
{
    entered.Trim(true).Trim(false);
    return IsAcceptable(entered) ? entered : previous;
}

void GridSquare::ToLatLon(wxString gridSquare, double& lat, double& lon)
{
    // Uppercase grid square for easier processing
    gridSquare.MakeUpper();
    
    // Start from antimeridian South Pole (e.g. over the Pacific, not over the UK)
    lon = -180.0;
    lat = -90.0;
    double lonSize = 360.0;
    double latSize = 180.0;

    // Use each pair in turn, stopping at the first one that's missing or
    // isn't what belongs there.
    for (size_t index = 0; index < sizeof(LocatorPairs_) / sizeof(LocatorPairs_[0]); index++)
    {
        const LocatorPair& pair = LocatorPairs_[index];
        size_t position = index * 2;
        if (gridSquare.Length() < position + 2)
        {
            break;
        }

        int lonIndex = (char)gridSquare.GetChar(position) - pair.first;
        int latIndex = (char)gridSquare.GetChar(position + 1) - pair.first;
        if (lonIndex < 0 || lonIndex >= pair.count || latIndex < 0 || latIndex >= pair.count)
        {
            break;
        }

        lon += lonIndex * pair.lonSize;
        lat += latIndex * pair.latSize;
        lonSize = pair.lonSize;
        latSize = pair.latSize;
    }

    // Center in the middle of the most precise part used.
    lon += lonSize / 2;
    lat += latSize / 2;
}

double GridSquare::DistanceInKm(wxString const& gridSquare1, wxString const& gridSquare2)
{
    double lat1 = 0;
    double lon1 = 0;
    double lat2 = 0;
    double lon2 = 0;
    
    // Grab latitudes and longitudes for the two locations.
    ToLatLon(gridSquare1, lat1, lon1);
    ToLatLon(gridSquare2, lat2, lon2);
    
    // Use Haversine formula to calculate distance. See
    // https://stackoverflow.com/questions/27928/calculate-distance-between-two-latitude-longitude-points-haversine-formula.
    const double EARTH_RADIUS = 6371;
    double dLat = DegreesToRadians_(lat2-lat1);
    double dLon = DegreesToRadians_(lon2-lon1); 
    double a = 
        sin(dLat/2) * sin(dLat/2) +
        cos(DegreesToRadians_(lat1)) * cos(DegreesToRadians_(lat2)) * 
        sin(dLon/2) * sin(dLon/2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a)); 
    return EARTH_RADIUS * c;
}

double GridSquare::BearingInDegrees(wxString const& gridSquare1, wxString const& gridSquare2)
{
    double lat1 = 0;
    double lon1 = 0;
    double lat2 = 0;
    double lon2 = 0;
    
    // Grab latitudes and longitudes for the two locations.
    ToLatLon(gridSquare1, lat1, lon1);
    ToLatLon(gridSquare2, lat2, lon2);

    // Convert latitudes and longitudes into radians
    lat1 = DegreesToRadians_(lat1);
    lat2 = DegreesToRadians_(lat2);
    lon1 = DegreesToRadians_(lon1);
    lon2 = DegreesToRadians_(lon2);

    double diffLongitude = lon2 - lon1;
    double x = cos(lat2) * sin(diffLongitude);
    double y = (cos(lat1) * sin(lat2)) - (sin(lat1) * cos(lat2) * cos(diffLongitude));
    double radians = atan2(x, y);

    return RadiansToDegrees_(radians);
}

wxString GridSquare::CardinalDirection(int degrees)
{
    int cardinalDirectionNumber( static_cast<int>( ( ( degrees / 360.0 ) * 16 ) + 0.5 )  % 16 );
    const char* const cardinalDirectionTexts[] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
    return cardinalDirectionTexts[cardinalDirectionNumber];
}
