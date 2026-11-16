#include <string>
#include <vector>

#include "../ConfigurationDataElement.h"
#include "../../test/UnitTestCommon.h"

namespace {

bool startsWithDefaultValue()
{
    ConfigurationDataElement<int> element("/Test/Value", 42);
    return 
        std::string(element.getElementName()) == "/Test/Value" &&
        element.get() == 42 &&
        element.getWithoutProcessing() == 42 &&
        element.getDefaultVal() == 42;
}

bool assignmentChangesValueButNotDefault()
{
    ConfigurationDataElement<int> element("/Test/Value", 42);
    element = 7;

    int converted = element;
    return element.get() == 7 && converted == 7 && element.getDefaultVal() == 42;
}

bool changingDefaultDoesNotChangeValue()
{
    // Used during migrations, prior to the value being loaded.
    ConfigurationDataElement<int> element("/Test/Value", 42);
    int newDefault = 99;
    element.setDefaultVal(newDefault);
    return element.getDefaultVal() == 99 && element.get() == 42;
}

bool arrowOperatorAccessesUnderlyingValue()
{
    ConfigurationDataElement<std::string> element("/Test/Value", "abc");
    if (element->size() != 3) return false;

    element->append("def");
    return element.get() == "abcdef";
}

bool saveProcessorAppliesOnAssignment()
{
    ConfigurationDataElement<int> element("/Test/Value", 1);
    element.setSaveProcessor([](int val) { return val * 100; });

    // Default is taken as is, since it's already in stored form.
    if (element.getWithoutProcessing() != 1) return false;

    element = 5;
    return element.getWithoutProcessing() == 500;
}

// Limits hold however the value is set: assigned (from Settings) or set
// without processing (from the config file).
bool limitsApplyHoweverValueIsSet()
{
    ConfigurationDataElement<int> element("/Test/Value", 50);
    element.setLimits(10, 100);

    bool result = element.get() == 50;
    element = 5;
    result &= element.get() == 10;
    element = 1000;
    result &= element.get() == 100;
    element.setWithoutProcessing(-7);
    result &= element.get() == 10;
    element = 42;
    result &= element.get() == 42;
    return result;
}

// One set before the limits come in is brought within them.
bool limitsApplyToExistingValue()
{
    ConfigurationDataElement<unsigned int> element("/Test/Value", 0x1FF);
    element.setLimits(0, 0xFF);
    return element.get() == 0xFF;
}

bool loadProcessorAppliesOnRead()
{
    ConfigurationDataElement<int> element("/Test/Value", 300);
    element.setLoadProcessor([](int val) { return val / 100; });

    int converted = element;
    return element.get() == 3 && converted == 3 && element.getWithoutProcessing() == 300;
}

bool processorsRoundTrip()
{
    // Same arrangement as the codec2 post filter gamma/beta settings.
    ConfigurationDataElement<float> element("/Test/Value", 50);
    element.setSaveProcessor([](float val) { return val * 100; });
    element.setLoadProcessor([](float val) { return val / 100; });

    if (element.get() != 0.5f) return false;

    element = 0.25f;
    return element.getWithoutProcessing() == 25.0f && element.get() == 0.25f;
}

bool withoutProcessingBypassesProcessors()
{
    ConfigurationDataElement<int> element("/Test/Value", 0);
    element.setSaveProcessor([](int val) { return val * 100; });
    element.setLoadProcessor([](int val) { return val / 100; });

    // Used when loading from/saving to the configuration file.
    element.setWithoutProcessing(1200);
    return element.getWithoutProcessing() == 1200 && element.get() == 12;
}

bool worksWithContainers()
{
    ConfigurationDataElement<std::vector<std::string>> element("/Test/Value", {});
    element.setSaveProcessor([](std::vector<std::string> val) {
        for (auto& item : val) item = "stored:" + item;
        return val;
    });

    element = std::vector<std::string> { "a", "b" };
    return element.getWithoutProcessing() == std::vector<std::string> { "stored:a", "stored:b" };
}

}

int main()
{
    executeTestCase("startsWithDefaultValue", startsWithDefaultValue);
    executeTestCase("assignmentChangesValueButNotDefault", assignmentChangesValueButNotDefault);
    executeTestCase("changingDefaultDoesNotChangeValue", changingDefaultDoesNotChangeValue);
    executeTestCase("arrowOperatorAccessesUnderlyingValue", arrowOperatorAccessesUnderlyingValue);
    executeTestCase("saveProcessorAppliesOnAssignment", saveProcessorAppliesOnAssignment);
    executeTestCase("loadProcessorAppliesOnRead", loadProcessorAppliesOnRead);
    executeTestCase("limitsApplyHoweverValueIsSet", limitsApplyHoweverValueIsSet);
    executeTestCase("limitsApplyToExistingValue", limitsApplyToExistingValue);
    executeTestCase("processorsRoundTrip", processorsRoundTrip);
    executeTestCase("withoutProcessingBypassesProcessors", withoutProcessingBypassesProcessors);
    executeTestCase("worksWithContainers", worksWithContainers);
    return testResult();
}
