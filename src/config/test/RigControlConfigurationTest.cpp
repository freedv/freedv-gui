#include "../RigControlConfiguration.h"
#include "ConfigTestCommon.h"

namespace {

void loadFrom(RigControlConfiguration& rig, wxString const& contents)
{
    auto config = makeConfig(contents);
    rig.load(config.get());
}

bool defaultsMatchApplication()
{
    RigControlConfiguration rig;
    loadFrom(rig, "");

    bool result = true;
    result &= check("hamlibUseForPTT", rig.hamlibUseForPTT.get(), false);
    result &= check("hamlibEnableFreqModeChanges", rig.hamlibEnableFreqModeChanges.get(), true);
    result &= check("hamlibEnableFreqChangesOnly", rig.hamlibEnableFreqChangesOnly.get(), false);
    result &= check("hamlibUseAnalogModes", rig.hamlibUseAnalogModes.get(), false);
    result &= check("hamlibSaveRestoreFrequency", rig.hamlibSaveRestoreFrequency.get(), true);
    result &= check("hamlibIcomCIVAddress", rig.hamlibIcomCIVAddress.get(), 0u);
    result &= check("hamlibRigName", rig.hamlibRigName.get(), "");
    result &= check("hamlibPTTType", rig.hamlibPTTType.get(), 0u);
    result &= check("hamlibSerialRate", rig.hamlibSerialRate.get(), 0u);
    result &= check("hamlibSerialPort", rig.hamlibSerialPort.get(), "");
    result &= check("hamlibPttSerialPort", rig.hamlibPttSerialPort.get(), "");
    result &= check("hamlibForceRTSOn", rig.hamlibForceRTSOn.get(), false);
    result &= check("hamlibForceDTROn", rig.hamlibForceDTROn.get(), false);
    result &= check("useSerialPTT", rig.useSerialPTT.get(), false);
    result &= check("serialPTTPort", rig.serialPTTPort.get(), "");
    result &= check("serialPTTUseRTS", rig.serialPTTUseRTS.get(), true);
    result &= check("serialPTTPolarityRTS", rig.serialPTTPolarityRTS.get(), true);
    result &= check("serialPTTUseDTR", rig.serialPTTUseDTR.get(), false);
    result &= check("serialPTTPolarityDTR", rig.serialPTTPolarityDTR.get(), false);
    result &= check("useSerialPTTInput", rig.useSerialPTTInput.get(), false);
    result &= check("serialPTTInputPort", rig.serialPTTInputPort.get(), "");
    result &= check("serialPTTInputPolarityCTS", rig.serialPTTInputPolarityCTS.get(), false);
    result &= check("leftChannelVoxTone", rig.leftChannelVoxTone.get(), false);
    result &= check("totTimerEnabled", rig.totTimerEnabled.get(), true);
    result &= check("totTimerSecs", rig.totTimerSecs.get(), 180);
    return result;
}

bool roundTripPreservesValues()
{
    auto config = makeConfig();

    RigControlConfiguration rig;
    rig.load(config.get());

    rig.hamlibUseForPTT = true;
    rig.hamlibEnableFreqModeChanges = false;
    rig.hamlibEnableFreqChangesOnly = true;
    rig.hamlibUseAnalogModes = true;
    rig.hamlibSaveRestoreFrequency = false;
    rig.hamlibIcomCIVAddress = 0x1a;
    // Rig names are free text and may legitimately contain '$' (e.g. a path
    // the operator keeps in their home directory); loading must not rewrite it.
    rig.hamlibRigName = wxString::FromUTF8("ICOM IC-7300 ($HOME)");
    rig.hamlibPTTType = 2;
    rig.hamlibSerialRate = 9600;
    rig.hamlibSerialPort = wxString::FromUTF8("/dev/ttyUSB0");
    rig.hamlibPttSerialPort = wxString::FromUTF8("/dev/ttyUSB1");
    rig.hamlibForceRTSOn = true;
    rig.useSerialPTT = true;
    rig.serialPTTPort = wxString::FromUTF8("/dev/ttyS0");
    rig.serialPTTUseRTS = false;
    rig.serialPTTUseDTR = true;
    rig.serialPTTPolarityDTR = true;
    rig.useSerialPTTInput = true;
    rig.serialPTTInputPort = wxString::FromUTF8("/dev/ttyS1");
    rig.serialPTTInputPolarityCTS = true;
    rig.leftChannelVoxTone = true;
    rig.totTimerEnabled = false;
    rig.totTimerSecs = 300;

    rig.save(config.get());

    auto reloaded = reload(*config);
    RigControlConfiguration afterRestart;
    afterRestart.load(reloaded.get());

    bool result = true;
    result &= check("hamlibUseForPTT", afterRestart.hamlibUseForPTT.get(), true);
    result &= check("hamlibEnableFreqModeChanges", afterRestart.hamlibEnableFreqModeChanges.get(), false);
    result &= check("hamlibEnableFreqChangesOnly", afterRestart.hamlibEnableFreqChangesOnly.get(), true);
    result &= check("hamlibUseAnalogModes", afterRestart.hamlibUseAnalogModes.get(), true);
    result &= check("hamlibSaveRestoreFrequency", afterRestart.hamlibSaveRestoreFrequency.get(), false);
    result &= check("hamlibIcomCIVAddress", afterRestart.hamlibIcomCIVAddress.get(), 0x1au);
    result &= check("hamlibRigName", afterRestart.hamlibRigName.get(), "ICOM IC-7300 ($HOME)");
    result &= check("hamlibPTTType", afterRestart.hamlibPTTType.get(), 2u);
    result &= check("hamlibSerialRate", afterRestart.hamlibSerialRate.get(), 9600u);
    result &= check("hamlibSerialPort", afterRestart.hamlibSerialPort.get(), "/dev/ttyUSB0");
    result &= check("hamlibPttSerialPort", afterRestart.hamlibPttSerialPort.get(), "/dev/ttyUSB1");
    result &= check("hamlibForceRTSOn", afterRestart.hamlibForceRTSOn.get(), true);
    result &= check("useSerialPTT", afterRestart.useSerialPTT.get(), true);
    result &= check("serialPTTPort", afterRestart.serialPTTPort.get(), "/dev/ttyS0");
    result &= check("serialPTTUseRTS", afterRestart.serialPTTUseRTS.get(), false);
    result &= check("serialPTTUseDTR", afterRestart.serialPTTUseDTR.get(), true);
    result &= check("serialPTTPolarityDTR", afterRestart.serialPTTPolarityDTR.get(), true);
    result &= check("useSerialPTTInput", afterRestart.useSerialPTTInput.get(), true);
    result &= check("serialPTTInputPort", afterRestart.serialPTTInputPort.get(), "/dev/ttyS1");
    result &= check("serialPTTInputPolarityCTS", afterRestart.serialPTTInputPolarityCTS.get(), true);
    result &= check("leftChannelVoxTone", afterRestart.leftChannelVoxTone.get(), true);
    result &= check("totTimerEnabled", afterRestart.totTimerEnabled.get(), false);
    result &= check("totTimerSecs", afterRestart.totTimerSecs.get(), 300);
    return result;
}

} // namespace

int main()
{
    executeTestCase("defaultsMatchApplication", defaultsMatchApplication);
    executeTestCase("roundTripPreservesValues", roundTripPreservesValues);

    return testResult();
}
