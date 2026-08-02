# combDSL

`combDSL` is a header-only C++20 DSL for writing with the `S`, `K`, `I`,
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
`<combdsl/combinators.hpp>`. All the birds in the appendix "Who's Who Among
the Birds" in Raymond Smullyan's book
[*To Mock a Mockingbird*](https://en.wikipedia.org/wiki/To_Mock_a_Mockingbird)
have been defined, including Bluebird (`B`), Cardinal (`C`), Dove (`D`),
Eagle (`E`), Finch (`F`), Goldfinch (`G`), Hummingbird (`H`),
Identity bird (`I`), Jay (`J`), Kestrel (`K`), Lark (`L`),
Mockingbird (`M`), Owl (`O`), Queer bird (`Q`), Quixotic bird (`Q1`),
Quirky bird (`Q3`), Robin (`R`), Starling (`S`), Thrush (`T`), Turing bird (`U`),
Vireo (`V`), Warbler (`W`), and Converse warbler (`W1`), using a capital letter
and possible subscript for each name.
The Sage bird has been defined as `Y`, to match current conventions.
Cardinal star (`C*`), Cardinal star star (`C**`), Warbler star (`W*`), and
Warbler star star (`W**`) are also defined.
The following additional bird combinators have been defined: Albatross
(`A`),  Nightingale (`N`), and Zazu (`Z`).
The `basis(name, arity, combinator_expression)` function assigns an atomic
printed name to any other combinator expression without changing its behavior.
Named callables are deferred and cached like `S`, `K`, and `I`; copies share
the same cached value:

```cpp
const auto x = symbol('x');
const auto y = symbol('y');

const auto M = basis("M", 1, S(I)(I));
const auto T = basis("T", 2, S(K(S(I)))(K));
const auto Qzero = basis("Qzero", 0, K);
const auto Cstar = basis("Cstar", 4, S(K(C)));

M();            // M
M(x)();         // xx
K(M)();         // KM
T();            // T
T(x)();         // Tx
T(x)(y)();      // yx
Qzero();        // Qzero
Qzero(x)();     // Kx
Qzero(x)(y)();  // x

auto copied_M = M;
assert(&force(copied_M) == &force(M));
```

The arity controls when the named basis expands: undersaturated applications
retain the basis name, while reaching the declared argument count evaluates
the stored combinator. An arity of zero is always saturated, so its stored
combinator is immediately applied to every following argument. With no
following arguments, printing the basis still prints its name as usual.

Expression printing separates a multi-character basis name from adjacent
non-parenthesis tokens, except that a name ending outside lowercase ASCII
`a` through `z` stays attached to a following symbol. Parentheses act as
boundaries and stay attached on either side. Thus `Q1(x)()` prints `Q1x`.
For `auto Alias = basis("Alias", 1, I);`, `K(Alias)()` prints `K Alias`;
`x(Cstar(y))()` prints `x(Cstar y)`; `Cstar(y(z))()` prints `Cstar(yz)`;
and `x(y(z))(Cstar)()` prints `x(yz)Cstar`.

Basis names are copied into the expression and may contain up to 15
bytes. Names cannot be empty or begin with a null character; a later null
character terminates the copied name. Because leading whitespace and
parentheses belong to the parser grammar, names cannot begin with one of those
characters or with a double quote. A name also cannot begin with a single
or doubled backslash, though a doubled backslash may occur later in the name.
A visible name longer than 15 bytes throws `std::length_error`; an
invalid name throws `std::invalid_argument`.

Every successful `basis(...)` call also registers that name with the text
parser. User-defined names may be redefined.
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

`color_step_html` performs the same single reduction, returns the uncolored
resulting expression, and prints the before and after expressions as a
two-line HTML fragment. Its output defaults to `std::cout`; a different
stream can be passed as the second argument. Like `single_step`, it also
accepts `basis_step` as a second argument; when supplying both, pass the
output stream before `basis_step`.

For native terminal output, include
`<combdsl/color_step_ansi.hpp>` and call `color_step_ansi` instead.
It has the same arguments and return value, but uses the bundled
`fmt/color.h` to emit true-color ANSI escape sequences with white text on the
colored backgrounds. Because the function intentionally produces terminal
control sequences even when passed a file or string stream, the caller should
only select it when output to an ANSI-capable terminal is wanted.

For the redex selected by that step, the first required argument is shown red,
the second tunic green (`#00cc00`), the third blue, the fourth dark orange
(`#ff8c00`), and the fifth Munsell purple (`#cc00ff`). This applies to `S`, `K`,
`I`, `Y`, deferred recursive `Y` nodes, and named bases with positive arity.
Only the first five required arguments are colored; trailing arguments and
additional basis arguments remain uncolored. The wrappers follow their
arguments into the after expression, so an argument keeps its color when it is
moved or duplicated and disappears when it is discarded. Colors are
recomputed for each call, including when the selected redex is nested.
With `basis_step` enabled, a named-basis expansion is the exception: only the
basis name before the step and its stored contents after the step are colored,
both red. Required and trailing arguments, along with surrounding expression
context, remain uncolored.

The emitted fragment expects these styles in its containing HTML document:

```html
<style type="text/css">
.wor { background-color: red; color: white; }
.wog { background-color: #00cc00; color: white; }
.wob { background-color: blue; color: white; }
.woo { background-color: #ff8c00; color: white; }
.wop { background-color: #cc00ff; color: white; }
</style>
```

Each colored argument uses a matching `span` wrapper. Ordinary lexical
separators are still emitted where needed for multicharacter names. Expression
text is HTML-escaped, including `&`, `<`, `>`, `"`, and `'`, while the fixed
markup remains HTML. Repeated calls intentionally print the shared boundary
expression twice. Because the next call colors a new redex, the two copies can
have different markup even though they represent the same expression:

```cpp
auto next = color_step_html(expression);
//   S<span class="wor">K</span><span class="wog">I</span><span class="wob">x</span>
// -><span class="wor">K</span><span class="wob">x</span>(<span class="wog">I</span><span class="wob">x</span>)

next = color_step_html(std::move(next));
//   K<span class="wor">x</span><span class="wog">(Ix)</span>
// -><span class="wor">x</span>
```

Trailing operands are preserved. Unknown and undersaturated heads are skipped
while the search continues through their explicit subexpressions. `S`, `K`,
`I`, `Y`, deferred recursive `Y` nodes, and saturated named bases can all
reduce in nested positions. `Y` exposes its recursive operand without forcing
it. By default, a saturated basis with positive arity applies its declared
reduction as one step while leaving its operands unevaluated. Pass `true` as
the second argument to `single_step` to make expansion of the stored
definition a separate step instead. Zero-arity bases retain their existing
expansion behavior in either mode:

```cpp
single_step(quote(Y)(x))();       // x<deferred Y(x)>
single_step(quote(M)(x))();       // xx
single_step(quote(M)(x), true)(); // SIIx
```

Quoted application nodes are immutable and shared, so an `S` reduction can
reuse the same quoted operand even when it owns a move-only value.

`eval` repeatedly applies this reduction technique and prints the resulting
expression as one line when no eligible reduction remains. A partially
applied `K`, such as `K(Ix)`, is treated as opaque, so its stored argument is
not reduced before `K` receives the argument that determines whether the
stored value is needed. The single-step functions still allow that stored
argument to be explored explicitly. `eval` does not print intermediate
expressions or a status label during an uninterrupted evaluation, and its
output defaults to `std::cout`:

```cpp
eval(quote(S)(K)(I)(x)); // prints: x
```

A different output stream can be supplied as the second argument, and an
input stream can be supplied as the third. The input defaults to `std::cin`.
An optional trailing `basis_step` boolean selects the same named-basis
behavior for every reduction performed by `eval`. A five-argument overload
accepts an `evaluation_progress_callback`, which receives the accumulated
reduction count after every completed reduction.
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
`std::cin`. `single_step_loop` and `single_step_run` also accept an optional
trailing `basis_step` boolean:

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
left-associative, parentheses group expressions, and whitespace separates
adjacent tokens:

```cpp
auto expression = parse(" S K (I) x "); // SKIx
single_step_run(expression);

parse_and_step("K (I x) y");
// Ix
// x

parse_and_key_step("K (I x) y");
// waits for Enter before printing each reduction

parse_eval("K (I x) y"); // prints: x
```

At the start of a line, optionally preceded by whitespace, `set` followed by
whitespace defines and registers a named basis. An optional decimal arity can
appear after `=` and before the stored expression; when omitted, it defaults
to `0`:

```cpp
parse("set Double = 1 S(I)(I)");      // Double
parse_eval("set Twice = 1 S(I)(I)");  // registers Twice; prints nothing
single_step(parse("Double x"));       // xx
single_step(parse("Double x"), true); // SIIx
parse("set Alias = I");               // Alias; arity defaults to 0
```

Whitespace before `set` and around `=` is optional, while at least one
whitespace character must separate `set` from the name and an explicit arity
from its expression. The declaration acts like
`basis(name, arity, expression)`: names use the normal basis restrictions,
and `S`, `K`, `I`, and `Y` retain their
primitive meanings. Because a leading decimal token followed by whitespace is
treated as the arity, parenthesize a numeric basis name when it begins the
stored expression. `parse_eval`, `read_parse_eval`, `parse_and_step`, and
`parse_and_key_step` register a declaration without evaluating or stepping its
stored expression, and produce no output for the declaration itself. A
malformed declaration does not register its name.

At the start of a line, preceded by optional whitespace, `define` followed
by whitespace creates a named basis
by abstracting one or more lowercase symbols from a combinator expression.
The symbols may be adjacent or separated by whitespace. Their count becomes
the basis arity, and abstraction proceeds from the last symbol back to the
first. For a one-character basis name, the separating space may be omitted;
all following lowercase letters before `=` become symbols. A space before the
symbols keeps the preceding token as a multicharacter name:

```cpp
parse("define Flip xy = yx");            // Flip
single_step(parse("Flip a b"), true);    // Tab
single_step(parse("Flip a b"));          // ba

parse("define Apply xyz = xz(yz)");      // Apply
single_step(parse("Apply a b c"), true); // Sabc
single_step(parse("Apply a b c"));       // ac(bc)

parse("define Gx = xSTK(KK)(SK)");       // G
parse("define Gxyz = x(yz)");            // G
parse("define Gx y = y");                // Gx
```

Under the hood, `define` uses contextual `takeout` passes. For `define foo xyz = exp`,
the first pass takes out `z` with `x`, `y`, and recursive `foo` pending. The `y` pass
similarly has `x` and `foo` pending, while the `x` pass has only `foo` pending.
If the definition is recursive, the final `foo` pass has no pending atoms.
The resulting expression has arity
`3`. As with `set`, the definition command is silent under `parse_eval`,
`read_parse_eval`, and `parse_and_step` or `parse_and_key_step`, and malformed
definitions are not registered.

Before `takeout`, saturated named bases in the expression are applied,
including reachable saturated bases nested inside other applications.
Undersaturated bases remain named, and preprocessing does not enter
`K`-protected arguments or `Y` recursion boundaries. For example,
`define foo xyz = C(CB)xyz` first reduces the body to `x(yz)`, so the stored
expression is `B`. If this preprocessing repeats an expression or exceeds its
reduction limit, `define` safely abstracts the original body instead.

The names `set`, `define`, `show`, `single`, `key`, `basis`, `colorize`,
`about`, `birds`, `help`, `quit`, and `exit` are reserved words and cannot be
used as names by either `set` or `define`.

Occurrences of the defined name in the combinator expression are recursive
references. If any remain after the argument symbols are abstracted, `define`
abstracts the recursive name, optimizes the result, and appends it to `Y`,
producing `Y(optimized_expression)`:

```cpp
parse("define Repeat x = x(Repeat x)"); // Repeat
parse_eval("show Repeat");              // prints: arity:1 Y
```

The resulting recursive transformation is
`optimize(Y(optimize(takeout(rec_func, previous_takeout_result))))`. The
optimization pass recursively replaces `C*T` with `V`, `BDD` with `E`, `BOM`
with `U`, `B(QT)R` with `F`, `B(QT)B` with `Q1`, `BT` with `Q3`, `BW` with
`W*`, `B W*` with `W**`, `BC` with `C*`, `B C*` with `C**`, `YO` with `Y`,
`BB` with `D`, `SBT` with `A`, `SR` with `H`, `QM` with `L`, `DC` with `G`,
`WC` with `N`, `WV` with `W1`, `WB` with `Z`, and `S(D(BQC))D` with `J`.

`search_for_xy_subexp(expression)` searches the unoptimized result of
contextually taking out `y` with `x` pending, then taking out `x` with no
pending atoms, over all 129,958 application trees containing from one through
eight `x` or `y` operands.
`search_for_xyz_subexp(expression)` similarly searches
the contextual passes `z` with `x` and `y` pending, `y` with `x` pending, then
`x` with no pending atoms, over all 3,137,844 application trees containing
from one through eight `x`, `y`, or `z` operands. Both functions look for
`expression` at the root, at the head, or in any application subexpression and
stop at the first match. A match contains
`source_expression`, `takeout_result`, and `examined_expression_count`; an
empty result means every candidate was examined.
`search_for_subexp(expression)` tries the 129,958-candidate `xy` search first
and, only if that fails, tries the 3,137,844-candidate `xyz` search. Its
`examined_expression_count` is cumulative across both searches. For example:

```cpp
auto first = search_for_subexp(parse("SBT"));       // source: x(yx)
auto fallback = search_for_subexp(parse("B(QT)B")); // source: x(zy)
auto none = search_for_subexp(parse("C(CB)"));      // std::nullopt
```

`check_for_match(combs, symbol_list, expression)` appends the quoted atoms in
`symbol_list` to `combs`, normalizes that application and `expression` using
the ordinary evaluator rules, and compares the resulting quoted expression
trees. `check_for_pairs_match(symbol_list, expression)` runs that check for all
808 ordered pairs, with repetition, made from the 29 combinators in Bird Info
other than `J` and `Y`, and returns every matching pair. Every pair headed by
`I`, along with `MM`, `MU`, `UM`, and `UU`, is excluded.
`check_for_trips_match(symbol_list, expression)` similarly checks 45,186
ordered trip applications in both the left-associated and right-associated
application-tree shapes. It skips `(AB)C` when `AB` is excluded and
independently skips `A(BC)` when `BC` is excluded. In the left-associated
`ABC` shape, it also skips every saturated `K<anything><anything>` expression;
the partially applied `K(BC)` right-associated shape remains eligible. It also
skips `SK<anything>` in the `ABC` shape while retaining `S(K<anything>)`, and
skips right-associated `I(BC)` because `I` is applied to the composite `BC`.
`check_for_quads_match(symbol_list, expression)` checks 3,228,466 eligible
quad applications across the five application trees `ABCD`, `AB(CD)`,
`A(BC)D`, `A(BCD)`, and `A(B(CD))`. It inherits the pair and trip exclusions
at every pair or trip subtree. Both pair subtrees in `AB(CD)` must be eligible,
every application of `I` is excluded even when its argument is composite, and
the `Kxx` and `SKx` exclusions are checked in both left-associated triplet
positions, `ABC` and `BCD`. The exhaustive quad search is not run
automatically; call `check_for_quads_match` explicitly when its potentially
minutes-long search is wanted.

```cpp
std::array const symbols{
    quoted_atomic{x}, quoted_atomic{y},
    quoted_atomic{z}, quoted_atomic{w},
};
auto target = quote(x)(y)(quote(x)(w)(z));
auto one_match = check_for_match(I(J), symbols, target); // true
auto pair_matches = check_for_pairs_match(symbols, target); // empty
auto trip_matches = check_for_trips_match(
    std::span<quoted_atomic const>{},
    quote(A)(quote(A)(A))); // starts with AAA and A(AA)
// Run only when the exhaustive search is wanted:
auto quad_matches = check_for_quads_match(symbols, target);
```

`AB` and `BA` are separate candidates. For each remaining triplet, both
`(AB)C`, printed as `ABC`, and `A(BC)` are included and are not deduplicated
even if they normalize to the same result. The pair, trip, and quad pools omit
`J` to avoid trivial Jay matches and omit `Y` to avoid recursive candidates;
`check_for_match` itself accepts either combinator.
The `I`-headed exclusions avoid redundant matches because `Ix` reduces to
`x`. The fixed `MM`, `MU`, `UM`, and `UU` exclusions avoid pairs that do not
reach normal form under the matcher's bounded normalization. This trip and
quad subtree pruning checks every position for all four pairs and is
deliberately heuristic because surrounding application context can make an
excluded pair normalize.
Each normalization is limited to
`check_for_match_reduction_limit` reductions, currently 256, and also stops on
a repeated or excessively large expression. A stopped normalization is treated
as a non-match, preventing a divergent candidate from blocking the remaining
candidates.
Native `check_for_trips_match` and `check_for_quads_match` dynamically
distribute work among up to `std::thread::hardware_concurrency()`
`std::jthread` workers. Matches are recorded by original candidate index and
collected in index order, preserving the sequential result order. The quad
search constructs candidates on demand rather than retaining millions of
expression trees. Emscripten builds retain the single-threaded searches.

Names created with `set` or `define` may be redefined. A changed definition
replaces the user-defined basis for future parsing; expressions parsed earlier
retain the basis snapshot they already contain. Repeating an equivalent arity
and stored expression makes no change. The fundamental names `S`, `K`, `I`,
and `Y`, and every pre-defined basis registered by C++ `basis(...)`, are
immutable; attempting to redefine one is a parse error. A later C++ basis
registration cannot take a name that is already user-defined.

At the start of a line, optionally preceded by whitespace, `show` followed by
whitespace and a name displays the basis's arity followed by one level of its
stored definition, without reducing it. For `S`, `K`, `I`, or `Y`, it reports
that the name is fundamental and also gives its arity.
Anything other than a named basis or one of those four fundamental names is a
parse error:

```cpp
parse_eval("show M");   // prints: arity:1 SII
parse_eval("show S");   // prints: S is a fundamental name with arity:3
parse_eval("show x");   // parse error: x is not a defined name
```

`parse("show M")` returns the arity-and-definition display as a
`quoted_expression`. `parse_eval`, `read_parse_eval`, `parse_and_step`, and
`parse_and_key_step` print that result once without evaluating or stepping it.

`set_list()` returns the chronological history of successful user definitions
as newline-separated `set` or `define` declarations. Changed redefinitions are
included so replay preserves definitions that captured earlier basis
snapshots; equivalent repetitions are omitted. A `set` declaration always
includes its arity, including `0`; a `define` declaration includes its defining
symbols. Quotes and backslashes appear exactly as a user would enter them.
Passing each line through `input_escape` and then to `parse` recreates the
definitions:

```cpp
auto definitions = set_list();
std::istringstream input(definitions);
for (std::string line; std::getline(input, line);) {
    static_cast<void>(parse(input_escape(line)));
}
```

Definitions made by C++ `basis(...)` calls and rejected attempts to replace
pre-defined bases are not included.

`parse_eval` parses a string and passes the resulting quoted expression to
`eval`. Its output and input streams can be supplied as the second and third
arguments; they default to `std::cout` and `std::cin`. A trailing
`basis_step` boolean is forwarded by `parse_eval`, `read_parse_eval`, and
`parse_and_step` or `parse_and_key_step`. A five-argument `parse_eval` overload
also forwards an `evaluation_progress_callback` to `eval`.

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
When standard output is a terminal, it first prints
`Combinator Read-Eval-Print Loop, version 2.1.1`. Long evaluations display
the accumulated step count every 1,000 reductions by overwriting one status
line; the line is cleared before evaluation output is printed. Its interactive
prompt is `>`. Interactive input uses GNU Readline, so previous nonempty
commands can be recalled with Up Arrow or Ctrl-P. Redirected output contains
no progress status. Enter
`single step` or `single step on` to print every subsequent reduction, and
enter `single step off` to return to printing only the final result. Enter
`key step` or `key step on` to display the starting expression and wait for
one keypress before each reduction. Any key advances immediately without
requiring Enter, while `q` or `Q` ends the current reduction; `key step off`
disables that mode. When standard input is redirected from a file or pipe, all
three Key Step commands are silently ignored. Enabling either stepping mode
disables the other, while turning off an inactive mode leaves the active mode
unchanged. Omitting `on` or `off` enables the selected mode. `basis step`,
`basis step on`, and `basis step off` independently control whether
named-basis expansion is shown as a separate reduction in either stepping
mode; ordinary evaluation ignores this setting.
Enter `colorize`, `colorize on`, or `colorize off` to independently control
ANSI argument highlighting while either stepping mode is active. Colorized
stepping uses `color_step_ansi` and prints the final normal form without color
at the left margin. When Basis Step is on, a basis expansion colors only the
basis name before the step and its stored contents after the step, both red;
all arguments remain uncolored. Ordinary evaluation ignores this setting.
The mode commands themselves produce no output. Enter `help` or `help brief`
to display the command summary; enter `help full` to display detailed help.
Enter `birds` to list every bird and its
reduction rule. The list uses three
columns when their
calculated widths fit in 80 characters and otherwise uses two. Enter `about`
to print CREPL's About text wrapped to 80-character lines; its first line is
the CREPL banner.
Enter `quit` or `exit` with optional surrounding whitespace to end CREPL.
Running `crepl --version` prints the same About text and exits successfully
without starting the interactive loop.

`S`, `K`, `I`, and `Y` are reserved combinators. A single-character name
registered by `basis(...)` parses the same way, so `Mx` means `M` applied to
`x`. A multi-character registered name that does not end in a lowercase ASCII
letter may also be followed immediately by another operand, so `Q1xyz` means
`Q1 x y z` and prints in that same compact form. Names ending in `a` through
`z` still require whitespace,
parentheses, or an escaped-word opener as a delimiter. Thus `Cstar x` means
`Cstar` applied to `x`, while `Cstarx` is an unknown operand; it does not fall
back to `C` followed by five symbols.

Whitespace also distinguishes an intended lowercase basis name from compact
symbols. An unregistered run of two or more lowercase letters is an unknown
operand when whitespace separates it from another operand in the same
expression or parenthesized group. Consequently, `x foo y`, `x(foo y)`, and
`x(y foo)` are errors unless `foo` is registered. In `x(foo)y`, an exact
registered `foo` still wins; otherwise the run becomes the three symbols `f`,
`o`, and `o`. Leading or trailing whitespace used only as padding does not
make a run a basis name. Neither does whitespace used to delimit a preceding
multi-character basis or word operand; whitespace after a multi-character
basis remains a token separator even when it is optional. Everywhere else,
each lowercase ASCII letter (`a` through `z`) becomes a single-character
symbol, so compact primitive and symbol expressions such as `SKIx` remain
valid.

In escaped parser input, the two characters `\"` open or close one raw word,
and `\\` represents one backslash. This input:

```text
x \"word\" y \"mid\\dle\" z
```

parses and prints as `x word y mid\dle z`. Spaces, parentheses, UTF-8 bytes,
and other ordinary bytes between the word delimiters are content rather than
parser syntax. Outside a word, `\\` is a one-character symbolic backslash
operand. A doubled backslash may also occur later in a registered basis name,
where registered-name matching consumes it as part of that name. If that name
ends outside `a` through `z`, an adjacent operand may follow without a
delimiter. Any other sequence beginning with a backslash is rejected, as are
empty or unterminated words. A word whose spelling matches a primitive or
registered basis remains raw.

Malformed or empty input throws `combdsl::parse_error`. Its `position()` is the
zero-based byte position of the error; an error at the end of input reports the
input length. Its human-readable `what()` message displays that position
one-based, so the first byte is position 1 and the end of input is the input
length plus 1. `parse_and_step` uses the same `std::cout` and `std::cin`
defaults, including the same Ctrl-C pause behavior, as `single_step_run`.
`parse_and_key_step` uses those stream defaults with `single_step_loop`.

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
    cmake --build build-browser --target combdsl_browser_docs
```

The `combdsl_browser_docs` target builds the WebAssembly application and
refreshes the tracked browser files in `docs`. Its WebAssembly heap starts at
64 MiB and may grow to 4 GiB, subject to browser and device limits. Serve those
static files with any HTTP server and open
`http://localhost:8000/`:

```sh
python3 -m http.server 8000 --directory docs
```

The web page is static and does not use `emrun`.
Loading through `file://` is not supported because
the page starts a Web Worker and fetches `combdsl.wasm`; opening the file
directly displays the required HTTP-server command. Cancel terminates and
recreates the worker, so a non-normalizing expression does not freeze the page.

The Combinator Expression box is a scrollable history with the current editable
input at the bottom. Successful commands and expressions that reach normal form
remain visible in submission order. Cancelled and timed-out expressions are
retained with ` [cancelled]` and ` [timed out]` appended, respectively. Parse
errors are not added to the history. A successfully registered
`set` command leaves only that submitted definition line, with no output
beneath it.

The Single Step button switches between displaying only the evaluated result
and displaying every reduction produced by
`single_step_run(parse(input_escape(source)))`. Evaluations accumulate in the
results area with a blank line between them.

The Key Step button starts a
manual reduction session: after submitting an expression, each ordinary
keypress performs exactly one `single_step`. The keypress that performs the
final reduction also ends the session; Cancel ends it at any time. The Single
Step and Key Step modes are mutually exclusive.

The independent Basis Step
button controls whether either stepping mode exposes a saturated named basis
definition as a separate step. With Basis Step off, `Mx` goes directly to
`xx`; with it on, the first step is `SIIx`.

While either stepping mode is
active, the Colorize button uses `color_step_html` to highlight the first, second,
third, fourth, and fifth arguments of each reduction in red, tunic green, blue,
dark orange, and Munsell purple and carries those highlights into the reduced
result. With Basis Step on, a basis expansion instead colors only the basis
name before the step and its stored contents after the step, both red; all
arguments remain uncolored. After the final colorized reduction, the normal
form is printed without color at the left margin. The browser prints the
submitted starting expression immediately, then appends the output beneath it.

The Cancel button is active when not stepping or when Single Step is on.
Clicking on it aborts a long-running or infinite-looping reduction.

The Save button downloads all successfully registered user
definitions and redefinitions as `set_list.cmb`, with explicit arities and
user-facing quoting that can be entered again. If there are no user-defined
bases, Save opens a dialog that says `Nothing to save` instead. When a command
entered in the browser would change a user definition, a confirmation dialog
shows `About to replace name=arity expression`; Cancel preserves the existing
definition, while Replace is initially focused so Enter confirms it.

The Load
button opens a file picker filtered for `.cmb` files and recreates those
definitions in file order by applying `parse(input_escape(record))` to each
saved record; file redefinitions are applied silently and the expressions are
not evaluated. A parser failure is reported with the file name, one-based line
number, and one-based byte position. Loading continues with the next line after
a parse error. It stops after the fifteenth parse error and reports
`Too many errors, aborting with no changes made`. If any error is found, the
entire load is rolled back, so no definitions from that file are kept. Failed
loads below the cutoff report
`Errors are preventing any changes from being made`; a load aborted at the
cutoff displays only the `Too many errors` status after its diagnostics.

Basis Step and Colorize may remain on when neither stepping mode is active;
ordinary evaluation ignores both settings. Cancelling an evaluation appends
`[cancelled]` beneath its starting expression. During automatic evaluation,
the worker sends its accumulated reduction count about every 100 milliseconds.
If a heartbeat is missing for one second, the watchdog stops and replaces the
worker, then appends
`[timed out after more than nnn steps]`, where `nnn` is the last reported
accumulated reduction count. Automatic worker failure uses the same timeout
message; `[cancelled]` is reserved for user cancellation.

The Help button
summarizes the stepping, definition, saving, and loading options in a
scrollable dialog.

The Bird Info button displays the names and reductions of all the pre-defined
bird combinators.

The About button displays information on copyright and redistribution.

For another CMake project, link the interface target after adding this project:

```cmake
add_subdirectory(path/to/combDSL)
target_link_libraries(your_target PRIVATE combdsl::combdsl)
```
