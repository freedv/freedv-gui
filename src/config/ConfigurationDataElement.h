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

#include <cassert>
#include <functional>
#include <mutex>

// What it takes for a change to a setting to take effect while FreeDV is
// running. Most settings are read each time they're used and need nothing;
// the rest are read once, on Start, and are given a set of these. Merging
// changes into the configuration (FreeDVConfiguration::mergeChangesFrom())
// says which are needed, through FreeDVConfiguration::onChanged.
namespace ConfigChange
{
    enum Effect : unsigned
    {
        NONE = 0,
        RESTARTS_AUDIO = 1u << 0,
        RECONNECTS_RADIO = 1u << 1,
        REOPENS_PTT_INPUT = 1u << 2,
        RESTARTS_REPORTING = 1u << 3,
        // FreeDV Reporter is told who and where this station is when it
        // connects, so changing that means connecting again (and briefly
        // leaving its list). Reporting restarts too.
        RECONNECTS_FREEDV_REPORTER = 1u << 4,
        // The running audio keeps a copy of it, which is updated as soon as
        // nothing's starting, stopping or restarting it (no restart needed).
        UPDATES_RUNNING_AUDIO = 1u << 5,
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
    virtual unsigned getChangeEffects() const = 0;

    // other is the same setting in another copy of the configuration.
    virtual bool hasSameValueAs(IConfigurationDataElement& other) = 0;
    virtual void copyValueFrom(IConfigurationDataElement& other) = 0;
};

template<typename UnderlyingDataType>
class ConfigurationDataElement : public IConfigurationDataElement
{
public:
    ConfigurationDataElement(const char* elementName, UnderlyingDataType const& defaultVal, unsigned changeEffects = ConfigChange::NONE);
    virtual ~ConfigurationDataElement() = default;

    // Elements are read and written from several threads (the UI thread,
    // the start/stop worker threads, dialogs). Every access below is
    // serialized by dataMutex_, and no accessor exposes the stored value
    // itself, so a copy or assignment is always a consistent snapshot.
    ConfigurationDataElement(const ConfigurationDataElement&) = delete;
    ConfigurationDataElement& operator=(const ConfigurationDataElement&) = delete;

    const char* getElementName() const override;
    unsigned getChangeEffects() const override;

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
private:
    const char* elementName_;
    unsigned changeEffects_;
    UnderlyingDataType data_;
    UnderlyingDataType default_;
    
    std::function<UnderlyingDataType(UnderlyingDataType)> saveProcessor_;
    std::function<UnderlyingDataType(UnderlyingDataType)> loadProcessor_;

    // Guards data_, default_ and the two processors above.
    mutable std::mutex dataMutex_;
};

template<typename UnderlyingDataType>
ConfigurationDataElement<UnderlyingDataType>::ConfigurationDataElement(const char* elementName, UnderlyingDataType const& defaultVal, unsigned changeEffects)
    : elementName_(elementName)
    , changeEffects_(changeEffects)
    , data_(defaultVal)
    , default_(defaultVal)
    , saveProcessor_(nullptr)
    , loadProcessor_(nullptr)
{
    // empty
}

template<typename UnderlyingDataType>
const char* ConfigurationDataElement<UnderlyingDataType>::getElementName() const
{
    return elementName_;
}

template<typename UnderlyingDataType>
unsigned ConfigurationDataElement<UnderlyingDataType>::getChangeEffects() const
{
    return changeEffects_;
}

template<typename UnderlyingDataType>
bool ConfigurationDataElement<UnderlyingDataType>::hasSameValueAs(IConfigurationDataElement& other)
{
    auto* same = dynamic_cast<ConfigurationDataElement<UnderlyingDataType>*>(&other);
    assert(same != nullptr && same != this);
    if (same == nullptr)
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
    auto* same = dynamic_cast<ConfigurationDataElement<UnderlyingDataType>*>(&other);
    assert(same != nullptr && same != this);
    if (same == nullptr)
    {
        return;
    }

    auto otherDefault = same->getDefaultVal();
    auto otherVal = same->getWithoutProcessing();

    std::lock_guard<std::mutex> lock(dataMutex_);
    default_ = otherDefault;
    data_ = otherVal;
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
    
    data_ = val;
    return *this;
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setWithoutProcessing(UnderlyingDataType const& val)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    data_ = val;
}

template<typename UnderlyingDataType>
template<typename Fn>
void ConfigurationDataElement<UnderlyingDataType>::update(Fn&& fn)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    fn(data_);
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

#endif // CONFIGURATION_DATA_ELEMENT_H
