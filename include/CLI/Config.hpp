// Copyright (c) 2017-2021, University of Cincinnati, developed by Henry Schreiner
// under NSF AWARD 1414736 and by the respective contributors.
// All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// #pragma once

// [CLI11:public_includes:set]
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
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

// ---------------- JSON Config file ----------- BEGIN //
using nlohmann::json;
inline std::string ConfigJSON::to_config(const CLI::App *app, bool default_also, bool, std::string) const {

    json j;

    for(const CLI::Option *opt : app->get_options({})) {

        // Only process option with a long-name and configurable
        if(!opt->get_lnames().empty() && opt->get_configurable()) {
            std::string name = opt->get_lnames()[0];

            // Non-flags
            if(opt->get_type_size() != 0) {

                // If the option was found on command line
                if(opt->count() == 1)
                    j[name] = opt->results().at(0);
                else if(opt->count() > 1)
                    j[name] = opt->results();

                // If the option has a default and is requested by optional argument
                else if(default_also && !opt->get_default_str().empty())
                    j[name] = opt->get_default_str();

                // Flag, one passed
            } else if(opt->count() == 1) {
                j[name] = true;

                // Flag, multiple passed
            } else if(opt->count() > 1) {
                j[name] = opt->count();

                // Flag, not present
            } else if(opt->count() == 0 && default_also) {
                j[name] = false;
            }
        }
    }

    for(const CLI::App *subcom : app->get_subcommands({}))
        j[subcom->get_name()] = json(to_config(subcom, default_also, false, ""));

    return j.dump(4);
}

inline std::vector<CLI::ConfigItem> ConfigJSON::from_config(std::istream &input) const {
    json j;
    input >> j;
    return _from_config(j);
}

inline std::vector<CLI::ConfigItem>
ConfigJSON::_from_config(json j, std::string name, std::vector<std::string> prefix) const {
    std::vector<CLI::ConfigItem> results;

    if(j.is_object()) {
        for(json::iterator item = j.begin(); item != j.end(); ++item) {
            auto copy_prefix = prefix;
            if(!name.empty())
                copy_prefix.push_back(name);
            auto sub_results = _from_config(*item, item.key(), copy_prefix);
            results.insert(results.end(), sub_results.begin(), sub_results.end());
        }
    } else if(!name.empty()) {
        results.emplace_back();
        CLI::ConfigItem &res = results.back();
        res.name = name;
        res.parents = prefix;
        if(j.is_boolean()) {
            res.inputs = {j.get<bool>() ? "true" : "false"};
        } else if(j.is_number()) {
            std::stringstream ss;
            ss << j.get<double>();
            res.inputs = {ss.str()};
        } else if(j.is_string()) {
            res.inputs = {j.get<std::string>()};
        } else if(j.is_array()) {
            for(std::string ival : j)
                res.inputs.push_back(ival);
        } else {
            throw CLI::ConversionError("Failed to convert " + name);
        }
    } else {
        throw CLI::ConversionError("You must make all top level values objects in json!");
    }

    return results;
}

// ---------------- JSON Config file ----------- END //
//
//
// ---------------- TOML Config file ----------- BEGIN //

using namespace toml::literals::toml_literals;

inline std::string
ConfigTOML::to_config(const CLI::App *app, bool default_also, bool write_description, std::string prefix) const {
    bool is_initialised;

    std::function<toml::basic_value<toml::preserve_comments>(const CLI::App *, std::string)> get_values;

    get_values = [&get_values, &is_initialised, default_also, write_description](const CLI::App *app,
                                                                                 std::string subcom_name = "") {
        toml::basic_value<toml::preserve_comments> j;
        is_initialised = false;
        for(const CLI::Option *opt : app->get_options({})) {
            bool missing_entry = false;
            // Only process configurable options
            if((!opt->get_lnames().empty() || !opt->get_snames().empty()) && opt->get_configurable()) {
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
                        j[name] = "";
                    else
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
                    std::vector<std::string> comment = detail::get_description_for_TOML(opt);
                    j[name].comments() = comment;
                }
            }
        }

        for(const CLI::App *subcom : app->get_subcommands({})) {
            toml::basic_value<toml::preserve_comments> _temp_toml = get_values(subcom, subcom->get_name());
            if(is_initialised)
                j[subcom->get_name()] = _temp_toml;
        }

        if(!j.is_uninitialized())
            is_initialised = true;

        if(write_description) {
            std::vector<std::string> comment = detail::get_description_for_TOML(app);
            j.comments() = comment;
        }
        return j;
    };

    // Get values and comments for main app
    toml::basic_value<toml::preserve_comments> config_toml = get_values(app, "");

    std::stringstream config_stream;
    try {
        config_stream << config_toml;

    } catch(const std::exception &e) {
        std::cerr << "[WARNING] (Internal to toml11) " << e.what() << "\n\n"
                  << "[WARNING] No configuration present to save to TOML file.\n"
                  << "[WARNING] Try either running with default_also==TRUE\n"
                  << "[WARNING]  orwith some command line arguments.\n"
                  << "[WARNING] TOML configuration file will be empty.\n"
                  << std::endl;
    }

    std::string _temp, config_string;
    while(getline(config_stream, _temp)) {
        config_string.append(_temp);
        config_string.push_back('\n');
    }

    return config_string;
}

inline std::vector<CLI::ConfigItem> ConfigTOML::from_config(std::istream &input) const {
    toml::basic_value<toml::preserve_comments> config_file = toml::parse(input);
    return _from_config(config_file);
}

inline std::vector<CLI::ConfigItem> ConfigTOML::_from_config(toml::basic_value<toml::preserve_comments> j,
                                                             std::string name,
                                                             std::vector<std::string> prefix) const {
    std::vector<CLI::ConfigItem> results;

    auto table = j.as_table();

    for(auto element : table) {
        auto key = element.first;
        auto value = element.second;

        if(value.is_uninitialized()) {
            continue;
        } else if(value.is_table()) {
            prefix.push_back(key);
            auto sub_results = _from_config(value, key, prefix);
            results.insert(results.end(), sub_results.begin(), sub_results.end());
            prefix.pop_back();
        } else {
            results.emplace_back();
            auto &res = results.back();
            res.name = key;
            res.parents = prefix;

            switch(value.type()) {

            case toml::value_t::boolean:
                res.inputs = {value.as_boolean() ? "true" : "false"};
                break;
            case toml::value_t::string:
                res.inputs = {value.as_string()};
                break;
            case toml::value_t::integer:
                res.inputs = {std::to_string(value.as_integer())};
                break;
            case toml::value_t::floating:
                res.inputs = {std::to_string(value.as_floating())};
                break;
            case toml::value_t::offset_datetime:
                // implement
                std::cerr << "Conversion from offset datetime to be implemented.\n";
                break;
            case toml::value_t::local_datetime:
                // implement
                std::cerr << "Conversion from local datetime to be implemented.\n";
                break;
            case toml::value_t::local_date:
                // implement
                std::cerr << "Conversion from local date to be implemented.\n";
                break;
            case toml::value_t::local_time:
                // implement
                std::cerr << "Conversion from local time to be implemented.\n";
                break;
            case toml::value_t::array:
                for(auto ival : value.as_array())
                    res.inputs.push_back(ival.as_string());
                break;

            default:
                break;
            }
        }
    }

    return results;
}

// ---------------- TOML Config file ----------- END //

}  // namespace CLI
