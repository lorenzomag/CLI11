// Copyright (c) 2017-2021, University of Cincinnati, developed by Henry Schreiner
// under NSF AWARD 1414736 and by the respective contributors.
// All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// [CLI11:public_includes:set]
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
// [CLI11:public_includes:end]

#include "Error.hpp"
#include "StringTools.hpp"

#include <toml11/toml.hpp>

namespace CLI {
// [CLI11:config_fwd_hpp:verbatim]

class App;

/// Holds values to load into Options
struct ConfigItem {
    /// This is the list of parents
    std::vector<std::string> parents{};

    /// This is the name
    std::string name{};

    /// Listing of inputs
    std::vector<std::string> inputs{};

    /// The list of parents and name joined by "."
    std::string fullname() const {
        std::vector<std::string> tmp = parents;
        tmp.emplace_back(name);
        return detail::join(tmp, ".");
    }
};

/// This class provides a converter for configuration files.
class Config {
  protected:
    std::vector<ConfigItem> items{};

  public:
    /// Convert an app into a configuration
    virtual std::string to_config(const App *, bool, bool, std::string) const = 0;

    /// Convert a configuration into an app
    virtual std::vector<ConfigItem> from_config(std::istream &) const = 0;

    /// Get a flag value
    virtual std::string to_flag(const ConfigItem &item) const {
        if(item.inputs.size() == 1) {
            return item.inputs.at(0);
        }
        throw ConversionError::TooManyInputsFlag(item.fullname());
    }

    /// Parse a config file, throw an error (ParseError:ConfigParseError or FileError) on failure
    std::vector<ConfigItem> from_file(const std::string &name) {
        std::ifstream input{name};
        if(!input.good())
            throw FileError::Missing(name);

        return from_config(input);
    }

    /// Virtual destructor
    virtual ~Config() = default;
};

/// This converter works with INI/TOML files; to write INI files use ConfigINI
class ConfigBase : public Config {
  protected:
    /// the character used for comments
    char commentChar = '#';
    /// the character used to start an array '\0' is a default to not use
    char arrayStart = '[';
    /// the character used to end an array '\0' is a default to not use
    char arrayEnd = ']';
    /// the character used to separate elements in an array
    char arraySeparator = ',';
    /// the character used separate the name from the value
    char valueDelimiter = '=';
    /// the character to use around strings
    char stringQuote = '"';
    /// the character to use around single characters
    char characterQuote = '\'';
    /// the maximum number of layers to allow
    uint8_t maximumLayers{255};
    /// the separator used to separator parent layers
    char parentSeparatorChar{'.'};
    /// Specify the configuration index to use for arrayed sections
    int16_t configIndex{-1};
    /// Specify the configuration section that should be used
    std::string configSection{};

  public:
    std::string
    to_config(const App * /*app*/, bool default_also, bool write_description, std::string prefix) const override;

    std::vector<ConfigItem> from_config(std::istream &input) const override;
    /// Specify the configuration for comment characters
    ConfigBase *comment(char cchar) {
        commentChar = cchar;
        return this;
    }
    /// Specify the start and end characters for an array
    ConfigBase *arrayBounds(char aStart, char aEnd) {
        arrayStart = aStart;
        arrayEnd = aEnd;
        return this;
    }
    /// Specify the delimiter character for an array
    ConfigBase *arrayDelimiter(char aSep) {
        arraySeparator = aSep;
        return this;
    }
    /// Specify the delimiter between a name and value
    ConfigBase *valueSeparator(char vSep) {
        valueDelimiter = vSep;
        return this;
    }
    /// Specify the quote characters used around strings and characters
    ConfigBase *quoteCharacter(char qString, char qChar) {
        stringQuote = qString;
        characterQuote = qChar;
        return this;
    }
    /// Specify the maximum number of parents
    ConfigBase *maxLayers(uint8_t layers) {
        maximumLayers = layers;
        return this;
    }
    /// Specify the separator to use for parent layers
    ConfigBase *parentSeparator(char sep) {
        parentSeparatorChar = sep;
        return this;
    }
    /// get a reference to the configuration section
    std::string &sectionRef() { return configSection; }
    /// get the section
    const std::string &section() const { return configSection; }
    /// specify a particular section of the configuration file to use
    ConfigBase *section(const std::string &sectionName) {
        configSection = sectionName;
        return this;
    }

    /// get a reference to the configuration index
    int16_t &indexRef() { return configIndex; }
    /// get the section index
    int16_t index() const { return configIndex; }
    /// specify a particular index in the section to use (-1) for all sections to use
    ConfigBase *index(int16_t sectionIndex) {
        configIndex = sectionIndex;
        return this;
    }
};

/// ConfigINI generates a "standard" INI compliant output
class ConfigINI : public ConfigBase {

  public:
    ConfigINI() {
        commentChar = ';';
        arrayStart = '\0';
        arrayEnd = '\0';
        arraySeparator = ' ';
        valueDelimiter = '=';
    }
};


/// ConfigTOML_CustomTime generates a TOML v1.0 compliant output
// Based on git@github.com:ToruNiina/toml11.git
//
// The template typename T corresponds to the unit of std::chrono::duration, used for TOML's local time entries
// By default (see ConfigTOML alias below), T is taken to be std::chrono::seconds
template <typename T> class ConfigTOML_CustomTime : public Config {
  private:
#define DEFAULT_TIME_FORMAT "%Y-%m-%d %H:%M:%S %Z"
    std::string time_format = DEFAULT_TIME_FORMAT;  // Default format used to convert offset datetime, local datetime,
                                                    // and local date TOML entries to string
    bool use_local_timezone = true;  // Boolean: convert offset datetime, local datetime, and local date TOML entries to
                                     // local timezone std::chrono::time_points Otherwise: convert to UTC time
    T time_unit;                     // Unit of time for TOML local time entries

  public:
    //   Constructors
    ConfigTOML_CustomTime(){};  // Default Constructor

    // Constructors allowing user to set time_format string
    ConfigTOML_CustomTime(const std::string &time_format) : time_format(time_format){};
    ConfigTOML_CustomTime(const std::string &&time_format) : time_format(std::move(time_format)){};

    // Constructors allowing user to set use_local_timezone boolean and (if desired) time_format string (defaults to
    // DEFAULT_TIME_FORMAT)
    ConfigTOML_CustomTime(const bool &use_local_timezone, const std::string &time_format = DEFAULT_TIME_FORMAT)
        : use_local_timezone(use_local_timezone), time_format(time_format){};
    ConfigTOML_CustomTime(const bool &use_local_timezone, const std::string &&time_format = DEFAULT_TIME_FORMAT)
        : use_local_timezone(use_local_timezone), time_format(std::move(time_format)){};

    // Convert current set of command line arguments to TOML config file
    std::string to_config(const App *app, bool default_also, bool write_description, std::string prefix) const override;

    // Convert TOML config file to current set of Command Line Arguments (overridden by user input command line args)
    std::vector<ConfigItem> from_config(std::istream &input) const override;

  private:
    // Used internally by from_config
    std::vector<ConfigItem> _from_config(toml::basic_value<toml::preserve_comments> j,
                                         std::string name = "",
                                         std::vector<std::string> prefix = {}) const;
};


// Set default time unit for TOML local time entries to std::chrono::seconds
using ConfigTOML = ConfigTOML_CustomTime<std::chrono::seconds>;

// [CLI11:config_fwd_hpp:end]
}  // namespace CLI
