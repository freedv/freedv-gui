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
#include <functional>

template<typename UnderlyingDataType>
class ConfigurationDataElement
{
public:
    ConfigurationDataElement(const char* elementName, UnderlyingDataType const& defaultVal);
    virtual ~ConfigurationDataElement() = default;
    
    const char* getElementName() const;
    
    void setDefaultVal(UnderlyingDataType& val); // useful for migration
    UnderlyingDataType& getDefaultVal();
    
    // Allows usage of class as though it were defined directly as UnderlyingDataType.
    ConfigurationDataElement<UnderlyingDataType>& operator=(UnderlyingDataType val);
    operator UnderlyingDataType();
    
    UnderlyingDataType* operator->();
    
    UnderlyingDataType get();
    UnderlyingDataType getWithoutProcessing();
    void setWithoutProcessing(UnderlyingDataType const& val);
    
    void setSaveProcessor(std::function<UnderlyingDataType(UnderlyingDataType)> fn);
    void setLoadProcessor(std::function<UnderlyingDataType(UnderlyingDataType)> fn);

    // Keeps the value within [minVal, maxVal] however it's set, including
    // from the config file, where anything can have been written: one
    // outside the range is brought to the nearer end of it.
    void setLimits(UnderlyingDataType minVal, UnderlyingDataType maxVal);
private:
    const char* elementName_;
    UnderlyingDataType data_;
    UnderlyingDataType default_;
    
    std::function<UnderlyingDataType(UnderlyingDataType)> saveProcessor_;
    std::function<UnderlyingDataType(UnderlyingDataType)> loadProcessor_;
    std::function<UnderlyingDataType(UnderlyingDataType)> limit_;
};

template<typename UnderlyingDataType>
ConfigurationDataElement<UnderlyingDataType>::ConfigurationDataElement(const char* elementName, UnderlyingDataType const& defaultVal)
    : elementName_(elementName)
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
UnderlyingDataType& ConfigurationDataElement<UnderlyingDataType>::getDefaultVal()
{
    return default_;
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setDefaultVal(UnderlyingDataType& val)
{
    default_ = val;
}

template<typename UnderlyingDataType>
ConfigurationDataElement<UnderlyingDataType>& ConfigurationDataElement<UnderlyingDataType>::operator=(UnderlyingDataType val)
{
    if (saveProcessor_)
    {
        val = saveProcessor_(val);
    }
    if (limit_)
    {
        val = limit_(val);
    }
    
    setWithoutProcessing(val);
    return *this;
}

template<typename UnderlyingDataType>
UnderlyingDataType* ConfigurationDataElement<UnderlyingDataType>::operator->()
{
    return &data_;
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setWithoutProcessing(UnderlyingDataType const& val)
{
    data_ = limit_ ? limit_(val) : val;
}

template<typename UnderlyingDataType>
UnderlyingDataType ConfigurationDataElement<UnderlyingDataType>::getWithoutProcessing()
{
    return data_;
}

template<typename UnderlyingDataType>
ConfigurationDataElement<UnderlyingDataType>::operator UnderlyingDataType()
{
    if (loadProcessor_)
    {
        return loadProcessor_(data_);
    }
    
    return data_;
}

template<typename UnderlyingDataType>
UnderlyingDataType ConfigurationDataElement<UnderlyingDataType>::get()
{
    if (loadProcessor_)
    {
        return loadProcessor_(data_);
    }
    
    return data_;
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setSaveProcessor(std::function<UnderlyingDataType(UnderlyingDataType)> fn)
{
    saveProcessor_ = std::move(fn);
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setLoadProcessor(std::function<UnderlyingDataType(UnderlyingDataType)> fn)
{
    loadProcessor_ = std::move(fn);
}

template<typename UnderlyingDataType>
void ConfigurationDataElement<UnderlyingDataType>::setLimits(UnderlyingDataType minVal, UnderlyingDataType maxVal)
{
    limit_ = [minVal, maxVal](UnderlyingDataType val) { return std::clamp(val, minVal, maxVal); };
    data_ = limit_(data_);
}

#endif // CONFIGURATION_DATA_ELEMENT_H
