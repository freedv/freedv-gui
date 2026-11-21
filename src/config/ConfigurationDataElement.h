//==========================================================================
// Name:            ConfigurationDataElement.h
// Purpose:         Implements the interface for a data item in the config.
// Created:         July 1, 2023
// Authors:         Mooneer Salem
// 
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
//  License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//==========================================================================

#ifndef CONFIGURATION_DATA_ELEMENT_H
#define CONFIGURATION_DATA_ELEMENT_H

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <mutex>

// What it takes for a change to a setting to take effect while FreeDV is
// running. Most settings are read each time they're used and need nothing;
// the rest are read once, on Start, and are given a set of these. Merging
// changes into the configuration (FreeDVConfiguration::mergeChangesFrom())
// says which are needed, through FreeDVConfiguration::onChanged.
namespace ConfigChange
{
    enum Effect : uint32_t
    {
        NONE = 0,
        RESTARTS_AUDIO = UINT32_C(1) << 0,
        RECONNECTS_RADIO = UINT32_C(1) << 1,
        REOPENS_PTT_INPUT = UINT32_C(1) << 2,
        RESTARTS_REPORTING = UINT32_C(1) << 3,
        // FreeDV Reporter is told who and where this station is when it
        // connects, so changing that means connecting again (and briefly
        // leaving its list). Reporting restarts too.
        RECONNECTS_FREEDV_REPORTER = UINT32_C(1) << 4,
        // The running audio keeps a copy of it, which is updated as soon as
        // nothing's starting, stopping or restarting it (no restart needed).
        UPDATES_RUNNING_AUDIO = UINT32_C(1) << 5,
    };
}

// The type-independent part of a setting, so that the settings of two copies
// of a configuration can be gone through together (see
// WxWidgetsConfigStore::elements()).
class IConfigurationDataElement
{
public:
    virtual ~IConfigurationDataElement() = default;

    virtual const char* getElementName() const = 0;

    // ConfigChange::Effect flags.
    virtual uint32_t getChangeEffects() const = 0;

    // other is the same setting in another copy of the configuration.
    virtual bool hasSameValueAs(IConfigurationDataElement& other) = 0;
    virtual void copyValueFrom(IConfigurationDataElement& other) = 0;
};

template<typename UnderlyingDataType>
class ConfigurationDataElement : public IConfigurationDataElement
{
public:
    ConfigurationDataElement(const char* elementName, UnderlyingDataType const& defaultVal, uint32_t changeEffects = ConfigChange::NONE);
    virtual ~ConfigurationDataElement() = default;

    // Elements are read and written from several threads (the UI thread,
    // the start/stop worker threads, dialogs). Every access below is
    // serialized by dataMutex_, and no accessor exposes the stored value
    // itself, so a copy or assignment is always a consistent snapshot.
    ConfigurationDataElement(const ConfigurationDataElement&) = delete;
    ConfigurationDataElement& operator=(const ConfigurationDataElement&) = delete;

    const char* getElementName() const override;
    uint32_t getChangeEffects() const override;

    // The value as stored (as saved), and the default with it, which some
    // settings work out when the configuration's loaded.
    bool hasSameValueAs(IConfigurationDataElement& other) override;
    void copyValueFrom(IConfigurationDataElement& other) override;
    
    void setDefaultVal(UnderlyingDataType& val); // useful for migration
    UnderlyingDataType getDefaultVal();
    
    // Allows usage of class as though it were defined directly as UnderlyingDataType.
    ConfigurationDataElement<UnderlyingDataType>& operator=(UnderlyingDataType val);
    operator UnderlyingDataType();
    
    UnderlyingDataType get();
    UnderlyingDataType getWithoutProcessing();
    void setWithoutProcessing(UnderlyingDataType const& val);

    // Read-modify-write in one step: calls fn(value&) on the stored value
    // under the lock (with no load/save processing, as operator-> did), so
    // two threads updating it can't lose each other's changes. fn must not
    // use this element itself.
    template<typename Fn>
    void update(Fn&& fn);
    
    void setSaveProcessor(std::function<UnderlyingDataType(UnderlyingDataType)> fn);
    void setLoadProcessor(std::function<UnderlyingDataType(UnderlyingDataType)> fn);

    // Keeps the value within [minVal, maxVal] however it's set, including
    // from the config file, where anything can have been written: one
    // outside the range is brought to the nearer end of it.
    void setLimits(UnderlyingDataType minVal, UnderlyingDataType maxVal);
private:
    const char* elementName_;
    uint32_t changeEffects_;
    UnderlyingDataType data_;
    UnderlyingDataType default_;
    
