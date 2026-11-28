#include <cmath>
#include <string>

#include <wx/init.h>

#include "../GridSquare.h"
#include "../../../test/UnitTestCommon.h"

// Maidenhead grid squares divide the world into 20 by 10 degree fields
// (two letters A-R), each into 2 by 1 degree squares (two digits), each of
// those into 5 by 2.5 minute subsquares (two letters A-X), and each of those
// into 30 by 15 second extended squares (two more digits). Expected
// values here were worked out by hand or with an independent calculation.

namespace {

bool isNear(const char* description, double actual, double expected, double tolerance)
{
    if (std::fabs(actual - expected) <= tolerance) return true;
    std::cout << "[" << description << " was " << actual << ", expected " << expected << " +/- " << tolerance << "] ";
    return false;
}

bool locationIs(const char* gridSquare, double expectedLat, double expectedLon)
{
    double lat = 0;
    double lon = 0;
    GridSquare::ToLatLon(gridSquare, lat, lon);

    bool result = true;
    result &= isNear((std::string("latitude of ") + gridSquare).c_str(), lat, expectedLat, 0.00001);
    result &= isNear((std::string("longitude of ") + gridSquare).c_str(), lon, expectedLon, 0.00001);
    return result;
}

bool validityIs(const char* gridSquare, bool expected)
{
    if (GridSquare::IsValid(gridSquare) == expected) return true;
    std::cout << "[\"" << gridSquare << "\" was " << (expected ? "rejected" : "accepted") << "] ";
    return false;
}

bool acceptedIs(const char* entered, const char* previous, const char* expected)
{
    auto actual = GridSquare::AcceptedValue(entered, previous);
    if (actual == expected) return true;
    std::cout << "[\"" << entered << "\" over \"" << previous << "\" left \""
              << actual.ToStdString() << "\", expected \"" << expected << "\"] ";
    return false;
}

bool acceptableIs(const char* entered, bool expected)
{
    if (GridSquare::IsAcceptable(entered) == expected) return true;
    std::cout << "[\"" << entered << "\" was " << (expected ? "not acceptable" : "acceptable") << "] ";
    return false;
}

bool directionIs(int degrees, const char* expected)
{
    auto actual = GridSquare::CardinalDirection(degrees);
    if (actual == expected) return true;
    std::cout << "[" << degrees << " degrees was " << actual.ToStdString() << ", expected " << expected << "] ";
    return false;
}

bool acceptsFieldsSquaresSubsquaresAndExtendedSquares()
{
    bool result = true;
    result &= validityIs("FN", true);
    result &= validityIs("fn", true);
    result &= validityIs("FN31", true);
    result &= validityIs("fn31", true);
    result &= validityIs("FN31pr", true);
    result &= validityIs("DM12KW", true);
    result &= validityIs("FN31pr25", true);
    result &= validityIs("FN31PR25", true);

    // The first and last value of each pair.
    result &= validityIs("AA00aa00", true);
    result &= validityIs("RR99xx99", true);
    return result;
}

bool rejectsIncompleteOrMalformedGridSquares()
{
    bool result = true;
    result &= validityIs("", false);
    result &= validityIs("F", false);
    result &= validityIs("FN3", false);
    result &= validityIs("1234", false);
    result &= validityIs("F3N1", false);
    result &= validityIs(" FN31", false);
    result &= validityIs("FN31p", false);
    result &= validityIs("FN3123", false);
    result &= validityIs("FN31pr2", false);
    result &= validityIs("FN31pr2a", false);
    result &= validityIs("FN31pr25 ", false);

    // Fields only go up to R, and subsquares only up to X.
    result &= validityIs("SA00", false);
    result &= validityIs("AS00", false);
    result &= validityIs("ZZ99", false);
    result &= validityIs("FN31ya", false);
    result &= validityIs("FN31ay", false);

    // Nothing past the extended square is formally defined.
    result &= validityIs("FN31pr25ab", false);
    result &= validityIs("FN31pr25ab12", false);
    return result;
}

bool acceptsGridSquaresAndNothingElseIgnoringSurroundingSpace()
{
    bool result = true;
    result &= acceptableIs("FN31", true);
    result &= acceptableIs(" fn31pr ", true);
    result &= acceptableIs("FN31PR25", true);
    result &= acceptableIs("", true);
    result &= acceptableIs("   ", true);
    result &= acceptableIs("DM1", false);
    result &= acceptableIs(" ZZ12ZZ ", false);
    result &= acceptableIs("FN 31", false);
    return result;
}

bool keepsThePreviousGridSquareWhenWhatWasTypedIsNotOne()
{
    bool result = true;

    // What was typed, when it is a grid square, with the space around it
    // removed.
    result &= acceptedIs("FN31", "DM12", "FN31");
    result &= acceptedIs(" fn31pr ", "DM12", "fn31pr");
    result &= acceptedIs("FN31PR25", "DM12", "FN31PR25");

    // An empty box is allowed: reporting started without a grid square is
    // what the main window says something about.
    result &= acceptedIs("", "DM12", "");
    result &= acceptedIs("   ", "DM12", "");

    // What isn't one leaves the grid square that was there before, so a typo
    // can't replace a good one.
    result &= acceptedIs("DM1", "FN31", "FN31");
    result &= acceptedIs("1234", "FN31", "FN31");
    result &= acceptedIs("FN31p", "FN31", "FN31");
    result &= acceptedIs("ZZ99", "FN31", "FN31");
    result &= acceptedIs("FN31pr25ab", "FN31", "FN31");
    return result;
}

bool twoCharacterFieldIsLocatedAtItsMiddle()
{
    bool result = true;
    result &= locationIs("FN", 45.0, -70.0);
    result &= locationIs("AA", -85.0, -170.0);
    result &= locationIs("RR", 85.0, 170.0);
    return result;
}

bool fourCharacterGridSquareIsLocatedAtItsMiddle()
{
    bool result = true;
    result &= locationIs("FN31", 41.5, -73.0);
    result &= locationIs("JO01", 51.5, 1.0);
    result &= locationIs("QF56", -33.5, 151.0);

    // The corners of the world.
    result &= locationIs("AA00", -89.5, -179.0);
    result &= locationIs("RR99", 89.5, 179.0);
    return result;
}

bool sixCharacterGridSquareIsLocatedMorePrecisely()
{
    bool result = true;

    // W1AW in Newington, Connecticut is at about 41.71N, 72.73W.
    result &= locationIs("FN31pr", 41.729167, -72.708333);
    result &= locationIs("FN31aa", 41.020833, -73.958333);
    result &= locationIs("FN31xx", 41.979167, -72.041667);
    return result;
}

bool eightCharacterGridSquareIsLocatedMorePreciselyStill()
{
    bool result = true;

    // Each extended square is 30 seconds of longitude by 15 of latitude.
    result &= locationIs("FN31pr25", 41.731250, -72.729167);
    result &= locationIs("FN31pr00", 41.710417, -72.745833);
    result &= locationIs("FN31pr99", 41.747917, -72.670833);
    result &= locationIs("fn31PR25", 41.731250, -72.729167);
    return result;
}

bool gridSquaresAreNotCaseSensitive()
{
    bool result = true;
    result &= locationIs("fn31", 41.5, -73.0);
    result &= locationIs("fn31PR", 41.729167, -72.708333);
    result &= locationIs("Fn31Pr", 41.729167, -72.708333);
    return result;
}

bool anythingAfterTheLastValidPairIsIgnored()
{
    bool result = true;
    result &= locationIs("FN3", 45.0, -70.0);
    result &= locationIs("FN31p", 41.5, -73.0);
    result &= locationIs("FN3112", 41.5, -73.0);
    result &= locationIs("FN31p1", 41.5, -73.0);
    result &= locationIs("FN31ya", 41.5, -73.0);
    result &= locationIs("FN31pr2", 41.729167, -72.708333);
    result &= locationIs("FN31prab", 41.729167, -72.708333);
    result &= locationIs("FN31pr25ab", 41.731250, -72.729167);
    return result;
}

bool distanceIsZeroToSameGridSquare()
{
    return isNear("distance", GridSquare::DistanceInKm("FN31pr", "FN31pr"), 0.0, 0.0001);
}

bool distanceFollowsGreatCircle()
{
    bool result = true;

    // One degree of latitude is about 111.2 km everywhere.
    result &= isNear("JJ00 to JJ01", GridSquare::DistanceInKm("JJ00", "JJ01"), 111.19, 0.01);

    // Connecticut to south east England, and to Sydney.
    result &= isNear("FN31 to JO01", GridSquare::DistanceInKm("FN31", "JO01"), 5523.9, 0.1);
    result &= isNear("QF56 to FN31", GridSquare::DistanceInKm("QF56", "FN31"), 16077.2, 0.1);
    return result;
}

bool distanceIsSameInBothDirections()
{
    return isNear("difference between directions",
        GridSquare::DistanceInKm("FN31", "JO01") - GridSquare::DistanceInKm("JO01", "FN31"), 0.0, 0.0001);
}

bool bearingIsMeasuredClockwiseFromNorth()
{
    bool result = true;

    // JJ00 is just north of the equator, where a degree east or west is
    // almost exactly east or west.
    result &= isNear("bearing north", GridSquare::BearingInDegrees("JJ00", "JJ01"), 0.0, 0.0001);
    result &= isNear("bearing east", GridSquare::BearingInDegrees("JJ00", "JJ10"), 89.99, 0.01);
    result &= isNear("bearing south", GridSquare::BearingInDegrees("JJ01", "JJ00"), 180.0, 0.0001);
    result &= isNear("bearing west", GridSquare::BearingInDegrees("JJ10", "JJ00"), 270.01, 0.01);
    return result;
}

bool bearingFollowsGreatCircle()
{
    bool result = true;

    // England is north east of Connecticut by the shortest route even
    // though it's at a similar latitude, and the way back is north west.
    result &= isNear("FN31 to JO01", GridSquare::BearingInDegrees("FN31", "JO01"), 51.71, 0.01);
    result &= isNear("JO01 to FN31", GridSquare::BearingInDegrees("JO01", "FN31"), 289.22, 0.01);
    return result;
}

bool bearingStaysBelow360()
{
    bool result = true;

    // Due north could come out as either 0 or 360; it's always reported as 0.
    for (const char* destination : { "JJ01", "JJ05", "JO01", "JR01" })
    {
        double bearing = GridSquare::BearingInDegrees("JJ00", destination);
        if (bearing < 0.0 || bearing >= 360.0)
        {
            std::cout << "[bearing to " << destination << " was " << bearing << "] ";
            result = false;
        }
    }
    return result;
}

bool namesCompassPoints()
{
    bool result = true;
    result &= directionIs(0, "N");
    result &= directionIs(45, "NE");
    result &= directionIs(90, "E");
    result &= directionIs(135, "SE");
    result &= directionIs(180, "S");
    result &= directionIs(225, "SW");
    result &= directionIs(270, "W");
    result &= directionIs(315, "NW");
    result &= directionIs(23, "NNE");
    result &= directionIs(338, "NNW");
    return result;
}

bool compassPointChangesHalfwayBetweenNeighbors()
{
    bool result = true;

    // Each point covers 22.5 degrees centered on itself.
    result &= directionIs(11, "N");
    result &= directionIs(12, "NNE");
    result &= directionIs(33, "NNE");
    result &= directionIs(34, "NE");
    result &= directionIs(348, "NNW");
    result &= directionIs(349, "N");
    result &= directionIs(359, "N");
    result &= directionIs(360, "N");
    return result;
}

} // namespace

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk())
    {
        std::cout << "Could not initialize wxWidgets" << std::endl;
        return -1;
    }

    executeTestCase("acceptsFieldsSquaresSubsquaresAndExtendedSquares", acceptsFieldsSquaresSubsquaresAndExtendedSquares);
    executeTestCase("rejectsIncompleteOrMalformedGridSquares", rejectsIncompleteOrMalformedGridSquares);
    executeTestCase("acceptsGridSquaresAndNothingElseIgnoringSurroundingSpace", acceptsGridSquaresAndNothingElseIgnoringSurroundingSpace);
    executeTestCase("keepsThePreviousGridSquareWhenWhatWasTypedIsNotOne", keepsThePreviousGridSquareWhenWhatWasTypedIsNotOne);
    executeTestCase("twoCharacterFieldIsLocatedAtItsMiddle", twoCharacterFieldIsLocatedAtItsMiddle);
    executeTestCase("fourCharacterGridSquareIsLocatedAtItsMiddle", fourCharacterGridSquareIsLocatedAtItsMiddle);
    executeTestCase("sixCharacterGridSquareIsLocatedMorePrecisely", sixCharacterGridSquareIsLocatedMorePrecisely);
    executeTestCase("eightCharacterGridSquareIsLocatedMorePreciselyStill", eightCharacterGridSquareIsLocatedMorePreciselyStill);
    executeTestCase("gridSquaresAreNotCaseSensitive", gridSquaresAreNotCaseSensitive);
    executeTestCase("anythingAfterTheLastValidPairIsIgnored", anythingAfterTheLastValidPairIsIgnored);
    executeTestCase("distanceIsZeroToSameGridSquare", distanceIsZeroToSameGridSquare);
    executeTestCase("distanceFollowsGreatCircle", distanceFollowsGreatCircle);
    executeTestCase("distanceIsSameInBothDirections", distanceIsSameInBothDirections);
    executeTestCase("bearingIsMeasuredClockwiseFromNorth", bearingIsMeasuredClockwiseFromNorth);
    executeTestCase("bearingFollowsGreatCircle", bearingFollowsGreatCircle);
    executeTestCase("bearingStaysBelow360", bearingStaysBelow360);
    executeTestCase("namesCompassPoints", namesCompassPoints);
    executeTestCase("compassPointChangesHalfwayBetweenNeighbors", compassPointChangesHalfwayBetweenNeighbors);

    return testResult();
}
