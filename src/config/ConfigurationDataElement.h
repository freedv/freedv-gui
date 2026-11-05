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

#include <functional>
#include <mutex>

template<typename UnderlyingDataType>
class ConfigurationDataElement
{
public:
    ConfigurationDataElement(const char* elementName, UnderlyingDataType const& defaultVal);
    virtual ~ConfigurationDataElement() = default;

    // Elements are read and written from several threads (the UI thread,
    // the start/stop worker threads, dialogs). Every access below is
    // serialized by dataMutex_, and no accessor exposes the stored value
    // itself, so a copy or assignment is always a consistent snapshot.
    ConfigurationDataElement(const ConfigurationDataElement&) = delete;
    ConfigurationDataElement& operator=(const ConfigurationDataElement&) = delete;

    const char* getElementName() const;
    
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
    UnderlyingDataType data_;
    UnderlyingDataType default_;
    
    std::function<UnderlyingDataType(UnderlyingDataType)> saveProcessor_;
    std::function<UnderlyingDataType(UnderlyingDataType)> loadProcessor_;

    // Guards data_, default_ and the two processors above.
    mutable std::mutex dataMutex_;
};

template<typename UnderlyingDataType>
ConfigurationDataElement<UnderlyingDataType>::ConfigurationDataElement(const char* elementName, UnderlyingDataType const& defaultVal)
    : elementName_(elementName)
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
