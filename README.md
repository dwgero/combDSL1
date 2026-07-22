# combDSL

`combDSL` is a tiny, header-only C++20 DSL for writing with the `S`, `K`, `I`,
and `Y` combinators in a functional style. It uses templates and ordinary
function-call syntax, and works with heterogeneous or move-only values without
`std::function`.

```cpp
#include <combdsl/combinators.hpp>

using namespace combdsl;

assert(I(42) == 42);
assert(K(42)("discarded") == 42);
assert(S(K)(K)(42) == I(42));
```

## The combinators

The API mirrors the usual definitions through curried application:

```text
I(x)       = x
K(x)(y)    = x
S(f)(g)(x) = f(x)(g(x))
Y(x)       = x(defer([&] { return Y(x); }))
```

`S`, `K`, and `I` are cached deferred callable heads. Applying one forces only
the combinator itself; it does not force a deferred value passed as an operand.
In particular, `I(lazy)` preserves `lazy`, and `K(x)(lazy)` discards it
untouched. `S` itself forces only deferred callable positions: `f`, `g`, and
`f(z)`; either user-supplied function can still demand `z` by accepting a
concrete value type. `force(S)`, `force(K)`, and `force(I)` expose the cached
underlying callables.
Forcing uses a runtime cache, so public `S`, `K`, and `I` applications are not
constant expressions; use runtime assertions rather than `static_assert`.

For example, `S(add)(I)` doubles its input because `S` supplies that input to
both `add` and `I`:

```cpp
constexpr auto add = [](int left) {
    return [left](int right) { return left + right; };
};

auto twice = S(add)(I);
assert(twice(21) == 42);
```

Composition can be expressed with `S(K(f))(g)`:

```cpp
constexpr auto square = [](int x) { return x * x; };
constexpr auto increment = [](int x) { return x + 1; };
auto square_after_increment = S(K(square))(increment);

assert(square_after_increment(4) == 25);
```

## Printing expressions

Calling a combinator expression without an argument prints its prefix form to
`std::cout` without adding a newline:

```cpp
I();        // I
K();        // K
S();        // S
Y();        // Y
K(42)();    // K<42>
S(K)();     // SK
S(K)(I)();  // SKI
```

Ordinary operands that are not `S`, `K`, `I`, `Y`, or a combinator partial
application are enclosed in angle brackets. Their contents are rendered with
`toString()` when available, and otherwise with `operator<<`. An unprintable
value is shown as `<?>`, while a deferred computation is shown as `<deferred>`
without being forced. Raw narrow strings are the symbolic exception described
below.

Symbols are symbolic callables and print without angle brackets. The `char`
overload requires a lowercase ASCII letter (`a` through `z`). The
`std::string_view` overload accepts exactly one valid UTF-8 character encoded
in one to four bytes, including non-ASCII symbols. Applying symbols builds an
expression tree, so combinator reductions can be inspected directly:

```cpp
const auto x = symbol('x');
const auto y = symbol('y');
const auto z = symbol('z');
const auto lambda = symbol("\xCE\xBB"); // UTF-8 lambda

S(x)(y)(z)();  // xz(yz)
lambda(x)();    // λx
```

The string-view overload copies its encoded bytes, so temporary views are
safe. It rejects empty strings, multiple characters, overlong encodings,
UTF-16 surrogates, and values above U+10FFFF. Its one-byte case accepts any
ASCII character; the lowercase rule belongs to the `char` overload and the
text parser.

Raw string literals, C strings, `std::string`, and `std::string_view` operands
are automatically copied into callable symbolic string expressions. They
print without quotes or angle brackets. Names longer than one byte use the
same spacing rules as multi-character basis names:

```cpp
x("word")();              // x word
I("word")(x)();           // word x
S(I)(I)("word")();        // word word
quote("left")("right")(); // left right
```

Raw strings are byte-preserving and do not receive UTF-8 validation. Empty or
null strings are rejected. Consequently, raw UTF-8 text and an explicit symbol
have different spacing: `x("\xE2\x97\x8F")()` prints `x ●`, while
`x(symbol("\xE2\x97\x8F"))()` prints `x●`. A printed multi-character raw name
is not parsed atomically by itself; it must be surrounded by the escaped word
delimiters described below or registered as a basis. Wrap a string in a
distinct user-defined type when it should remain ordinary C++ data rather than
a symbolic operand.

