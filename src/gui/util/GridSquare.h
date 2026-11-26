#ifndef GRID_SQUARE_H
#define GRID_SQUARE_H

#include <wx/string.h>

// Calculations on Maidenhead grid squares (e.g. "DM13" or "DM13qd"), as
// used to show how far away and in which direction other stations are.
namespace GridSquare
{
    // Returns true if the grid square is a Maidenhead locator: a field of
    // two letters A-R ("DM"), optionally followed by a square of two digits
    // ("DM12"), then a subsquare of two letters A-X ("DM12kw"), then an
    // extended square of two digits ("DM12kw47"). Either case is accepted.
    // Anything else, including anything longer, isn't one.
    bool IsValid(wxString const& gridSquare);

    // What a dialog's grid square box leaves in the configuration: what was
    // typed, with whitespace around it removed, when it's a grid square or
    // the box is empty; otherwise the value that was there before, so that a
    // typo like "DM1" can't replace a good one.
    wxString AcceptedValue(wxString entered, wxString const& previous);

    // Provides the latitude and longitude (in degrees) of the middle of the
    // grid square, as precisely as its length allows. The grid square must
    // be valid; given one that isn't, only the valid pairs it starts with
    // are used.
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
