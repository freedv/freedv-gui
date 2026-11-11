#ifndef ADI_PARSER_H
#define ADI_PARSER_H

#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>

// Reads a one-record ADIF (ADI) file the way a logging program would: each
// field's value is exactly the number of bytes its length says, which is
// what catches a wrong length. Field names are case-insensitive in ADIF, so
// they're returned in lower case.
struct AdiFile
{
    std::map<std::string, std::string> header;
    std::map<std::string, std::string> record;
    std::string error; // empty if the file was read successfully
};

inline AdiFile ParseAdi(std::string const& text)
{
    AdiFile file;
    bool inHeader = true;
    bool sawEor = false;
    size_t pos = 0;

    // ADIF: a file with a header starts with something other than '<'.
    if (text.empty() || text[0] == '<')
    {
        file.error = "no header";
        return file;
    }

    while ((pos = text.find('<', pos)) != std::string::npos)
    {
        size_t end = text.find('>', pos);
        if (end == std::string::npos)
        {
            file.error = "unterminated tag at " + std::to_string(pos);
            return file;
        }

        std::string tag = text.substr(pos + 1, end - pos - 1);
        for (auto& c : tag) c = (char)std::tolower((unsigned char)c);
        pos = end + 1;

        if (tag == "eoh")
        {
            inHeader = false;
            continue;
        }
        if (tag == "eor")
        {
            if (inHeader)
            {
                file.error = "<EOR> before <EOH>";
                return file;
            }
            sawEor = true;
            if (pos != text.size())
            {
                file.error = "text after <EOR>";
                return file;
            }
            break;
        }

        // name:length[:type]
        size_t colon = tag.find(':');
        if (colon == std::string::npos)
        {
            file.error = "tag without a length: " + tag;
            return file;
        }
        std::string name = tag.substr(0, colon);
        std::string lengthStr = tag.substr(colon + 1);
        lengthStr = lengthStr.substr(0, lengthStr.find(':'));
        char* lengthEnd = nullptr;
        unsigned long length = strtoul(lengthStr.c_str(), &lengthEnd, 10);
        if (lengthStr.empty() || *lengthEnd != '\0')
        {
            file.error = "bad length for " + name;
            return file;
        }
        if (pos + length > text.size())
        {
            file.error = "value of " + name + " runs off the end";
            return file;
        }

        auto& fields = inHeader ? file.header : file.record;
        if (fields.count(name) != 0)
        {
            file.error = "field repeated: " + name;
            return file;
        }
        fields[name] = text.substr(pos, length);
        pos += length;

        // Only whitespace may separate a value from the next tag. A wrong
        // length leaves part of a value (or of the next tag) here instead.
        size_t next = text.find('<', pos);
        size_t gapEnd = next == std::string::npos ? text.size() : next;
        for (size_t i = pos; i < gapEnd; i++)
        {
            if (!std::isspace((unsigned char)text[i]))
            {
                file.error = "unexpected text after " + name + " (wrong length?)";
                return file;
            }
        }
    }

    if (!sawEor)
    {
        file.error = "no <EOR>";
    }
    return file;
}

#endif // ADI_PARSER_H
