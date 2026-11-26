#include <cmath>

#include <wx/translation.h>
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

bool GridSquare::IsValid(wxString const& gridSquare)
{
    // Two letters, two digits and, for a more precise square, two more
    // letters: nothing else is a grid square, so the end is anchored too.
    const wxRegEx gridSquareRegex("^[A-Za-z]{2}[0-9]{2}([A-Za-z]{2})?$");
    return gridSquareRegex.Matches(gridSquare);
}

void GridSquare::ToLatLon(wxString gridSquare, double& lat, double& lon)
{
    char charA = 'A';
    char char0 = '0';
    
    // Uppercase grid square for easier processing
    gridSquare.MakeUpper();
    
    // Start from antimeridian South Pole (e.g. over the Pacific, not over the UK)
    lon = -180.0;
    lat = -90.0;
    
    // Process first two characters
    lon += ((char)gridSquare.GetChar(0) - charA) * 20;
    lat += ((char)gridSquare.GetChar(1) - charA) * 10;
    
    // Then next two
    lon += ((char)gridSquare.GetChar(2) - char0) * 2;
    lat += ((char)gridSquare.GetChar(3) - char0) * 1;
    
    // If grid square is 6 or more letters, THEN use the next two.
    // Otherwise, optional.
    wxString optionalSegment = gridSquare.Mid(4, 2);
    const wxRegEx allLetters("^[A-Z]{2}$");
    if (gridSquare.Length() >= 6 && allLetters.Matches(optionalSegment))
    {
        lon += ((char)gridSquare.GetChar(4) - charA) * 5.0 / 60;
        lat += ((char)gridSquare.GetChar(5) - charA) * 2.5 / 60;
    
        // Center in middle of grid square
        lon += 5.0 / 60 / 2;
        lat += 2.5 / 60 / 2;
    }
    else
    {
        lon += 2 / 2;
        lat += 1.0 / 2;
    }
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
    // TRANSLATORS: Compass directions (N = north, NNE = north-northeast, etc.).
    const char* const cardinalDirectionTexts[] = {
        wxTRANSLATE("N"), wxTRANSLATE("NNE"), wxTRANSLATE("NE"), wxTRANSLATE("ENE"),
        wxTRANSLATE("E"), wxTRANSLATE("ESE"), wxTRANSLATE("SE"), wxTRANSLATE("SSE"),
        wxTRANSLATE("S"), wxTRANSLATE("SSW"), wxTRANSLATE("SW"), wxTRANSLATE("WSW"),
        wxTRANSLATE("W"), wxTRANSLATE("WNW"), wxTRANSLATE("NW"), wxTRANSLATE("NNW") };
    return wxGetTranslation(cardinalDirectionTexts[cardinalDirectionNumber]);
}
