//==========================================================================
// Name:            WxWidgetsConfigStore.h
// Purpose:         Implements wxWidgets-specific configuration handling
// Created:         July 2, 2023
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

#ifndef WXWIDGETS_CONFIG_STORE_H
#define WXWIDGETS_CONFIG_STORE_H

#include <map>
#include <vector>
#include <wx/config.h>
#include <wx/string.h>
#include "ConfigurationDataElement.h"

class WxWidgetsConfigStore
{
public:
    virtual ~WxWidgetsConfigStore() = default;
    
    virtual void load(wxConfigBase* config) = 0;
    virtual void save(wxConfigBase* config) = 0;
    
protected:
    template<typename UnderlyingDataType>
    void load_(wxConfigBase* config, ConfigurationDataElement<UnderlyingDataType>& configElement);
    
    template<typename UnderlyingDataType>
    void save_(wxConfigBase* config, ConfigurationDataElement<UnderlyingDataType>& configElement);
    
    wxString generateStringFromArray_(std::vector<wxString> const& vec);
    std::vector<wxString> generateStrArrayFromString_(wxString const& str);

    wxString generateStringFromArray_(std::vector<int> const& vec);
    std::vector<int> generateNumArrayFromString_(wxString const& str);

    wxString generateStringFromArray_(std::vector<bool> const& vec);
    std::vector<bool> generateBoolArrayFromString_(wxString const& str);
};

template<typename UnderlyingDataType>
void WxWidgetsConfigStore::load_(wxConfigBase* config, ConfigurationDataElement<UnderlyingDataType>& configElement)
{
    UnderlyingDataType val;
    config->Read(configElement.getElementName(), &val, configElement.getDefaultVal());
    configElement.setWithoutProcessing(val);
}

template<typename UnderlyingDataType>
void WxWidgetsConfigStore::save_(wxConfigBase* config, ConfigurationDataElement<UnderlyingDataType>& configElement)
{
    config->Write(configElement.getElementName(), configElement.getWithoutProcessing());
}

template<>
void WxWidgetsConfigStore::load_<int>(wxConfigBase* config, ConfigurationDataElement<int>& configElement);

template<>
void WxWidgetsConfigStore::load_<unsigned int>(wxConfigBase* config, ConfigurationDataElement<unsigned int>& configElement);

template<>
void WxWidgetsConfigStore::load_<wxString>(wxConfigBase* config, ConfigurationDataElement<wxString>& configElement);

// Special handling for loading and saving string arrays.
template<>
void WxWidgetsConfigStore::load_<std::vector<wxString> >(wxConfigBase* config, ConfigurationDataElement<std::vector<wxString> >& configElement);
template<>
void WxWidgetsConfigStore::save_<std::vector<wxString> >(wxConfigBase* config, ConfigurationDataElement<std::vector<wxString> >& configElement);

// Special handling for loading and saving int arrays.
template<>
void WxWidgetsConfigStore::load_<std::vector<int> >(wxConfigBase* config, ConfigurationDataElement<std::vector<int> >& configElement);
template<>
void WxWidgetsConfigStore::save_<std::vector<int> >(wxConfigBase* config, ConfigurationDataElement<std::vector<int> >& configElement);

// Special handling for loading and saving bool arrays.
template<>
void WxWidgetsConfigStore::load_<std::vector<bool> >(wxConfigBase* config, ConfigurationDataElement<std::vector<bool> >& configElement);
template<>
void WxWidgetsConfigStore::save_<std::vector<bool> >(wxConfigBase* config, ConfigurationDataElement<std::vector<bool> >& configElement);

// Special handling for loading and saving a string-keyed int map (stored as a config group).
template<>
void WxWidgetsConfigStore::load_<std::map<wxString, int> >(wxConfigBase* config, ConfigurationDataElement<std::map<wxString, int> >& configElement);
template<>
void WxWidgetsConfigStore::save_<std::map<wxString, int> >(wxConfigBase* config, ConfigurationDataElement<std::map<wxString, int> >& configElement);

#endif // WXWIDGETS_CONFIG_STORE_H
