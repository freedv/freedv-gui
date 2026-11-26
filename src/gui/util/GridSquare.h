#ifndef GRID_SQUARE_H
#define GRID_SQUARE_H

#include <wx/string.h>

// Calculations on Maidenhead grid squares (e.g. "DM13" or "DM13qd"), as
// used to show how far away and in which direction other stations are.
namespace GridSquare
{
    // Returns true if the grid square is two letters followed by two digits,
    // with two more letters for a more precise square ("DM12", "DM12kw").
    // Anything else, including anything longer, isn't one.
    bool IsValid(wxString const& gridSquare);

    // Provides the latitude and longitude (in degrees) of the middle of the
    // grid square. Six character grid squares are located more precisely.
    // The grid square must be valid.
    void ToLatLon(wxString gridSquare, double& lat, double& lon);

    // Returns the distance between the two grid squares in kilometers.
    // Both grid squares must be valid.
    double DistanceInKm(wxString const& gridSquare1, wxString const& gridSquare2);

    // Returns the bearing from the first grid square to the second, in
    // degrees clockwise from north (0 to just under 360). Both grid squares
    // must be valid.
    double BearingInDegrees(wxString const& gridSquare1, wxString const& gridSquare2);

    // Returns the name of the compass point (e.g. "NNE") closest to the
    // given bearing, which must be between 0 and 360 degrees.
    wxString CardinalDirection(int degrees);
}

#endif // GRID_SQUARE_H
