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

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct find_benchmark_case {
    std::string_view name;
    std::string_view symbols;
    std::string_view target;
    std::size_t maximum_size;
    std::string_view description;
};

constexpr std::array benchmark_cases{
    find_benchmark_case{
        "short",
        "x",
        "Y",
        3,
        "failed size-3 search with a small target",
    },
    find_benchmark_case{
        "deep",
        "abcdef",
        "a(b(c(d(ef))))",
        3,
        "failed size-3 search with deeper normalization",
    },
    find_benchmark_case{
        "matches",
        "xy",
        "x(yx)",
        3,
        "size-3 search with matching result aggregation",
    },
    find_benchmark_case{
        "quad",
        "x",
        "Y",
        4,
        "exhaustive size-4 search; this can take minutes",
    },
};

struct command_line_options {
    std::vector<std::string_view> case_names;
    std::size_t warmup_count = 1;
    std::size_t repetition_count = 5;
    bool warmup_count_was_set = false;
    bool repetition_count_was_set = false;
    bool full = false;
    bool list = false;
    bool help = false;
};

struct prepared_case {
    find_benchmark_case const* specification;
    std::vector<combdsl::quoted_atomic> symbols;
    combdsl::quoted_expression target;
};

struct find_result_summary {
    std::string signature;
    std::array<std::size_t, 4> match_counts;
};

struct timed_find_result {
    double seconds;
    find_result_summary summary;
};

[[nodiscard]] std::size_t parse_count(
    std::string_view text,
    std::string_view option_name,
    bool allow_zero) {
    std::size_t result = 0;
    auto const conversion = std::from_chars(
        text.data(), text.data() + text.size(), result);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != text.data() + text.size() ||
        (!allow_zero && result == 0)) {
        throw std::invalid_argument(
            std::string(option_name) +
            (allow_zero
                 ? " requires a non-negative integer"
                 : " requires a positive integer"));
    }
    return result;
}

[[nodiscard]] command_line_options parse_command_line(
    int argc,
    char const* const* argv) {
    command_line_options options;
    for (int index = 1; index < argc; ++index) {
        auto const argument = std::string_view(argv[index]);
        auto require_value = [&](std::string_view option_name) {
            if (++index == argc) {
                throw std::invalid_argument(
                    std::string(option_name) +
                    " requires an argument");
            }
            return std::string_view(argv[index]);
        };

        if (argument == "--case") {
            options.case_names.push_back(
                require_value(argument));
        } else if (argument == "--warmup") {
            options.warmup_count = parse_count(
                require_value(argument), argument, true);
            options.warmup_count_was_set = true;
        } else if (argument == "--repeat") {
            options.repetition_count = parse_count(
                require_value(argument), argument, false);
            options.repetition_count_was_set = true;
        } else if (argument == "--full") {
            options.full = true;
        } else if (argument == "--list") {
            options.list = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            throw std::invalid_argument(
                "unknown benchmark option: " +
                std::string(argument));
        }
    }
    if (options.full && !options.case_names.empty()) {
        throw std::invalid_argument(
            "--full cannot be combined with --case");
    }
    return options;
}

void print_usage(std::ostream& output, char const* program_name) {
    output
        << "Usage: " << program_name << " [options]\n"
        << "  --case <name>  run one case; may be repeated\n"
        << "  --full         run every case, including size 4\n"
        << "  --warmup <n>   untimed warmups per case (default 1)\n"
        << "  --repeat <n>   timed repetitions per case (default 5)\n"
        << "  --list         list the available cases\n"
        << "  --help, -h     display this help\n\n"
        << "Without --case or --full, the short and deep cases run.\n"
        << "Size-4 cases default to no warmup and one repetition.\n";
}

void print_case_list(std::ostream& output) {
    for (auto const& benchmark_case : benchmark_cases) {
        output << std::left << std::setw(10)
               << benchmark_case.name << " | "
               << benchmark_case.description << '\n';
    }
}

[[nodiscard]] find_benchmark_case const& find_case(
    std::string_view name) {
    auto const match = std::ranges::find(
        benchmark_cases, name, &find_benchmark_case::name);
    if (match == benchmark_cases.end()) {
        throw std::invalid_argument(
            "unknown benchmark case: " + std::string(name));
    }
    return *match;
}

[[nodiscard]] std::vector<find_benchmark_case const*>
select_cases(command_line_options const& options) {
    std::vector<find_benchmark_case const*> result;
    if (options.full) {
        for (auto const& benchmark_case : benchmark_cases) {
            result.push_back(&benchmark_case);
        }
        return result;
    }
    if (options.case_names.empty()) {
        result.push_back(&benchmark_cases[0]);
        result.push_back(&benchmark_cases[1]);
        return result;
    }
    for (auto const name : options.case_names) {
        auto const& benchmark_case = find_case(name);
        if (std::ranges::find(result, &benchmark_case) ==
            result.end()) {
            result.push_back(&benchmark_case);
        }
    }
    return result;
}

[[nodiscard]] prepared_case prepare_case(
    find_benchmark_case const& specification) {
    std::vector<combdsl::quoted_atomic> symbols;
    symbols.reserve(specification.symbols.size());
    for (auto const name : specification.symbols) {
        symbols.emplace_back(combdsl::symbol(name));
    }
    return {
        &specification,
        std::move(symbols),
        combdsl::parse(specification.target),
    };
}