    std::function<UnderlyingDataType(UnderlyingDataType)> saveProcessor_;
    std::function<UnderlyingDataType(UnderlyingDataType)> loadProcessor_;
    std::function<UnderlyingDataType(UnderlyingDataType)> limit_;

    // Guards data_, default_, the two processors and the limits above.
    mutable std::mutex dataMutex_;
};

template<typename UnderlyingDataType>
ConfigurationDataElement<UnderlyingDataType>::ConfigurationDataElement(const char* elementName, UnderlyingDataType const& defaultVal, uint32_t changeEffects)
    : elementName_(elementName)
    , changeEffects_(changeEffects)
    , data_(defaultVal)
    , default_(defaultVal)
    , saveProcessor_(nullptr)
    , loadProcessor_(nullptr)
    , limit_(nullptr)
{
    // empty
}

template<typename UnderlyingDataType>
const char* ConfigurationDataElement<UnderlyingDataType>::getElementName() const
{
    return elementName_;
}

template<typename UnderlyingDataType>
uint32_t ConfigurationDataElement<UnderlyingDataType>::getChangeEffects() const
{
    return changeEffects_;
}

template<typename UnderlyingDataType>
bool ConfigurationDataElement<UnderlyingDataType>::hasSameValueAs(IConfigurationDataElement& other)
{
    auto* same = dynamic_cast<ConfigurationDataElement<UnderlyingDataType>*>(&other); // GCOVR_EXCL_BR_LINE: gcov counts the cast's null test on this line and on the guard below, so both are marked
    assert(same != nullptr && same != this);
    if (same == nullptr) // GCOVR_EXCL_BR_LINE: only taken with the assert above compiled out
    {
        return false;
    }

    // One at a time: other's lock is never held with this one's.
    auto otherVal = same->getWithoutProcessing();
    return getWithoutProcessing() == otherVal;
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::copyValueFrom(IConfigurationDataElement& other)
{
    auto* same = dynamic_cast<ConfigurationDataElement<UnderlyingDataType>*>(&other); // GCOVR_EXCL_BR_LINE: see hasSameValueAs() above
    assert(same != nullptr && same != this);
    if (same == nullptr) // GCOVR_EXCL_BR_LINE: only taken with the assert above compiled out
    {
        return;
    }

    auto otherDefault = same->getDefaultVal();
    auto otherVal = same->getWithoutProcessing();

    std::lock_guard<std::mutex> lock(dataMutex_);
    default_ = otherDefault;
    data_ = limit_ ? limit_(otherVal) : otherVal;
}

template<typename UnderlyingDataType>
UnderlyingDataType ConfigurationDataElement<UnderlyingDataType>::getDefaultVal()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return default_;
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setDefaultVal(UnderlyingDataType& val)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    default_ = val;
}

template<typename UnderlyingDataType>
ConfigurationDataElement<UnderlyingDataType>& ConfigurationDataElement<UnderlyingDataType>::operator=(UnderlyingDataType val)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (saveProcessor_)
    {
        val = saveProcessor_(val);
    }
    if (limit_)
    {
        val = limit_(val);
    }
    
    data_ = val;
    return *this;
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setWithoutProcessing(UnderlyingDataType const& val)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    data_ = limit_ ? limit_(val) : val;
}

template<typename UnderlyingDataType>
template<typename Fn>
void ConfigurationDataElement<UnderlyingDataType>::update(Fn&& fn)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    fn(data_);
    if (limit_)
    {
        data_ = limit_(data_);
    }
}

template<typename UnderlyingDataType>
UnderlyingDataType ConfigurationDataElement<UnderlyingDataType>::getWithoutProcessing()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return data_;
}

template<typename UnderlyingDataType>
ConfigurationDataElement<UnderlyingDataType>::operator UnderlyingDataType()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (loadProcessor_)
    {
        return loadProcessor_(data_);
    }
    
    return data_;
}

template<typename UnderlyingDataType>
UnderlyingDataType ConfigurationDataElement<UnderlyingDataType>::get()
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (loadProcessor_)
    {
        return loadProcessor_(data_);
    }
    
    return data_;
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setSaveProcessor(std::function<UnderlyingDataType(UnderlyingDataType)> fn)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    saveProcessor_ = std::move(fn);
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setLoadProcessor(std::function<UnderlyingDataType(UnderlyingDataType)> fn)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    loadProcessor_ = std::move(fn);
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setLimits(UnderlyingDataType minVal, UnderlyingDataType maxVal)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    limit_ = [minVal, maxVal](UnderlyingDataType val) { return std::clamp(val, minVal, maxVal); };
    data_ = limit_(data_);
}

#endif // CONFIGURATION_DATA_ELEMENT_H
