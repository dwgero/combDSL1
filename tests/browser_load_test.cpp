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

[[nodiscard]] std::string shown_revisions(std::string_view name) {
    std::string command = "revisions ";
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

[[nodiscard]] std::string evaluated_expression(std::string_view source) {
    std::ostringstream output;
    combdsl::eval(combdsl::parse(source), output);
    auto result = output.str();
    if (result.ends_with('\n')) {
        result.pop_back();
    }
    return result;
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

    auto const legacy_snapshot = load_set_list("snapshot off\n");
    check("a legacy snapshot command selects and records live references",
          legacy_snapshot.success &&
          legacy_snapshot.loaded == 1 &&
          combdsl::set_list() == "references live");

    auto const first_references = load_set_list(
        "references captured\n"
        "references live\n");
    check("leading references commands compact to the last one",
          first_references.success &&
          first_references.loaded == 2 &&
          combdsl::set_list() == "references live");

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
    check("the legacy mode creates a live reference",
          shown_definition("FileUse") == "arity:1 FileGood");
    check("the compacted leading references command precedes definitions",
          combdsl::set_list().starts_with(
              "references live\nset FileGood = 0 I"));
    check("an error-free load has no diagnostic messages",
          format_file_load_diagnostics(
              "successful.cmb", successful).empty());

    auto const numeric_values = load_set_list(
        "set FileInteger = 42\n"
        "define FileNumber x = 7\n");
    check("a file can load nonnegative integer values",
          numeric_values.success &&
          numeric_values.loaded == 2 &&
          numeric_values.diagnostics.empty());
    check("loaded integer values retain their numeric bodies",
          shown_definition("FileInteger") == "arity:0 42" &&
          shown_definition("FileNumber") == "arity:1 K 7");
    check("a loaded integer function evaluates to its value",
          stepped_expression("FileNumber y") == "7");
    auto const saved_numeric_values = combdsl::set_list();
    check("saved integer definitions contain parser-readable numbers",
          saved_numeric_values.find(
              "set FileInteger = 0 42") != std::string::npos &&
          saved_numeric_values.find(
              "define FileNumber x = 7") !=
              std::string::npos &&
          saved_numeric_values.find('<') == std::string::npos);
    auto const reloaded_numeric_values = load_set_list(
        saved_numeric_values);
    check("a saved file containing numeric values reloads",
          reloaded_numeric_values.success &&
          reloaded_numeric_values.diagnostics.empty());
    check("reloaded integer values still display without angle brackets",
          shown_definition("FileInteger") == "arity:0 42" &&
          shown_definition("FileNumber") == "arity:1 K 7");

    auto const prohibited_numeric_values = load_set_list(
        "set FileNegative = -1\n"
        "set FileFloating = 1.5\n"
        "set FileExponent = 1e3\n");
    check("a file rejects negative and floating numeric values",
          !prohibited_numeric_values.success &&
          !prohibited_numeric_values.aborted &&
          prohibited_numeric_values.loaded == 0 &&
          prohibited_numeric_values.diagnostics.size() == 3);
    check("a failed numeric file load registers none of its names",
          !defined_name("FileNegative") &&
          !defined_name("FileFloating") &&
          !defined_name("FileExponent"));
    check("a failed numeric file load restores prior definitions",
          shown_definition("FileInteger") == "arity:0 42" &&
          shown_definition("FileNumber") == "arity:1 K 7");

    auto const before_lowercase_name_load = combdsl::set_list();
    auto const lowercase_names = load_set_list(
        "set FileBeforeLC = 0 I\n"
        "set lower = 1 I\n"
        "define anotherlower x=x\n"
        "set FileAfterLC = 0 I\n");
    constexpr std::string_view lowercase_name_error =
        "combdsl::basis names cannot begin with a lowercase ASCII letter";
    check("a file rejects lowercase-leading set and define names",
          !lowercase_names.success &&
          !lowercase_names.aborted &&
          lowercase_names.loaded == 0 &&
          lowercase_names.diagnostics.size() == 2);
    check("lowercase-name diagnostics retain their source lines",
          lowercase_names.diagnostics.size() == 2 &&
          lowercase_names.diagnostics[0].line == 2 &&
          lowercase_names.diagnostics[1].line == 3);
    check("lowercase-name diagnostics point at each definition name",
          lowercase_names.diagnostics.size() == 2 &&
          lowercase_names.diagnostics[0].position == 4 &&
          lowercase_names.diagnostics[1].position == 7);
    check("lowercase-name diagnostics preserve their exact detail",
          lowercase_names.diagnostics.size() == 2 &&
          lowercase_names.diagnostics[0].detail ==
              lowercase_name_error &&
          lowercase_names.diagnostics[1].detail ==
              lowercase_name_error);
    check("lowercase-name file errors use one-based display positions",
          format_file_load_diagnostics(
              "lowercase.cmb", lowercase_names) ==
          "Parse error in file lowercase.cmb on line 2 at position 5: "
          "combdsl::basis names cannot begin with a lowercase ASCII letter\n"
          "Parse error in file lowercase.cmb on line 3 at position 8: "
          "combdsl::basis names cannot begin with a lowercase ASCII letter\n"
          "Errors are preventing any changes from being made");
    check("a lowercase-name file failure rolls back every valid record",
          !defined_name("FileBeforeLC") &&
          !defined_name("FileAfterLC") &&
          !defined_name("lower") &&
          !defined_name("anotherlower") &&
          combdsl::set_list() == before_lowercase_name_load);

    auto const legacy_live_update = load_set_list(
        "set FileGood = 0 K\n");
    check("a dependency loaded after legacy snapshot off follows changes",
          legacy_live_update.success &&
          stepped_expression("FileUse x") == "Kx" &&
          combdsl::set_list().find("snapshot") == std::string::npos);

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

    auto const references_only_live = load_set_list("references live\n");
    check("a references-only file persists its mode and history",
          references_only_live.success &&
          references_only_live.loaded == 1 &&
          !combdsl::detail::registered_parser_lookup_snapshot()
               .snapshot_enabled &&
          combdsl::set_list().ends_with("references live"));

    auto const before_failed_references = combdsl::set_list();
    auto const failed_references = load_set_list(
        "references captured\n"
        "@\n");
    check("a failed load rolls back reference mode and history",
          !failed_references.success &&
          !combdsl::detail::registered_parser_lookup_snapshot()
               .snapshot_enabled &&
          combdsl::set_list() == before_failed_references);

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
        "references live\n"
        "set FileModeTarget = 1 I\n"
        "set FileModeLive = 1 FileModeTarget\n"
        "references captured\n"
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

    auto const replay_overrides = load_set_list(
        "references captured\n"
        "set FileOT = 2 I\n"
        "set live FileOL = 1 FileOT\n"
        "references live\n"
        "define captured FileOC x = FileOT x\n"
        "set FileOT = 2 K\n"
        "references captured\n");
    check("a successful load replays per-definition overrides",
          replay_overrides.success &&
          replay_overrides.loaded == 7 &&
          shown_definition("FileOL") == "arity:1 FileOT" &&
          shown_definition("FileOC") == "arity:1 FileOT@1" &&
          evaluated_expression("FileOL x y") == "x" &&
          evaluated_expression("FileOC x y") == "xy");
    check("a successful load retains explicit override syntax",
          combdsl::set_list().find(
              "set live FileOL = 1 FileOT") !=
              std::string::npos &&
          combdsl::set_list().find(
              "define captured FileOC x = FileOT x") !=
              std::string::npos &&
          combdsl::detail::registered_parser_lookup_snapshot()
              .snapshot_enabled);

    auto const replay_revision_modes = load_set_list(
        "references captured\n"
        "set FileRevMode = 1 I\n"
        "references live\n"
        "set FileRevMode = 2 K\n"
        "remove FileRevMode\n"
        "set captured FileRevMode = 3 S\n");
    auto const replayed_revisions = shown_revisions("FileRevMode");
    check("a successful load preserves chronological revision modes",
          replay_revision_modes.success &&
          replay_revision_modes.loaded == 6 &&
          replayed_revisions ==
              "FileRevMode@1 arity:1 I [captured]\n"
              "FileRevMode@2 arity:2 K [live]\n"
              "FileRevMode@3 arity:3 S [captured] [current]" &&
          !combdsl::detail::registered_parser_lookup_snapshot()
               .snapshot_enabled);

    auto const before_failed_revision_mode_load = combdsl::set_list();
    auto const before_failed_revision_mode =
        combdsl::detail::registered_parser_lookup_snapshot()
            .snapshot_enabled;
    auto const failed_revision_mode_load = load_set_list(
        "references captured\n"
        "set live FileRevMode = 4 C\n"
        "@\n");
    check("a failed load restores revision output and mode metadata",
          !failed_revision_mode_load.success &&
          shown_revisions("FileRevMode") == replayed_revisions &&
          !defined_name("FileRevMode@4") &&
          combdsl::set_list() == before_failed_revision_mode_load &&
          combdsl::detail::registered_parser_lookup_snapshot()
                  .snapshot_enabled ==
              before_failed_revision_mode);

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

    auto const multiline_escaped_word = load_set_list(
        R"(set FileEscapedWord = 0 "left\"mid\\
right"
set FileAfterWord = 0 I
)");
    check("a multiline word with C-style escapes loads as one record",
          multiline_escaped_word.success &&
          !multiline_escaped_word.aborted &&
          multiline_escaped_word.loaded == 2 &&
          multiline_escaped_word.diagnostics.empty());
    check("escaped quotes and slashes survive a multiline file load",
          evaluated_expression("FileEscapedWord") ==
              "left\"mid\\\nright");
    check("a record following the multiline escaped word is loaded",
          defined_name("FileAfterWord"));
    constexpr std::string_view expected_multiline_definition =
        R"(set FileEscapedWord = 0 "left\"mid\\
right")";
    auto const multiline_set_list = combdsl::set_list();
    check("the multiline escaped word is saved without splitting",
          multiline_set_list.find(expected_multiline_definition) !=
              std::string::npos);
    auto const reloaded_multiline = load_set_list(multiline_set_list);
    check("the saved multiline escaped word and following record reload",
          reloaded_multiline.success &&
          reloaded_multiline.diagnostics.empty() &&
          evaluated_expression("FileEscapedWord") ==
              "left\"mid\\\nright" &&
          defined_name("FileAfterWord"));

    auto const direct_backslash_names = load_set_list(
        R"(set \FileDirect = 1 I
set FileSlashUse = 1 \FileDirect
)");
    check("file loading parses direct backslash names without escaping",
          direct_backslash_names.success &&
          direct_backslash_names.loaded == 2 &&
          defined_name(R"(\FileDirect)") &&
          shown_definition(R"(\FileDirect)") == "arity:1 I" &&
          stepped_expression(R"(\FileDirect x)") == "x" &&
          stepped_expression("FileSlashUse x") == "x");
    auto const direct_backslash_set_list = combdsl::set_list();
    check("direct backslash names retain one byte in the saved journal",
          direct_backslash_set_list.find(
              R"(set \FileDirect = 1 I)") != std::string::npos &&
          direct_backslash_set_list.find(
              R"(set FileSlashUse = 1 \FileDirect)") !=
              std::string::npos);
    auto const reloaded_direct_backslash =
        load_set_list(direct_backslash_set_list);
    check("saved direct backslash names reload without input escaping",
          reloaded_direct_backslash.success &&
          reloaded_direct_backslash.diagnostics.empty() &&
          stepped_expression(R"(\FileDirect x)") == "x" &&
          stepped_expression("FileSlashUse x") == "x");

    auto const before_invalid_word_escape = combdsl::set_list();
    auto const invalid_word_escape = load_set_list(
        "set FileBeforeBad = 0 I\n"
        R"(set FileBadEscape = 0 "a\q")" "\n"
        "set FileAfterBad = 0 K\n");
    check("a non-C escape inside a loaded string is a parse error",
          !invalid_word_escape.success &&
          !invalid_word_escape.aborted &&
          invalid_word_escape.loaded == 0 &&
          invalid_word_escape.diagnostics.size() == 1 &&
          invalid_word_escape.diagnostics.front().line == 2);
    check("an invalid loaded string rolls back every file definition",
          !defined_name("FileBeforeBad") &&
          !defined_name("FileBadEscape") &&
          !defined_name("FileAfterBad") &&
          combdsl::set_list() == before_invalid_word_escape);

    std::cout << tests_run << " test(s) run, "
              << test_failures << " failed\n";
    return test_failures == 0 ? 0 : 1;
}