Application is left-associative, and a symbolic application used as an operand
is parenthesized: `x(y)(z)()` prints `xyz`, while `x(y(z))()` prints `x(yz)`.
Likewise, `K(x(y))()` prints `K(xy)`, and `S(K(x(y)))(z)()` prints
`S(K(xy))z`. Deferred operands remain unforced while the symbolic tree is built
or printed.

### Named bases

`M` and `T` are among the named bases provided by
`<combdsl/combinators.hpp>`. Many of the bird combinators in Raymond Smullyan's
book *To Mock a Mockingbird* have been defined, including Bluebird (`B`),
Cardinal (`C`), Dove (`D`), Identity (`I`), Kestrel (`K`), Lark (`L`),
Mockingbird (`M`), Owl (`O`), Robin (`R`), Starling (`S`), Thrush (`T`),
Vireo (`V`), and Warbler (`W`), using the capital first letter of each name.
The Sage bird has been defined as `Y`, to match current conventions.
The `basis(name, arity, combinator_expression)` function assigns an atomic
printed name to any other combinator expression without changing its behavior.
Named callables are deferred and cached like `S`, `K`, and `I`; copies share
the same cached value:

```cpp
const auto x = symbol('x');
const auto y = symbol('y');

const auto M = basis("M", 1, S(I)(I));
const auto T = basis("T", 2, S(K(S(I)))(K));

M();     // M
M(x)();  // xx
K(M)();  // KM
T();        // T
T(x)();     // Tx
T(x)(y)();  // yx

auto copied_M = M;
assert(&force(copied_M) == &force(M));
```

The arity controls when the named basis expands: undersaturated applications
retain the basis name, while reaching the declared argument count evaluates
the stored combinator. Arity must be at least one.

Expression printing separates a multi-character basis name from adjacent
non-parenthesis tokens. Parentheses act as boundaries and stay attached on
either side. For `auto Alias = basis("Alias", 1, I);`, `K(Alias)()` prints
`K Alias`; `x(Cstar(y))()` prints `x(Cstar y)`; `Cstar(y(z))()` prints
`Cstar(yz)`; and `x(y(z))(Cstar)()` prints `x(yz)Cstar`.

Basis names are copied into the expression and may contain up to seven
characters. Names cannot be empty or begin with a null character; a later null
character terminates the copied name. Because leading whitespace and
parentheses belong to the parser grammar, names cannot begin with one of those
characters or with a double quote. A name also cannot begin with a single
or doubled backslash, though a doubled backslash may occur later in the name.
A visible name longer than seven characters throws `std::length_error`; an
invalid name throws `std::invalid_argument`.

Every successful `basis(...)` call also registers that name with the text
parser. The first definition registered for a name wins; later bases with the
same name remain usable objects but do not replace the parser definition.
Exact `S`, `K`, `I`, and `Y` names always retain their primitive meanings.
Registrations own a deferred handle, so a local basis remains parseable after
its original handle is destroyed without evaluating its body during
registration. Any references borrowed by the basis body must still remain
valid when the registered definition is parsed or reduced. The registry and
the returned handle share the basis's deferred state. A move-only definition
can be parsed repeatedly, but destructively invoking its native handle as an
rvalue can move from the registered definition; do not parse or reduce that
name afterward. A registration in another translation unit becomes visible
once that translation unit has been linked and initialized.

## Quoted single-step reduction

Ordinary combinator calls reduce eagerly. `quote` instead builds an immutable
application tree, and `single_step` contracts at most one redex. A head
reduction has priority; when none is possible, it contracts the leftmost
nested redex:

```cpp
const auto x = symbol('x');
auto expression = quote(S)(K)(I)(x);

expression();                          // SKIx
single_step(expression)();             // Kx(Ix)
single_step(single_step(expression))(); // x
```

Trailing operands are preserved. Unknown and undersaturated heads are skipped
while the search continues through their explicit subexpressions. `S`, `K`,
`I`, `Y`, deferred recursive `Y` nodes, and saturated named bases can all
reduce in nested positions. `Y` exposes its recursive operand without forcing
it, and a saturated named basis expands by one step:

```cpp
single_step(quote(Y)(x))(); // x<deferred Y(x)>
single_step(quote(M)(x))(); // SIIx
```

Quoted application nodes are immutable and shared, so an `S` reduction can
reuse the same quoted operand even when it owns a move-only value.

