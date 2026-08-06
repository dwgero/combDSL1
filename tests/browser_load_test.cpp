/*
 * C++ Combinator DSL
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

[[nodiscard]] std::string stepped_expression(std::string_view source) {
    std::ostringstream output;
    combdsl::single_step(combdsl::parse(source)).print_to(output);
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

    auto const first_snapshot = load_set_list(
        "snapshot on\n"
        "snapshot off\n");
    check("leading snapshot commands compact to the last one",
          first_snapshot.success &&
          first_snapshot.loaded == 2 &&
          combdsl::set_list() == "snapshot off");

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
    check("the compacted leading snapshot precedes definitions",
          combdsl::set_list().starts_with(
              "snapshot off\nset FileGood = 0 I"));
    check("an error-free load has no diagnostic messages",
          format_file_load_diagnostics(
              "successful.cmb", successful).empty());

    auto const unreferenced_removal = load_set_list(
        "set FileGone = 0 I\n"
        "remove FileGone\n");
    check("a file can remove an unreferenced definition",
          unreferenced_removal.success &&
          unreferenced_removal.loaded == 2 &&
          !defined_name("FileGone") &&
          combdsl::set_list().ends_with(
              "set FileGone = 0 I\nremove FileGone"));

    auto const referred_removal = load_set_list(
        "set FileBase = 0 I\n"
        "set FileHolder = 0 FileBase\n"
        "remove FileBase\n");
    check("a file can remove a referred-to definition",
          referred_removal.success &&
          referred_removal.loaded == 3 &&
          !defined_name("FileBase") &&
          defined_name("FileHolder"));
    check("a referred-to removal remains replayable",
          combdsl::set_list().ends_with(
              "set FileBase = 0 I\n"
              "set FileHolder = 0 FileBase\n"
              "remove FileBase"));

    static_cast<void>(
        combdsl::parse("set FileRollback = 0 I"));
    auto const before_failed_removal = combdsl::set_list();
    auto const failed_removal = load_set_list(
        "remove FileRollback\n"
        "@\n");
    check("an error rolls back a file removal",
          !failed_removal.success &&
          defined_name("FileRollback") &&
          combdsl::set_list() == before_failed_removal);

    static_cast<void>(
        combdsl::parse("set FileReplace = 0 I"));
    auto const replacement = load_set_list(
        "set FileReplace = 1 K\n");
    auto const replacement_list = combdsl::set_list();
    check("a file silently replaces a user definition",
          replacement.success &&
          replacement.loaded == 1 &&
          replacement.diagnostics.empty() &&
          shown_definition("FileReplace") == "arity:1 K" &&
          replacement_list.find("set FileReplace = 0 I") !=
              std::string::npos &&
          replacement_list.find("set FileReplace = 1 K") !=
              std::string::npos);

    auto const before_failed_replacement = combdsl::set_list();
    auto const failed_replacement = load_set_list(
        "set FileReplace = 2 I\n"
        "@\n");
    check("an error rolls back a file replacement",
          !failed_replacement.success &&
          shown_definition("FileReplace") == "arity:1 K" &&
          !defined_name("FileReplace@3") &&
          combdsl::set_list() == before_failed_replacement);

    auto const repeated_replacement = load_set_list(
        "set FileTwice = 0 I\n"
        "set FileTwice = 0 K\n");
    auto const repeated_replacement_list = combdsl::set_list();
    check("redefinitions within a file preserve every revision",
          repeated_replacement.success &&
          repeated_replacement.loaded == 2 &&
          shown_definition("FileTwice") == "arity:0 K" &&
          repeated_replacement_list.find("set FileTwice = 0 I") !=
              std::string::npos &&
          repeated_replacement_list.find("set FileTwice = 0 K") !=
              std::string::npos);

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
    check("a load ending at the current definitions keeps its revisions",
          combdsl::set_list() == before_replayed_history +
              "\nset FileCycle = 0 I\nset FileCycle = 0 K" &&
          shown_definition("FileCycle@1") == "arity:0 K" &&
          shown_definition("FileCycle@2") == "arity:0 I" &&
          shown_definition("FileCycle@3") == "arity:0 K");

    auto const snapshot_only_off = load_set_list("snapshot off\n");
    check("a snapshot-only file persists its mode and history",
          snapshot_only_off.success &&
          snapshot_only_off.loaded == 1 &&
          !combdsl::detail::registered_parser_lookup_snapshot()
               .snapshot_enabled &&
          combdsl::set_list().ends_with("snapshot off"));

    auto const before_failed_snapshot = combdsl::set_list();
    auto const failed_snapshot = load_set_list(
        "snapshot on\n"
        "@\n");
    check("a failed load rolls back snapshot mode and history",
          !failed_snapshot.success &&
          !combdsl::detail::registered_parser_lookup_snapshot()
               .snapshot_enabled &&
          combdsl::set_list() == before_failed_snapshot);

    static_cast<void>(
        combdsl::parse("set FileLiveTarget = 1 I"));
    static_cast<void>(
        combdsl::parse("set FileLiveUse = 1 FileLiveTarget"));
    auto const before_failed_live_change = combdsl::set_list();
    auto const failed_live_change = load_set_list(
        "set FileLiveTarget = 1 K\n"
        "@\n");
    check("a failed load restores a shared live binding target",
          !failed_live_change.success &&
          shown_definition("FileLiveTarget") == "arity:1 I" &&
          !defined_name("FileLiveTarget@2") &&
          stepped_expression("FileLiveUse x") == "x" &&
          combdsl::set_list() == before_failed_live_change);

    auto const successful_live_change = load_set_list(
        "set FileLiveTarget = 1 K\n");
    check("a successful load updates every shared live reference",
          successful_live_change.success &&
          shown_definition("FileLiveTarget@2") == "arity:1 K" &&
          stepped_expression("FileLiveUse x") == "Kx");

    auto const replay_modes = load_set_list(
        "snapshot off\n"
        "set FileModeTarget = 1 I\n"
        "set FileModeLive = 1 FileModeTarget\n"
        "snapshot on\n"
        "set FileModeFrozen = 1 FileModeTarget\n"
        "set FileModeTarget = 1 K\n"
        "remove FileModeTarget\n"
        "set FileModeTarget = 1 S\n");
    check("a successful load replays live and frozen modes",
          replay_modes.success &&
          replay_modes.loaded == 8 &&
          shown_definition("FileModeLive") ==
              "arity:1 FileModeTarget" &&
          shown_definition("FileModeFrozen") ==
              "arity:1 FileModeTarget@1" &&
          stepped_expression("FileModeLive x") == "Sx" &&
          stepped_expression("FileModeFrozen x") == "x");
    check("a successful load preserves versions across removal",
          shown_definition("FileModeTarget@1") == "arity:1 I" &&
          shown_definition("FileModeTarget@2") == "arity:1 K" &&
          shown_definition("FileModeTarget@3") == "arity:1 S" &&
          combdsl::detail::registered_parser_lookup_snapshot()
              .snapshot_enabled);

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