[[nodiscard]] find_result_summary summarize(
    combdsl::combinator_find_result const& result) {
    std::ostringstream signature;
    auto append = [&](char label, auto const& expressions) {
        signature << label << expressions.size() << ':';
        for (auto const& expression : expressions) {
            expression.print_to(signature);
            signature << '\n';
        }
    };
    append('1', result.singles);
    append('2', result.pairs);
    append('3', result.triples);
    append('4', result.quads);
    return {
        std::move(signature).str(),
        {
            result.singles.size(),
            result.pairs.size(),
            result.triples.size(),
            result.quads.size(),
        },
    };
}

[[nodiscard]] timed_find_result run_once(
    prepared_case const& benchmark_case) {
    auto const start = std::chrono::steady_clock::now();
    auto const matches = combdsl::find_combinator_matches(
        benchmark_case.symbols,
        benchmark_case.target,
        {
            .maximum_size =
                benchmark_case.specification->maximum_size,
            .all_sizes = true,
        });
    auto const finish = std::chrono::steady_clock::now();
    return {
        std::chrono::duration<double>(finish - start).count(),
        summarize(matches),
    };
}

[[nodiscard]] std::size_t candidate_count_through(
    std::size_t maximum_size) {
    auto result = combdsl::check_for_match_combinator_count;
    if (maximum_size >= 2) {
        result += combdsl::check_for_pairs_match_candidate_count;
    }
    if (maximum_size >= 3) {
        result += combdsl::check_for_trips_match_candidate_count;
    }
    if (maximum_size >= 4) {
        result += combdsl::check_for_quads_match_candidate_count;
    }
    return result;
}

void run_benchmark(
    prepared_case const& benchmark_case,
    std::size_t warmup_count,
    std::size_t repetition_count) {
    std::optional<find_result_summary> expected_summary;
    auto accept_summary = [&](find_result_summary summary) {
        if (!expected_summary) {
            expected_summary = std::move(summary);
        } else if (
            summary.signature != expected_summary->signature ||
            summary.match_counts != expected_summary->match_counts) {
            throw std::runtime_error(
                "find results changed between benchmark runs");
        }
    };

    for (std::size_t index = 0; index < warmup_count; ++index) {
        accept_summary(run_once(benchmark_case).summary);
    }

    std::vector<double> samples;
    samples.reserve(repetition_count);
    for (std::size_t index = 0;
         index < repetition_count;
         ++index) {
        auto result = run_once(benchmark_case);
        samples.push_back(result.seconds);
        accept_summary(std::move(result.summary));
    }

    auto sorted_samples = samples;
    std::ranges::sort(sorted_samples);
    auto const middle = sorted_samples.size() / 2;
    auto const median = sorted_samples.size() % 2 == 0
        ? (sorted_samples[middle - 1] + sorted_samples[middle]) /
              2.0
        : sorted_samples[middle];
    auto const mean = std::accumulate(
        samples.begin(), samples.end(), 0.0) /
        static_cast<double>(samples.size());
    auto const total_matches = std::accumulate(
        expected_summary->match_counts.begin(),
        expected_summary->match_counts.end(),
        std::size_t{0});

    auto const& specification = *benchmark_case.specification;
    std::cout << "\ncase: " << specification.name << '\n'
              << "description: " << specification.description << '\n'
              << "symbols: ?" << specification.symbols << '\n'
              << "target: " << specification.target << '\n'
              << "maximum size: " << specification.maximum_size << '\n'
              << "warmups: " << warmup_count << '\n'
              << "repetitions: " << repetition_count << '\n'
              << "candidates: "
              << candidate_count_through(specification.maximum_size)
              << '\n'
              << "matches: " << total_matches
              << " [" << expected_summary->match_counts[0]
              << ", " << expected_summary->match_counts[1]
              << ", " << expected_summary->match_counts[2]
              << ", " << expected_summary->match_counts[3]
              << "]\n"
              << "seconds:";
    for (auto const sample : samples) {
        std::cout << ' ' << sample;
    }
    std::cout << "\nminimum: " << sorted_samples.front()
              << "\nmedian: " << median
              << "\nmean: " << mean
              << "\nmaximum: " << sorted_samples.back()
              << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto options = parse_command_line(argc, argv);
        if (options.help) {
            print_usage(std::cout, argv[0]);
            return 0;
        }
        if (options.list) {
            print_case_list(std::cout);
            return 0;
        }

        auto const selected_cases = select_cases(options);
#if defined(NDEBUG)
        constexpr auto build_mode = "release";
#else
        constexpr auto build_mode = "debug";
#endif
        std::cout << std::fixed << std::setprecision(6)
                  << "combDSL find benchmark\n"
                  << "build: " << build_mode << '\n'
                  << "hardware threads: "
                  << std::thread::hardware_concurrency() << '\n'
                  << "trip columns: "
                  << combdsl::check_for_trips_match_column_count << '\n'
                  << "quad columns: "
                  << combdsl::check_for_quads_match_column_count << '\n';

        for (auto const* specification : selected_cases) {
            auto benchmark_case = prepare_case(*specification);
            auto const warmup_count =
                specification->maximum_size == 4 &&
                        !options.warmup_count_was_set
                    ? std::size_t{0}
                    : options.warmup_count;
            auto const repetition_count =
                specification->maximum_size == 4 &&
                        !options.repetition_count_was_set
                    ? std::size_t{1}
                    : options.repetition_count;
            run_benchmark(
                benchmark_case,
                warmup_count,
                repetition_count);
        }
    } catch (std::exception const& error) {
        std::cerr << "benchmark error: " << error.what() << '\n';
        return 1;
    }
}