`eval` repeatedly applies this reduction technique and prints the resulting
expression as one line when no eligible reduction remains. It does not print
intermediate expressions or a status label during an uninterrupted
evaluation, and its output defaults to `std::cout`:

```cpp
eval(quote(S)(K)(I)(x)); // prints: x
```

A different output stream can be supplied as the second argument, and an
input stream can be supplied as the third. The input defaults to `std::cin`.
SIGINT (normally Ctrl-C) pauses evaluation at the next reduction boundary and
prints the current expression as one line. Press Enter to resume, or enter `q`
or `Q` to quit; end-of-input also quits. If a reduction finished before the
interrupt was observed, the newly reduced expression is current. The
program's previous SIGINT handler is restored after evaluation finishes or is
quit.

For an interactive reduction, `single_step_loop` prints the starting term and
waits for input. Each blank Enter applies `single_step`, using the same
head-priority, leftmost-nested reduction order. Type `q` and Enter to quit.
The loop stops when no eligible reduction remains:

```cpp
single_step_loop(quote(S)(K)(I)(x));
```

`single_step_run` repeatedly applies the same reduction technique without
waiting for input. It prints the expression after each successful reduction
and returns when no eligible reduction remains; it does not print the starting
expression. Its output defaults to `std::cout`, and its input defaults to
`std::cin`:

```cpp
single_step_run(quote(S)(K)(I)(x));
// Kx(Ix)
// x
```

While `single_step_run` is reducing, `SIGINT` (normally Ctrl-C) pauses it at a
step boundary. Press Enter to resume from that expression, or enter `q` or `Q`
to end the run. The program's previous `SIGINT` handler is restored afterward.
Because a signal handler is process-wide, concurrent calls to `eval` and
`single_step_run` are not supported.

`eval` and `single_step_run` keep reducing an expression with an endless
sequence of eligible reductions until interrupted.

`input_escape` copies input text and prefixes a backslash before every
backslash or double quote. It does not add surrounding delimiters, and all
other bytes are preserved:

```cpp
input_escape("a\\b\"c") == "a\\\\b\\\"c";
input_escape("R") == "R";

auto source = input_escape("x \"word\" y \"mid\\dle\" z");
parse(source); // x word y mid\dle z
```

### Parsing expressions

`parse` turns text into a `quoted_expression`. Application is
left-associative, parentheses group expressions, and whitespace is ignored:

```cpp
auto expression = parse(" S K (I) x "); // SKIx
single_step_run(expression);

parse_and_step("K (I x) y");
// Ix
// x

parse_eval("K (I x) y"); // prints: x
```

`parse_eval` parses a string and passes the resulting quoted expression to
`eval`. Its output and input streams can be supplied as the second and third
arguments; they default to `std::cout` and `std::cin`.

`read_parse_eval` reads exactly one line and passes it to `parse_eval`:

```cpp
read_parse_eval(); // reads from std::cin and writes to std::cout
```

Input and output streams can be supplied as the first and second arguments.
The input stream also supplies Enter or `q`/`Q` if evaluation is interrupted.
It normally prints only the final reduced expression; on interruption, it
prints the current expression and the resume prompt. End-of-input before an
expression produces no output, while malformed or empty lines throw
`combdsl::parse_error`.

The `crepl` executable applies `input_escape` to each line before passing it to
`parse_eval`, so ordinary quoted words and backslashes can be entered directly.
Enter exactly `q` or `Q` at its prompt to exit.

`S`, `K`, `I`, and `Y` are reserved combinators. A single-character name
registered by `basis(...)` parses the same way, so `Mx` means `M` applied to
`x`. Multi-character registered names use exact complete-token lookup.
Whitespace, parentheses, and escaped-word openers delimit those tokens, so
`Cstar x` means `Cstar` applied to `x`, while `Cstarx` is an unknown operand;
it does not fall back to `C` followed by five symbols. Every remaining
lowercase ASCII letter (`a` through `z`) becomes a single-character symbol, so
compact primitive and symbol expressions such as `SKIx` remain valid.

In escaped parser input, the two characters `\"` open or close one raw word,
and `\\` represents one backslash. This input:

```text
x \"word\" y \"mid\\dle\" z
```

parses and prints as `x word y mid\dle z`. Spaces, parentheses, UTF-8 bytes,
and other ordinary bytes between the word delimiters are content rather than
parser syntax. Outside a word, `\\` is a one-character symbolic backslash
operand. A doubled backslash may also occur later in a registered basis name,
where exact token lookup consumes it as part of that name. Any other
sequence beginning with a backslash is rejected, as are empty or unterminated
words. A word whose spelling matches a primitive or registered basis remains
raw.

