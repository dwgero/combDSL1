#include <combdsl/combinators.hpp>

#include <emscripten/bind.h>

#include <exception>
#include <optional>
#include <sstream>
#include <string>

namespace {

struct evaluation_result {
    bool success;
    std::string output;
    std::string error;
};

struct single_step_result {
    bool success;
    bool reduced;
    std::string output;
    std::string error;
};

std::optional<combdsl::quoted_expression> stepped_expression;

[[nodiscard]] evaluation_result parse_eval_input(std::string const& source) {
    std::istringstream input;
    std::ostringstream output;

    try {
        combdsl::parse_eval(
            combdsl::input_escape(source), output, input);
        return {true, output.str(), {}};
    } catch (std::exception const& error) {
        return {false, {}, error.what()};
    } catch (...) {
        return {false, {}, "unknown evaluation error"};
    }
}

[[nodiscard]] evaluation_result single_step_run_input(
    std::string const& source, bool basis_step) {
    std::istringstream input;
    std::ostringstream output;

    try {
        combdsl::single_step_run(
            combdsl::parse(combdsl::input_escape(source)), output, input,
            basis_step);
        return {true, output.str(), {}};
    } catch (std::exception const& error) {
        return {false, {}, error.what()};
    } catch (...) {
        return {false, {}, "unknown evaluation error"};
    }
}

[[nodiscard]] evaluation_result begin_single_step_input(
    std::string const& source) {
    stepped_expression.reset();

    try {
        stepped_expression.emplace(
            combdsl::parse(combdsl::input_escape(source)));
        return {true, {}, {}};
    } catch (std::exception const& error) {
        return {false, {}, error.what()};
    } catch (...) {
        return {false, {}, "unknown parsing error"};
    }
}

[[nodiscard]] single_step_result take_single_step(bool basis_step) {
    if (!stepped_expression.has_value()) {
        return {false, false, {}, "no expression is ready to step"};
    }

    try {
        auto next = combdsl::single_step(*stepped_expression, basis_step);
        if (combdsl::detail::quoted_access::root(next) ==
            combdsl::detail::quoted_access::root(*stepped_expression)) {
            stepped_expression.reset();
            return {true, false, {}, {}};
        }

        stepped_expression = std::move(next);
        std::ostringstream output;
        stepped_expression->print_to(output);
        output << '\n';
        return {true, true, output.str(), {}};
    } catch (std::exception const& error) {
        stepped_expression.reset();
        return {false, false, {}, error.what()};
    } catch (...) {
        stepped_expression.reset();
        return {false, false, {}, "unknown evaluation error"};
    }
}

} // namespace

EMSCRIPTEN_BINDINGS(combdsl_browser) {
    emscripten::value_object<evaluation_result>("EvaluationResult")
        .field("success", &evaluation_result::success)
        .field("output", &evaluation_result::output)
        .field("error", &evaluation_result::error);

    emscripten::value_object<single_step_result>("SingleStepResult")
        .field("success", &single_step_result::success)
        .field("reduced", &single_step_result::reduced)
        .field("output", &single_step_result::output)
        .field("error", &single_step_result::error);

    emscripten::function("parseEval", &parse_eval_input);
    emscripten::function("singleStepRun", &single_step_run_input);
    emscripten::function("beginSingleStep", &begin_single_step_input);
    emscripten::function("takeSingleStep", &take_single_step);
}
