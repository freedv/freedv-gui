//==========================================================================
// Name:            WxWidgetsConfigStore.cpp
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

#include <algorithm>
#include <climits>
#include <wx/tokenzr.h>
#include <wx/numformatter.h>
#include "WxWidgetsConfigStore.h"

// wxWidgets by default expands anything that looks like an environment variable
// (e.g. $HOME) when reading strings. Settings should instead come back exactly as 
// they were saved, so this turns that off while a string is being read.
class DisableEnvVarExpansion
{
public:
    explicit DisableEnvVarExpansion(wxConfigBase* config)
        : config_(config)
        , wasExpanding_(config->IsExpandingEnvVars())
    {
        config_->SetExpandEnvVars(false);
    }

    ~DisableEnvVarExpansion()
    {
        config_->SetExpandEnvVars(wasExpanding_);
    }

private:
    wxConfigBase* config_;
    bool wasExpanding_;
};

template<>
void WxWidgetsConfigStore::load_<wxString>(wxConfigBase* config, ConfigurationDataElement<wxString>& configElement)
{
    DisableEnvVarExpansion disableExpansion(config);

    wxString val;
    config->Read(configElement.getElementName(), &val, configElement.getDefaultVal());
    configElement.setWithoutProcessing(val);
}

// A number in the config file too big or small for the setting's type
// (anything can have been written there) would otherwise wrap around into
// some other value on the way in: the default is used instead.
template<>
void WxWidgetsConfigStore::load_<int>(wxConfigBase* config, ConfigurationDataElement<int>& configElement)
{
    long val;
    config->Read(configElement.getElementName(), &val, (long)configElement.getDefaultVal());
    if ((long long)val < INT_MIN || (long long)val > INT_MAX)
    {
        val = configElement.getDefaultVal();
    }
    configElement.setWithoutProcessing((int)val);
}

template<>
void WxWidgetsConfigStore::load_<unsigned int>(wxConfigBase* config, ConfigurationDataElement<unsigned int>& configElement)
{
    // Read as text and then as a 64 bit number, as a long isn't one on
    // every platform. Where it's 32 bits (Windows), one above INT_MAX is
    // written as the negative number with the same bits, so that's read
    // back as what it stands for; anything else outside an unsigned int
    // is taken as the default.
    wxString text;
    long long val = 0;
    if (!config->Read(configElement.getElementName(), &text) ||
        !text.Strip(wxString::both).ToLongLong(&val) ||
        val < INT_MIN || val > (long long)UINT_MAX)
    {
        configElement.setWithoutProcessing(configElement.getDefaultVal());
        return;
    }
    configElement.setWithoutProcessing((unsigned int)(val < 0 ? (unsigned int)(int)val : val));
}

template<>
void WxWidgetsConfigStore::load_<std::vector<int> >(wxConfigBase* config, ConfigurationDataElement<std::vector<int> >& configElement)
{
    wxString val;
    wxString defaultVal = generateStringFromArray_(configElement.getDefaultVal());
    
    config->Read(configElement.getElementName(), &val, defaultVal);
    configElement.setWithoutProcessing(generateNumArrayFromString_(val));
}

template<>
void WxWidgetsConfigStore::save_<std::vector<int> >(wxConfigBase* config, ConfigurationDataElement<std::vector<int> >& configElement)
{
    wxString val = generateStringFromArray_(configElement.getWithoutProcessing());
    config->Write(configElement.getElementName(), val);
}

template<>
void WxWidgetsConfigStore::load_<std::vector<bool> >(wxConfigBase* config, ConfigurationDataElement<std::vector<bool> >& configElement)
{
    wxString val;
    wxString defaultVal = generateStringFromArray_(configElement.getDefaultVal());
    
    config->Read(configElement.getElementName(), &val, defaultVal);
    configElement.setWithoutProcessing(generateBoolArrayFromString_(val));
}

template<>
void WxWidgetsConfigStore::save_<std::vector<bool> >(wxConfigBase* config, ConfigurationDataElement<std::vector<bool> >& configElement)
{
    wxString val = generateStringFromArray_(configElement.getWithoutProcessing());
    config->Write(configElement.getElementName(), val);
}

/* Note: for string arrays, we're treating them as a list of strings separated by commas. */

template<>
void WxWidgetsConfigStore::load_<std::vector<wxString> >(wxConfigBase* config, ConfigurationDataElement<std::vector<wxString> >& configElement)
{
    DisableEnvVarExpansion disableExpansion(config);

    wxString val;
    wxString defaultVal = generateStringFromArray_(configElement.getDefaultVal());
    
    config->Read(configElement.getElementName(), &val, defaultVal);
    configElement.setWithoutProcessing(generateStrArrayFromString_(val));
}