Malformed or empty input throws `combdsl::parse_error`. Its `position()` is the
zero-based byte position of the error; an error at the end of input reports the
input length. `parse_and_step` uses the same `std::cout` and `std::cin` defaults,
including the same Ctrl-C pause behavior, as `single_step_run`.

## Recursion with Y

C++ evaluates ordinary function arguments eagerly, so `Y` supplies its
recursive value through `defer`. The generator receives that deferred `self`
and returns the recursive body:

```cpp
auto factorial = Y([](auto self) {
    return [self = std::move(self)](auto n) -> unsigned long long {
        return n < 2 ? 1 : n * force(self)(n - 1);
    };
});

assert(factorial(10) == 3'628'800);
```

The implementation is operationally equivalent to the equation above, but it
owns a decayed copy of `x` in shared state instead of retaining the literal
`[&]` capture, which would dangle after `Y(x)` returned. Each demanded recursive
unfolding is memoized. Use a generic `auto` parameter in the returned unary body
and an explicit result type, as above; this breaks C++'s otherwise circular
return-type deduction.

## Value semantics

Partial applications own decayed copies of their operands. Use `std::ref` when
you intentionally want to borrow an object. `I` perfectly forwards its input.
`K` returns a copy when its partial application is reused, or moves the stored
value when the partial application is itself consumed:

```cpp
auto one_shot = K(std::make_unique<int>(42));
auto pointer = std::move(one_shot)("ignored");
```

Because `S` sends the same final argument down two branches, it memoizes the
already-evaluated `z` in one lvalue binding. Both branches observe that same
binding, so an expression such as `S(f)(g)(expensive())` calls `expensive()`
once and never moves its result twice.

### Deferred arguments

C++ evaluates ordinary function arguments before entering `operator()`, even
though the combinators themselves are deferred. Therefore the exact expression
`S(K(f))(K(g))(expensive())` must call `expensive()`. Use `defer` to place an
explicit lazy boundary around a computation:

```cpp
int evaluations = 0;
auto lazy_z = defer([&] {
    ++evaluations;
    return expensive();
});

auto result = S(K(f))(K(g))(lazy_z);
assert(evaluations == 0);  // neither K branch needs z
```

When a typed consumer needs `z`, the deferred value converts to its result and
evaluates the computation. Its result is cached in shared state, so both `S`
branches—and copies of the deferred value—reuse the same result. Generic
consumers can request the value explicitly with `force(z)`.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/combdsl_example
./build/combdsl_single_step
./build/crepl
```

### Browser WebAssembly

The browser target exposes the equivalent of
`parse_eval(input_escape(source))` through a Web Worker. Configure and build it
with Emscripten:

```sh
EM_CACHE="$PWD/build-emscripten-cache" emcmake cmake -S . -B build-browser \
    -DCOMBDSL_BUILD_BROWSER=ON \
    -DCOMBDSL_BUILD_EXAMPLES=OFF \
    -DCOMBDSL_BUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE=Release
EM_CACHE="$PWD/build-emscripten-cache" \
    cmake --build build-browser --target combdsl_browser
```

Serve the generated static files with any HTTP server and open
`http://localhost:8000/`:

```sh
python3 -m http.server 8000 --directory build-browser/web
```

This does not use `emrun`. Loading through `file://` is not supported because
the page starts a Web Worker and fetches `combdsl.wasm`; opening the file
directly displays the required HTTP-server command. Cancel terminates and
recreates the worker, so a non-normalizing expression does not freeze the page.
The Single Step button switches between displaying only the evaluated result
and displaying every reduction produced by
`single_step_run(parse(input_escape(source)))`. Evaluations accumulate in the
results area with a blank line between them. The Key Step button starts a
manual reduction session: after submitting an expression, each ordinary
keypress performs exactly one `single_step`, and Cancel ends the session. The
Single Step and Key Step modes are mutually exclusive. The browser prints the
submitted starting expression immediately, then appends the output beneath it.
Cancelling an evaluation appends `[cancelled]` beneath its starting expression.
The Help button summarizes both stepping modes in a keyboard-accessible dialog.

For another CMake project, link the interface target after adding this project:

```cmake
add_subdirectory(path/to/combDSL)
target_link_libraries(your_target PRIVATE combdsl::combdsl)
```
