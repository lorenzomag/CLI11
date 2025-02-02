// Copyright (c) 2017-2021, University of Cincinnati, developed by Henry Schreiner
// under NSF AWARD 1414736 and by the respective contributors.
// All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// [CLI11:public_includes:set]
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
// [CLI11:public_includes:set]

#include "App.hpp"
#include "ConfigFwd.hpp"
#include "StringTools.hpp"

namespace CLI {
// [CLI11:config_hpp:verbatim]
namespace detail {

inline std::string convert_arg_for_ini(const std::string &arg, char stringQuote = '"', char characterQuote = '\'') {
    if(arg.empty()) {
        return std::string(2, stringQuote);
    }
    // some specifically supported strings
    if(arg == "true" || arg == "false" || arg == "nan" || arg == "inf") {
        return arg;
    }
    // floating point conversion can convert some hex codes, but don't try that here
    if(arg.compare(0, 2, "0x") != 0 && arg.compare(0, 2, "0X") != 0) {
        double val;
        if(detail::lexical_cast(arg, val)) {
            return arg;
        }
    }
    // just quote a single non numeric character
    if(arg.size() == 1) {
        return std::string(1, characterQuote) + arg + characterQuote;
    }
    // handle hex, binary or octal arguments
    if(arg.front() == '0') {
        if(arg[1] == 'x') {
            if(std::all_of(arg.begin() + 2, arg.end(), [](char x) {
                   return (x >= '0' && x <= '9') || (x >= 'A' && x <= 'F') || (x >= 'a' && x <= 'f');
               })) {
                return arg;
            }
        } else if(arg[1] == 'o') {
            if(std::all_of(arg.begin() + 2, arg.end(), [](char x) { return (x >= '0' && x <= '7'); })) {
                return arg;
            }
        } else if(arg[1] == 'b') {
            if(std::all_of(arg.begin() + 2, arg.end(), [](char x) { return (x == '0' || x == '1'); })) {
                return arg;
            }
        }
    }
    if(arg.find_first_of(stringQuote) == std::string::npos) {
        return std::string(1, stringQuote) + arg + stringQuote;
    } else {
        return characterQuote + arg + characterQuote;
    }
}

/// Comma separated join, adds quotes if needed
inline std::string ini_join(const std::vector<std::string> &args,
                            char sepChar = ',',
                            char arrayStart = '[',
                            char arrayEnd = ']',
                            char stringQuote = '"',
                            char characterQuote = '\'') {
    std::string joined;
    if(args.size() > 1 && arrayStart != '\0') {
        joined.push_back(arrayStart);
    }
    std::size_t start = 0;
    for(const auto &arg : args) {
        if(start++ > 0) {
            joined.push_back(sepChar);
            if(isspace(sepChar) == 0) {
                joined.push_back(' ');
            }
        }
        joined.append(convert_arg_for_ini(arg, stringQuote, characterQuote));
    }
    if(args.size() > 1 && arrayEnd != '\0') {
        joined.push_back(arrayEnd);
    }
    return joined;
}

inline std::vector<std::string> generate_parents(const std::string &section, std::string &name, char parentSeparator) {
    std::vector<std::string> parents;
    if(detail::to_lower(section) != "default") {
        if(section.find(parentSeparator) != std::string::npos) {
            parents = detail::split(section, parentSeparator);
        } else {
            parents = {section};
        }
    }
    if(name.find(parentSeparator) != std::string::npos) {
        std::vector<std::string> plist = detail::split(name, parentSeparator);
        name = plist.back();
        detail::remove_quotes(name);
        plist.pop_back();
        parents.insert(parents.end(), plist.begin(), plist.end());
    }

    // clean up quotes on the parents
    for(auto &parent : parents) {
        detail::remove_quotes(parent);
    }
    return parents;
}

/// assuming non default segments do a check on the close and open of the segments in a configItem structure
inline void
checkParentSegments(std::vector<ConfigItem> &output, const std::string &currentSection, char parentSeparator) {

    std::string estring;
    auto parents = detail::generate_parents(currentSection, estring, parentSeparator);
    if(!output.empty() && output.back().name == "--") {
        std::size_t msize = (parents.size() > 1U) ? parents.size() : 2;
        while(output.back().parents.size() >= msize) {
            output.push_back(output.back());
            output.back().parents.pop_back();
        }

        if(parents.size() > 1) {
            std::size_t common = 0;
            std::size_t mpair = (std::min)(output.back().parents.size(), parents.size() - 1);
            for(std::size_t ii = 0; ii < mpair; ++ii) {
                if(output.back().parents[ii] != parents[ii]) {
                    break;
                }
                ++common;
            }
            if(common == mpair) {
                output.pop_back();
            } else {
                while(output.back().parents.size() > common + 1) {
                    output.push_back(output.back());
                    output.back().parents.pop_back();
                }
            }
            for(std::size_t ii = common; ii < parents.size() - 1; ++ii) {
                output.emplace_back();
                output.back().parents.assign(parents.begin(), parents.begin() + static_cast<std::ptrdiff_t>(ii) + 1);
                output.back().name = "++";
            }
        }
    } else if(parents.size() > 1) {
        for(std::size_t ii = 0; ii < parents.size() - 1; ++ii) {
            output.emplace_back();
            output.back().parents.assign(parents.begin(), parents.begin() + static_cast<std::ptrdiff_t>(ii) + 1);
            output.back().name = "++";
        }
    }

    // insert a section end which is just an empty items_buffer
    output.emplace_back();
    output.back().parents = std::move(parents);
    output.back().name = "++";
}

/// Add a single result to the result set, taking into account delimiters
inline int _split_result_str(std::string &&result, char delimiter_, std::vector<std::string> &res) {
    int result_count = 0;
    if(!result.empty() && result.front() == '[' &&
       result.back() == ']') {  // this is now a vector string likely from the default or user entry
        result.pop_back();

        for(auto &var : CLI::detail::split(result.substr(1), ',')) {
            if(!var.empty()) {
                result_count += _split_result_str(std::move(var), delimiter_, res);
            }
        }
        return result_count;
    }
    if(delimiter_ == '\0') {
        res.push_back(std::move(result));
        ++result_count;
    } else {
        if((result.find_first_of(delimiter_) != std::string::npos)) {
            for(const auto &var : CLI::detail::split(result, delimiter_)) {
                if(!var.empty()) {
                    res.push_back(var);
                    ++result_count;
                }
            }
        } else {
            res.push_back(std::move(result));
            ++result_count;
        }
    }
    return result_count;
}
template <typename T> std::vector<std::string> get_description_for_TOML(T *CLI_obj) {
    // Get description of CLI::App/CLI::Option
    // Place each line of the string in a string vector element, separating lines by '\n'
    // Return rvalue of string vector
    std::istringstream desc_stream(CLI_obj->get_description());
    std::vector<std::string> lines;
    std::string _line;
    while(getline(desc_stream, _line)) {
        // Add a trailing space for readability if whitespace is not present at beginning of line
        if(!isspace(_line.at(0)))
            _line.insert(0, 1, ' ');
        lines.push_back(_line);
    }
    return std::move(lines);
}

}  // namespace detail

inline std::vector<ConfigItem> ConfigBase::from_config(std::istream &input) const {
    std::string line;
    std::string currentSection = "default";
    std::string previousSection = "default";
    std::vector<ConfigItem> output;
    bool isDefaultArray = (arrayStart == '[' && arrayEnd == ']' && arraySeparator == ',');
    bool isINIArray = (arrayStart == '\0' || arrayStart == ' ') && arrayStart == arrayEnd;
    bool inSection{false};
    char aStart = (isINIArray) ? '[' : arrayStart;
    char aEnd = (isINIArray) ? ']' : arrayEnd;
    char aSep = (isINIArray && arraySeparator == ' ') ? ',' : arraySeparator;
    int currentSectionIndex{0};
    while(getline(input, line)) {
        std::vector<std::string> items_buffer;
        std::string name;

        detail::trim(line);
        std::size_t len = line.length();
        // lines have to be at least 3 characters to have any meaning to CLI just skip the rest
        if(len < 3) {
            continue;
        }
        if(line.front() == '[' && line.back() == ']') {
            if(currentSection != "default") {
                // insert a section end which is just an empty items_buffer
                output.emplace_back();
                output.back().parents = detail::generate_parents(currentSection, name, parentSeparatorChar);
                output.back().name = "--";
            }
            currentSection = line.substr(1, len - 2);
            // deal with double brackets for TOML
            if(currentSection.size() > 1 && currentSection.front() == '[' && currentSection.back() == ']') {
                currentSection = currentSection.substr(1, currentSection.size() - 2);
            }
            if(detail::to_lower(currentSection) == "default") {
                currentSection = "default";
            } else {
                detail::checkParentSegments(output, currentSection, parentSeparatorChar);
            }
            inSection = false;
            if(currentSection == previousSection) {
                ++currentSectionIndex;
            } else {
                currentSectionIndex = 0;
                previousSection = currentSection;
            }
            continue;
        }

        // comment lines
        if(line.front() == ';' || line.front() == '#' || line.front() == commentChar) {
            continue;
        }

        // Find = in string, split and recombine
        auto pos = line.find(valueDelimiter);
        if(pos != std::string::npos) {
            name = detail::trim_copy(line.substr(0, pos));
            std::string item = detail::trim_copy(line.substr(pos + 1));
            auto cloc = item.find(commentChar);
            if(cloc != std::string::npos) {
                item.erase(cloc, std::string::npos);
                detail::trim(item);
            }
            if(item.size() > 1 && item.front() == aStart) {
                for(std::string multiline; item.back() != aEnd && std::getline(input, multiline);) {
                    detail::trim(multiline);
                    item += multiline;
                }
                items_buffer = detail::split_up(item.substr(1, item.length() - 2), aSep);
            } else if((isDefaultArray || isINIArray) && item.find_first_of(aSep) != std::string::npos) {
                items_buffer = detail::split_up(item, aSep);
            } else if((isDefaultArray || isINIArray) && item.find_first_of(' ') != std::string::npos) {
                items_buffer = detail::split_up(item);
            } else {
                items_buffer = {item};
            }
        } else {
            name = detail::trim_copy(line);
            auto cloc = name.find(commentChar);
            if(cloc != std::string::npos) {
                name.erase(cloc, std::string::npos);
                detail::trim(name);
            }

            items_buffer = {"true"};
        }
        if(name.find(parentSeparatorChar) == std::string::npos) {
            detail::remove_quotes(name);
        }
        // clean up quotes on the items
        for(auto &it : items_buffer) {
            detail::remove_quotes(it);
        }

        std::vector<std::string> parents = detail::generate_parents(currentSection, name, parentSeparatorChar);
        if(parents.size() > maximumLayers) {
            continue;
        }
        if(!configSection.empty() && !inSection) {
            if(parents.empty() || parents.front() != configSection) {
                continue;
            }
            if(configIndex >= 0 && currentSectionIndex != configIndex) {
                continue;
            }
            parents.erase(parents.begin());
            inSection = true;
        }
        if(!output.empty() && name == output.back().name && parents == output.back().parents) {
            output.back().inputs.insert(output.back().inputs.end(), items_buffer.begin(), items_buffer.end());
        } else {
            output.emplace_back();
            output.back().parents = std::move(parents);
            output.back().name = std::move(name);
            output.back().inputs = std::move(items_buffer);
        }
    }
    if(currentSection != "default") {
        // insert a section end which is just an empty items_buffer
        std::string ename;
        output.emplace_back();
        output.back().parents = detail::generate_parents(currentSection, ename, parentSeparatorChar);
        output.back().name = "--";
        while(output.back().parents.size() > 1) {
            output.push_back(output.back());
            output.back().parents.pop_back();
        }
    }
    return output;
}

inline std::string
ConfigBase::to_config(const App *app, bool default_also, bool write_description, std::string prefix) const {
    std::stringstream out;
    std::string commentLead;
    commentLead.push_back(commentChar);
    commentLead.push_back(' ');

    std::vector<std::string> groups = app->get_groups();
    bool defaultUsed = false;
    groups.insert(groups.begin(), std::string("Options"));
    if(write_description && (app->get_configurable() || app->get_parent() == nullptr || app->get_name().empty())) {
        out << commentLead << detail::fix_newlines(commentLead, app->get_description()) << '\n';
    }
    for(auto &group : groups) {
        if(group == "Options" || group.empty()) {
            if(defaultUsed) {
                continue;
            }
            defaultUsed = true;
        }
        if(write_description && group != "Options" && !group.empty()) {
            out << '\n' << commentLead << group << " Options\n";
        }
        for(const Option *opt : app->get_options({})) {

            // Only process options that are configurable
            if(opt->get_configurable()) {
                if(opt->get_group() != group) {
                    if(!(group == "Options" && opt->get_group().empty())) {
                        continue;
                    }
                }
                std::string name = prefix + opt->get_single_name();
                std::string value = detail::ini_join(
                    opt->reduced_results(), arraySeparator, arrayStart, arrayEnd, stringQuote, characterQuote);

                if(value.empty() && default_also) {
                    if(!opt->get_default_str().empty()) {
                        value = detail::convert_arg_for_ini(opt->get_default_str(), stringQuote, characterQuote);
                    } else if(opt->get_expected_min() == 0) {
                        value = "false";
                    } else if(opt->get_run_callback_for_default()) {
                        value = "\"\"";  // empty string default value
                    }
                }

                if(!value.empty()) {
                    if(write_description && opt->has_description()) {
                        out << '\n';
                        out << commentLead << detail::fix_newlines(commentLead, opt->get_description()) << '\n';
                    }
                    out << name << valueDelimiter << value << '\n';
                }
            }
        }
    }
    auto subcommands = app->get_subcommands({});
    for(const App *subcom : subcommands) {
        if(subcom->get_name().empty()) {
            if(write_description && !subcom->get_group().empty()) {
                out << '\n' << commentLead << subcom->get_group() << " Options\n";
            }
            out << to_config(subcom, default_also, write_description, prefix);
        }
    }

    for(const App *subcom : subcommands) {
        if(!subcom->get_name().empty()) {
            if(subcom->get_configurable() && app->got_subcommand(subcom)) {
                if(!prefix.empty() || app->get_parent() == nullptr) {
                    out << '[' << prefix << subcom->get_name() << "]\n";
                } else {
                    std::string subname = app->get_name() + parentSeparatorChar + subcom->get_name();
                    auto p = app->get_parent();
                    while(p->get_parent() != nullptr) {
                        subname = p->get_name() + parentSeparatorChar + subname;
                        p = p->get_parent();
                    }
                    out << '[' << subname << "]\n";
                }
                out << to_config(subcom, default_also, write_description, "");
            } else {
                out << to_config(
                    subcom, default_also, write_description, prefix + subcom->get_name() + parentSeparatorChar);
            }
        }
    }

    return out.str();
}

// ---------------- TOML Config file ----------- BEGIN //

using toml_value = toml::basic_value<toml::preserve_comments>;

// Convert current set of command line arguments to TOML config file
template <typename T>
inline std::string ConfigTOML<T>::to_config(
    const CLI::App *app,     // Current CLI app
    bool default_also,       // Boolean: output also default values of CLI::ConfigItems
    bool write_description,  // Boolean: include descriptions of CLI::COnfigItems as comments to TOML file
    std::string prefix       // Uninitialised (needed to override)
) const {

    bool is_initialised;  // Boolean: latest parsed TOML value is initialised

    // Defined to reference in lambda definition
    std::function<toml_value(const CLI::App *, std::string)> get_values;

    // Lambda function to convert CLI items to TOML entries
    // recursivity used to consider subcommand chains
    get_values = [&get_values, &is_initialised, default_also, write_description](const CLI::App *app,
                                                                                 std::string subcom_name) {
        toml_value j;  // Base for TOML file
        is_initialised =
            false;  // Initialisation of boolean to false.
                    // If false until the end of the function, the currently analysed value is not initialised

        // Loop through all CLI options for the current CLI app
        for(const CLI::Option *opt : app->get_options({})) {
            bool missing_entry =
                false;  // Boolean: if by end of loop iteration still false, current CLI option was not utilised
            // Only process configurable options
            if((!opt->get_lnames().empty() || !opt->get_snames().empty()) && opt->get_configurable()) {
                // Get option long name (if available), otherwise, short name
                std::string name = (!opt->get_lnames().empty()) ? opt->get_lnames()[0] : opt->get_snames()[0];
                // Non-flags
                if(opt->get_type_size() != 0) {

                    // If the option was found on command line
                    if(opt->count() == 1)
                        j[name] = opt->results().at(0);
                    else if(opt->count() > 1) {
                        j[name] = opt->results();
                    }
                    // If the option has a default and is requested by optional argument
                    else if(default_also && !opt->get_default_str().empty()) {
                        std::string default_str = opt->get_default_str();
                        int n_res;
                        std::vector<std::string> default_vals;
                        n_res = detail::_split_result_str(std::move(default_str), opt->get_delimiter(), default_vals);
                        if(default_vals.size() == 1)
                            j[name] = default_vals[0];
                        else
                            j[name] = default_vals;

                    } else if(default_also)
                        // Leave empty if default is required, but no default value was found
                        j[name] = "";
                    else
                        // Default not required, missing entry
                        missing_entry = true;

                    // Flag, one passed
                } else if(opt->count() == 1) {
                    j[name] = opt->results();

                    // Flag, multiple passed
                } else if(opt->count() > 1) {
                    j[name] = opt->count();

                    // Flag, not present
                } else if(opt->count() == 0) {
                    if(default_also)
                        j[name] = opt->get_default_str();
                    else
                        j[name] = false;
                } else
                    missing_entry = true;

                if(write_description && !missing_entry) {
                    // Write description if entry not missing
                    std::vector<std::string> comment = detail::get_description_for_TOML(opt);
                    j[name].comments() = comment;
                }
            }
        }
        // Run recursively through subcommands of CLI app
        for(const CLI::App *subcom : app->get_subcommands({})) {
            toml_value _temp_toml = get_values(subcom, subcom->get_name());
            if(is_initialised)
                j[subcom->get_name()] = _temp_toml;
        }

        if(!j.is_uninitialized())
            // Check that j is initialised
            is_initialised = true;

        if(write_description) {
            // Write description for main app
            std::vector<std::string> comment = detail::get_description_for_TOML(app);
            j.comments() = comment;
        }

        // Return TOML value
        return j;
    };

    // Get values and comments for main app, and recuvsively for subcommands
    toml_value config_toml = get_values(app, "");

    // Cast toml file into stringstream to return string
    std::stringstream config_stream;

    try {
        // If any TOML value is config_toml is uninitialised, this will fail. Caught by exception
        config_stream << config_toml;

    } catch(const std::exception &e) {
        std::string error_msg = "No configuration present to save to TOML file.\n"
                                "Try either running with default_also==TRUE\n"
                                "or with some command line arguments.\n"
                                "TOML configuration file will be empty.";
        throw CLI::ParseError(error_msg, CLI::ExitCodes::ConversionError);
    }

    // Cast stringstream to string
    std::string _temp, config_string;
    while(getline(config_stream, _temp)) {
        config_string.append(_temp);
        config_string.push_back('\n');
    }

    // Return TOML config file as string
    return config_string;
}
template <typename T>
template <typename toml_type>
toml_type ConfigTOML<T>::dump_toml_value(const CLI::Option &opt) const {

};

// Convert TOML config file to current set of Command Line Arguments (overridden by user input command line args)
template <typename T> inline std::vector<CLI::ConfigItem> ConfigTOML<T>::from_config(std::istream &input) const {
    // Use TOML11 parser to parse TOML configuration file and store it in a toml::basic_value instance
    toml_value config_file = toml::parse(input);

    // Convert toml_value to std::vector<CLI::ConfigItem>
    return _from_config(config_file);
}

using time_point = std::chrono::system_clock::time_point;

// Convert toml_value to std::vector<CLI::ConfigItem>
template <typename T>
inline std::vector<CLI::ConfigItem>
ConfigTOML<T>::_from_config(toml_value j, std::string name, std::vector<std::string> prefix) const {

    // Vector to return
    std::vector<CLI::ConfigItem> results;

    // Cast toml_value to table (Whole TOML config is a TOML table)
    // This function will be recursively used on subtables of j
    auto table = j.as_table();

    // Loop through entries of table
    for(auto element : table) {
        auto key = element.first;     // grab key of key-value pari
        auto value = element.second;  // grab value of key-value pari

        if(value.is_uninitialized()) {
            continue;
        } else if(value.is_table()) {
            // if value is a table, recursively apply _from_config() to it, appending key to list of parent CLI commands
            prefix.push_back(key);
            auto sub_results = _from_config(value, key, prefix);
            results.insert(results.end(), sub_results.begin(), sub_results.end());
            prefix.pop_back();
        } else {
            results.emplace_back();      // Create instance of CLI::ConfigItem
            auto &res = results.back();  // Get reference to such instance
            res.name = key;              // Assign name to instance
            res.parents = prefix;        // Assign list of parents to instance

            std::stringstream ss;  // Declare stringstream to use in switch statement

            // Determine type of toml value and convert to string
            switch(value.type()) {
            case toml::value_t::boolean: {
                auto cast_value = toml::get<bool>(value);
                res.inputs = {std::to_string(cast_value)};
                break;
            }
            case toml::value_t::string: {
                auto cast_value = toml::get<std::string>(value);
                res.inputs = {cast_value};
                break;
            }
            case toml::value_t::integer: {
                auto cast_value = toml::get<int>(value);
                res.inputs = {std::to_string(cast_value)};
                break;
            }
            case toml::value_t::floating: {
                auto cast_value = toml::get<double>(value);
                res.inputs = {std::to_string(cast_value)};
                break;
            }
            case toml::value_t::local_datetime: 
            case toml::value_t::local_date: 
            case toml::value_t::local_time: 
            case toml::value_t::offset_datetime: {
                ss << value;
                res.inputs = {ss.str()};
                break;
            }
            case toml::value_t::array:
                res.inputs = parse_toml_array(value.as_array(), key);
                break;

            default:
                std::stringstream ss_error;
                ss_error << "Could not convert the key-value pair \"" << key << "\" from any known TOML type.";
                throw CLI::ParseError(ss_error.str(), CLI::ExitCodes::ConversionError);
                break;
            }
        }
    }

    return results;
}

template <typename T>
inline std::vector<std::string> ConfigTOML<T>::parse_toml_array(toml::value array, const std::string &key) const {
    std::vector<std::string> array_str;
    for(auto value : array.as_array()) {
        if(value.is_table()) {
            std::stringstream ss_error;
            ss_error << "TOML arrays of tables are not supported for conversion to ConfigItem";
            throw CLI::ParseError(ss_error.str(), CLI::ExitCodes::ConversionError);
        } else {
            std::stringstream ss;  // Declare stringstream to use in switch statement

            // Determine type of toml value and convert to string
            switch(value.type()) {
             case toml::value_t::boolean: {
                auto cast_value = toml::get<bool>(value);
                array_str.push_back(std::to_string(cast_value));
                break;
            }
            case toml::value_t::string: {
                auto cast_value = toml::get<std::string>(value);
                array_str.push_back(cast_value);
                break;
            }
            case toml::value_t::integer: {
                auto cast_value = toml::get<int>(value);
                array_str.push_back(std::to_string(cast_value));
                break;
            }
            case toml::value_t::floating: {
                auto cast_value = toml::get<double>(value);
                array_str.push_back(std::to_string(cast_value));
                break;
            }
            case toml::value_t::local_datetime: 
            case toml::value_t::local_date: 
            case toml::value_t::local_time: 
            case toml::value_t::offset_datetime: {
                ss << value;
                array_str.push_back(ss.str());
                break;
            }
            case toml::value_t::array: {
                std::vector<std::string> res_vector = parse_toml_array(value.as_array(), key);
                array_str.insert(array_str.end(), res_vector.begin(), res_vector.end());
                break;
            }

            default:
                std::stringstream ss_error;
                ss_error << "Could not convert an element of the array \"" << key << "\" from any known TOML type.";
                throw CLI::ParseError(ss_error.str(), CLI::ExitCodes::ConversionError);
                break;
            }
        }
    }

    return array_str;
}
// ---------------- TOML Config file ----------- END //

}  // namespace CLI