template<>
void WxWidgetsConfigStore::save_<std::vector<wxString> >(wxConfigBase* config, ConfigurationDataElement<std::vector<wxString> >& configElement)
{
    wxString val = generateStringFromArray_(configElement.getWithoutProcessing());
    config->Write(configElement.getElementName(), val);
}

template<>
void WxWidgetsConfigStore::load_<std::map<wxString, int> >(wxConfigBase* config, ConfigurationDataElement<std::map<wxString, int> >& configElement)
{
    std::map<wxString, int> result;
    config->SetPath(configElement.getElementName());
    wxString key;
    long cookie;
    bool hasMore = config->GetFirstEntry(key, cookie);
    while (hasMore)
    {
        long val;
        if (config->Read(key, &val))
            result[key] = (int)val;
        hasMore = config->GetNextEntry(key, cookie);
    }
    config->SetPath("/");
    configElement.setWithoutProcessing(result);
}

template<>
void WxWidgetsConfigStore::save_<std::map<wxString, int> >(wxConfigBase* config, ConfigurationDataElement<std::map<wxString, int> >& configElement)
{
    wxString path = configElement.getElementName();
    wxString groupName = path.AfterLast('/');
    wxString parentPath = path.BeforeLast('/');
    if (parentPath.IsEmpty()) parentPath = "/";

    config->SetPath(parentPath);
    config->DeleteGroup(groupName);
    config->SetPath(path);
    for (auto& kv : configElement.getWithoutProcessing())
        config->Write(kv.first, (long)kv.second);
    config->SetPath("/");
}

wxString WxWidgetsConfigStore::generateStringFromArray_(std::vector<int> const& vec)
{
    wxString rv = "";
    
    int count = vec.size();
    for (auto& item : vec)
    {
        wxString numAsString = wxNumberFormatter::ToString((long)item, wxNumberFormatter::Style_None);
        rv += numAsString;
        count--;
        
        if (count > 0)
        {
            rv += ",";
        }
    }
    
    return rv;
}

wxString WxWidgetsConfigStore::generateStringFromArray_(std::vector<bool> const& vec)
{
    std::vector<int> tmpVec;
    tmpVec.reserve(vec.size());
    for (auto v : vec)
    {
        tmpVec.push_back(v ? 1 : 0);
    }
    return generateStringFromArray_(tmpVec);
}

std::vector<bool> WxWidgetsConfigStore::generateBoolArrayFromString_(wxString const& str)
{
    std::vector<int> tmpVec = generateNumArrayFromString_(str);
    std::vector<bool> rv;
    
    rv.reserve(tmpVec.size());
    for (auto& v : tmpVec)
    {
        rv.push_back(v == 1 ? true : false);
    }

    return rv;
}

std::vector<int> WxWidgetsConfigStore::generateNumArrayFromString_(wxString const& str)
{
    std::vector<int> rv;
    
    wxStringTokenizer tokenizer(str, ",");
    while ( tokenizer.HasMoreTokens() )
    {
        wxString token = tokenizer.GetNextToken();
        wxLongLong_t tmp = 0;
        if (!wxNumberFormatter::FromString(token, &tmp))
        {
            tmp = 0;
        }
        // (Within an int, rather than wrapping around into some other value.)
        rv.push_back((int)std::clamp<long long>(tmp, INT_MIN, INT_MAX));
    }
    
    return rv;
}

wxString WxWidgetsConfigStore::generateStringFromArray_(std::vector<wxString> const& vec)
{
    wxString rv = "";

    int count = vec.size();
    for (auto& item : vec)
    {
        wxString escaped = item;
        escaped.Replace("\\", "\\\\");
        escaped.Replace(",", "\\,");
        rv += escaped;
        count--;

        if (count > 0)
        {
            rv += ",";
        }
    }

    return rv;
}

std::vector<wxString> WxWidgetsConfigStore::generateStrArrayFromString_(wxString const& str)
{
    std::vector<wxString> rv;
    if (str.IsEmpty()) return rv;

    wxString current;
    for (size_t i = 0; i < str.Length(); i++)
    {
        wxChar ch = str[i];
        if (ch == '\\' && i + 1 < str.Length())
        {
            wxChar next = str[i + 1];
            if (next == ',' || next == '\\')
            {
                current += next;
                i++;
                continue;
            }
        }
        if (ch == ',')
        {
            rv.push_back(current);
            current.Clear();
        }
        else
        {
            current += ch;
        }
    }
    rv.push_back(current);

    return rv;
}
