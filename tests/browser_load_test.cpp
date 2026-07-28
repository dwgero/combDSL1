/*
 * C++ combinator DSL
 * Copyright (C) 2026  David W. Gero
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <combdsl/combinators.hpp>
#include <web/load_set_list.hpp>

#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::size_t tests_run = 0;
std::size_t test_failures = 0;

void check(std::string_view title, bool condition) {
    ++tests_run;
    if (condition) {
        return;
    }
    ++test_failures;
    std::cerr << "FAILED:   " << title << '\n';
}

[[nodiscard]] bool defined_name(std::string_view name) {
    std::string command = "show ";
    command += name;
    try {
        static_cast<void>(combdsl::parse(command));
        return true;
    } catch (combdsl::parse_error const&) {
        return false;
    }
}

[[nodiscard]] std::string shown_definition(std::string_view name) {
    std::string command = "show ";
    command += name;
    std::ostringstream output;
    combdsl::parse(command).print_to(output);
    return output.str();
}

[[nodiscard]] std::string repeated_error_lines(
    std::size_t count) {
    std::string result;
    for (std::size_t index = 0; index < count; ++index) {
        result += "@\n";
    }
    return result;
}

} // namespace

int main() {
    using combdsl::web_detail::format_file_load_diagnostics;
    using combdsl::web_detail::load_set_list;

    auto const continued = load_set_list(
        "set FileBefore = 0 I\n"
        "@\n"
        "set FileAfter = 0 K\n"
        "#\n");
    check("errors make a file load unsuccessful",
          !continued.success);
    check("two parse errors do not abort loading",
          !continued.aborted);
    check("an erroneous file commits no definitions",
          continued.loaded == 0);
    check("loading resumes after the first parse error",
          continued.diagnostics.size() == 2 &&
          continued.diagnostics[0].line == 2 &&
          continued.diagnostics[1].line == 4);
    check("file parse positions remain zero-based internally",
          continued.diagnostics.size() == 2 &&
          continued.diagnostics[0].position == 0 &&
          continued.diagnostics[1].position == 0);
    check("file parse details are retained",
          continued.diagnostics.size() == 2 &&
          continued.diagnostics[0].detail == "unknown operand" &&
          continued.diagnostics[1].detail == "unknown operand");
    check("continued errors use the requested display format",
          format_file_load_diagnostics(
              "broken.cmb", continued) ==
          "Parse error in file broken.cmb on line 2 at position 1: "
          "unknown operand\n"
          "Parse error in file broken.cmb on line 4 at position 1: "
          "unknown operand\n"
          "Errors are preventing any changes from being made");
    check("a failed load rolls back an earlier valid definition",
          !defined_name("FileBefore"));
    check("a failed load rolls back a later valid definition",
          !defined_name("FileAfter"));

    auto fourteen_source = repeated_error_lines(14);
    fourteen_source += "set FourteenEnd = 0 I\n";
    auto const fourteen = load_set_list(fourteen_source);
    check("fourteen errors do not trigger the cutoff",
          !fourteen.success &&
          !fourteen.aborted &&
          fourteen.diagnostics.size() == 14);
    check("fourteen errors roll back a following valid definition",
          !defined_name("FourteenEnd"));
    check("fourteen errors omit the abort message",
          format_file_load_diagnostics(
              "fourteen.cmb", fourteen)
              .find("Too many errors, aborting with no changes made") ==
          std::string::npos);
    check("parse errors explain why definitions are not retained",
          format_file_load_diagnostics(
              "fourteen.cmb", fourteen)
              .ends_with(
                  "\nErrors are preventing any changes from being made"));

    auto fifteen_source = repeated_error_lines(15);
    fifteen_source += "#\n";
    auto const fifteen = load_set_list(fifteen_source);
    auto const fifteen_messages =
        format_file_load_diagnostics(
            "fifteen.cmb", fifteen);
    check("the fifteenth error triggers the cutoff",
          !fifteen.success &&
          fifteen.aborted &&
          fifteen.diagnostics.size() == 15);
    check("the loader stops before a sixteenth error",
          !fifteen.diagnostics.empty() &&
          fifteen.diagnostics.back().line == 15);
    check("the cutoff displays only its final status message",
          fifteen_messages.ends_with(
              "\nToo many errors, aborting with no changes made"));
    check("the cutoff omits the ordinary rollback message",
          fifteen_messages.find(
              "Errors are preventing any changes from being made") ==
          std::string::npos);

    combdsl::web_detail::set_list_load_result const fatal{
        false, false, 0, {}, 3, "unexpected loading error"};
    check("fatal load errors explain why definitions are not retained",
          format_file_load_diagnostics("fatal.cmb", fatal) ==
          "Error while loading file fatal.cmb on line 3: "
          "unexpected loading error\n"
          "Errors are preventing any changes from being made");

    combdsl::web_detail::set_list_load_result const fatal_without_line{
        false, false, 0, {}, 0, "setup loading error"};
    check("fatal load errors omit an unavailable line number",
          format_file_load_diagnostics(
              "fatal.cmb", fatal_without_line) ==
          "Error while loading file fatal.cmb: setup loading error\n"
          "Errors are preventing any changes from being made");

    auto const successful = load_set_list(
        "set FileGood = 0 I\n"
        "set FileUse = 1 FileGood\n");
    check("an error-free file load succeeds",
          successful.success &&
          !successful.aborted &&
          successful.loaded == 2 &&
          successful.diagnostics.empty());
    check("an error-free load commits its definitions",
          defined_name("FileGood") &&
          defined_name("FileUse"));
    check("an error-free load has no diagnostic messages",
          format_file_load_diagnostics(
              "successful.cmb", successful).empty());

    static_cast<void>(
        combdsl::parse("set FileReplace = 0 I"));
    auto const replacement = load_set_list(
        "set FileReplace = 1 K\n");
    check("a file silently replaces a user definition",
          replacement.success &&
          replacement.loaded == 1 &&
          replacement.diagnostics.empty() &&
          shown_definition("FileReplace") == "arity:1 K");

    auto const before_failed_replacement = combdsl::set_list();
    auto const failed_replacement = load_set_list(
        "set FileReplace = 2 I\n"
        "@\n");
    check("an error rolls back a file replacement",
          !failed_replacement.success &&
          shown_definition("FileReplace") == "arity:1 K" &&
          combdsl::set_list() == before_failed_replacement);

    auto const repeated_replacement = load_set_list(
        "set FileTwice = 0 I\n"
        "set FileTwice = 0 K\n");
    check("redefinitions within a file are silent and ordered",
          repeated_replacement.success &&
          repeated_replacement.loaded == 2 &&
          shown_definition("FileTwice") == "arity:0 K");

    static_cast<void>(
        combdsl::parse("set FileCycle = 0 K"));
    auto const before_replayed_history = combdsl::set_list();
    auto const replayed_history = load_set_list(
        "set FileCycle = 0 I\n"
        "set FileCycle = 0 K\n");
    check("a load ending at the current definitions succeeds",
          replayed_history.success &&
          replayed_history.loaded == 2 &&
          shown_definition("FileCycle") == "arity:0 K");
    check("a load ending at the current definitions is idempotent",
          combdsl::set_list() == before_replayed_history);

    auto const before_predefined_error = combdsl::set_list();
    auto const predefined_error = load_set_list(
        "set M = 0 I\n");
    check("a file cannot replace a predefined basis",
          !predefined_error.success &&
          predefined_error.diagnostics.size() == 1 &&
          predefined_error.diagnostics.front().line == 1 &&
          predefined_error.diagnostics.front().detail ==
              "M is a pre-defined basis and cannot be redefined" &&
          shown_definition("M") == "arity:1 SII" &&
          combdsl::set_list() == before_predefined_error);

    std::cout << tests_run << " test(s) run, "
              << test_failures << " failed\n";
    return test_failures == 0 ? 0 : 1;
}
