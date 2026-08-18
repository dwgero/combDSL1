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

import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import test from "node:test";
import vm from "node:vm";

const sourceUrl = new URL(
    "../web/input_history.js", import.meta.url);
const context = vm.createContext({});
new vm.Script(readFileSync(sourceUrl, "utf8"), {
    filename: sourceUrl.pathname,
}).runInContext(context);

const createHistory = context.combdslInputHistory.create;
const createCommandCompleter =
    context.combdslInputHistory.createCommandCompleter;

test("completes unique Studio command prefixes", () => {
    const complete = createCommandCompleter([
        "abstract",
        "compare",
        "define",
        "depends on",
        "depends-on",
        "dependson",
        "find",
        "inspect",
        "references",
        "remove",
        "revisions",
        "set",
        "show",
        "step limit",
        "used by",
        "used-by",
        "usedby",
    ]);

    assert.equal(complete("abs"), "abstract");
    assert.equal(complete("com"), "compare");
    assert.equal(complete("def"), "define");
    assert.equal(complete("depends o"), "depends on");
    assert.equal(complete("depends-"), "depends-on");
    assert.equal(complete("dependso"), "dependson");
    assert.equal(complete("fin"), "find");
    assert.equal(complete("ins"), "inspect");
    assert.equal(complete("re"), undefined);
    assert.equal(complete("ref"), "references");
    assert.equal(complete("rem"), "remove");
    assert.equal(complete("rev"), "revisions");
    assert.equal(complete("se"), "set");
    assert.equal(complete("sho"), "show");
    assert.equal(complete("step l"), "step limit");
    assert.equal(complete("used b"), "used by");
    assert.equal(complete("used-"), "used-by");
    assert.equal(complete("usedb"), "usedby");
});

test("does not complete ambiguous, exact, or argument text", () => {
    const complete = createCommandCompleter([
        "define",
        "set",
        "show",
    ]);

    assert.equal(complete("s"), undefined);
    assert.equal(complete("show"), undefined);
    assert.equal(complete("show M"), undefined);
    assert.equal(complete("Mx"), undefined);
    assert.equal(complete(""), undefined);
    assert.equal(complete("   "), undefined);
});

test("can append a command delimiter to an exact match", () => {
    const complete = createCommandCompleter([
        "define",
        "set",
        "show",
    ], {appendSpaceToExact: true});

    assert.equal(complete("se"), "set ");
    assert.equal(complete("set"), "set ");
    assert.equal(complete("  sho"), "  show ");
    assert.equal(complete("show "), undefined);
    assert.equal(complete("show M"), undefined);
});

test("completes the remove command with its delimiter", () => {
    const complete = createCommandCompleter([
        "define",
        "remove",
        "set",
        "show",
    ], {appendSpaceToExact: true});

    assert.equal(complete("rem"), "remove ");
    assert.equal(complete("remove"), "remove ");
    assert.equal(complete("  rem"), "  remove ");
    assert.equal(complete("remove "), undefined);
});

test("completes the revisions command with its delimiter", () => {
    const complete = createCommandCompleter([
        "references",
        "remove",
        "revisions",
    ], {appendSpaceToExact: true});

    assert.equal(complete("re"), undefined);
    assert.equal(complete("rev"), "revisions ");
    assert.equal(complete("revisions"), "revisions ");
    assert.equal(complete("  rev"), "  revisions ");
    assert.equal(complete("revisions "), undefined);
    assert.equal(complete("revisions Foo"), undefined);
});

test("completes dependency commands with their delimiter", () => {
    const completeTopLevel = createCommandCompleter([
        "depends on",
        "depends-on",
        "dependson",
        "used by",
        "used-by",
        "usedby",
    ], {appendSpaceToExact: true});
    const completeAll = createCommandCompleter([
        "depends on all",
        "depends-on all",
        "dependson all",
        "used by all",
        "used-by all",
        "usedby all",
    ], {appendSpaceToExact: true});
    const completePath = createCommandCompleter([
        "used by path",
        "used-by path",
        "usedby path",
    ], {appendSpaceToExact: true});
    const completeBetween = createCommandCompleter([
        "used by path between",
        "used-by path between",
        "usedby path between",
    ], {appendSpaceToExact: true});
    const complete = source =>
        completeTopLevel(source) ?? completeAll(source) ??
            completePath(source) ?? completeBetween(source);

    assert.equal(complete("dependso"), "dependson ");
    assert.equal(complete("depends-"), "depends-on ");
    assert.equal(complete("depends o"), "depends on ");
    assert.equal(complete("depends   on"), "depends   on ");
    assert.equal(complete("usedb"), "usedby ");
    assert.equal(complete("used-by"), "used-by ");
    assert.equal(complete("  used\tb"), "  used\tby ");
    assert.equal(complete("used by"), "used by ");
    assert.equal(complete("depends"), undefined);
    assert.equal(complete("used"), undefined);
    assert.equal(complete("dependson a"), "dependson all ");
    assert.equal(complete("depends-on all"), "depends-on all ");
    assert.equal(complete("depends on a"), "depends on all ");
    assert.equal(complete("usedby a"), "usedby all ");
    assert.equal(complete("used-by all"), "used-by all ");
    assert.equal(complete("used by a"), "used by all ");
    assert.equal(complete("usedby p"), "usedby path ");
    assert.equal(complete("used-by path"), "used-by path ");
    assert.equal(complete("used by p"), "used by path ");
    assert.equal(
        complete("usedby path b"),
        "usedby path between ");
    assert.equal(
        complete("used-by path between"),
        "used-by path between ");
    assert.equal(
        complete("  used\tby  path  b"),
        "  used\tby  path  between ");
    assert.equal(
        complete("  depends\ton  a"),
        "  depends\ton  all ");
    assert.equal(complete("depends on Foo"), undefined);
    assert.equal(complete("usedby all Foo"), undefined);
    assert.equal(complete("usedby path Foo"), undefined);
});

test("completes phrases while preserving multiple whitespace", () => {
    const complete = createCommandCompleter([
        "basis step",
        "key step",
        "single step",
    ]);

    assert.equal(complete("key   st"), "key   step");
    assert.equal(complete("  ke\tst  "), "  key\tstep  ");
    assert.equal(complete("key   "), "key   step");
});

test("completes the show all command form", () => {
    const complete = createCommandCompleter(["show all"]);

    assert.equal(complete("show a"), "show all");
    assert.equal(complete("show   "), "show   all");
});

test("completes captured and live definition modifiers", () => {
    const complete = createCommandCompleter(
        [
            "define captured",
            "define live",
            "set captured",
            "set live",
        ],
        {appendSpaceToExact: true});

    assert.equal(complete("define c"), "define captured ");
    assert.equal(complete("define l"), "define live ");
    assert.equal(complete("set c"), "set captured ");
    assert.equal(complete("set l"), "set live ");
    assert.equal(
        complete("  define\tcapt"),
        "  define\tcaptured ");
    assert.equal(complete("define s"), undefined);
    assert.equal(complete("define "), undefined);
    assert.equal(complete("define foo"), undefined);
});

test("completes references commands and options", () => {
    const completeTopLevel = createCommandCompleter(
        ["references"], {appendSpaceToExact: true});
    const completeOption = createCommandCompleter([
        "references captured",
        "references live",
    ]);
    const complete = source =>
        completeTopLevel(source) ?? completeOption(source);

    assert.equal(complete("ref"), "references ");
    assert.equal(complete("references"), "references ");
    assert.equal(complete("references cap"), "references captured");
    assert.equal(complete("references live"), undefined);
    assert.equal(
        complete("  references\tcap"),
        "  references\tcaptured");
    assert.equal(complete("references l"), "references live");
    assert.equal(complete("references "), undefined);
});

test("completes the required step limit command and off option", () => {
    const completeTopLevel = createCommandCompleter(
        ["step limit"], {appendSpaceToExact: true});
    const completeOption = createCommandCompleter([
        "step limit off",
    ]);
    const complete = source =>
        completeTopLevel(source) ?? completeOption(source);

    assert.equal(complete("step l"), "step limit ");
    assert.equal(complete("step   lim"), "step   limit ");
    assert.equal(complete("step limit"), "step limit ");
    assert.equal(complete("step limit o"), "step limit off");
    assert.equal(
        complete("  step\tlimit  o"),
        "  step\tlimit  off");
    assert.equal(complete("step limit "), "step limit off");
    assert.equal(complete("step limit 25"), undefined);
});

test("keeps references as a typed command without a UI button", () => {
    const html = readFileSync(
        new URL("../web/index.html", import.meta.url), "utf8");

    assert.match(html, /references &lt;captured \| live&gt;/);
    assert.match(
        html,
        /sole first\s+revision always prints as the bare name, even when stored,\s+inspected, explicitly entered as\s+<code>name@1<\/code>, or retained after removal/);
    assert.doesNotMatch(
        html,
        /<button\b[^>]*\bid=["']references["']/i);
});

test("keeps revisions as a typed command without a UI button", () => {
    const html = readFileSync(
        new URL("../web/index.html", import.meta.url), "utf8");

    assert.match(html, /<code>revisions name<\/code>/);
    assert.doesNotMatch(
        html,
        /<button\b[^>]*\bid=["']revisions["']/i);
});

test("keeps inspect as a typed command without a UI button", () => {
    const html = readFileSync(
        new URL("../web/index.html", import.meta.url), "utf8");

    assert.match(html, /<code>inspect expression<\/code>/);
    assert.doesNotMatch(
        html,
        /<button\b[^>]*\bid=["']inspect["']/i);
});

test("keeps compare as a typed command without a UI button", () => {
    const html = readFileSync(
        new URL("../web/index.html", import.meta.url), "utf8");

    assert.match(
        html,
        /<code>compare \?symbol_list left_expression =\s+right_expression<\/code>/);
    assert.doesNotMatch(
        html,
        /<button\b[^>]*\bid=["']compare["']/i);
});

test("documents compact restricted Find catalogs in Studio", () => {
    const html = readFileSync(
        new URL("../web/index.html", import.meta.url), "utf8");

    assert.match(
        html,
        /find \[all\] \[&lt;num&gt; \| among &lt;bird&gt;\.\.\.\]/);
    assert.match(html, /Whitespace between names is optional/);
    assert.match(
        html,
        /whitespace before the question mark remains required/);
    assert.match(html, /exact whole name or revision wins/);
    assert.match(
        html,
        /longest valid bird name or revision is taken from/);
    assert.match(html, /A basis name cannot begin with <code>"<\/code>,/);
    assert.match(html, /cannot end\s+with\s+<code>@<\/code>/);
    assert.match(html, /cannot contain\s+<code>\)<\/code> or <code>\(<\/code> anywhere/);
    assert.match(html, /cannot be a\s+non-negative integer literal/);
    assert.match(
        html,
        /Catalog\s+items are resolved sequentially/);
    assert.match(html,
        /a\s+<code>\?symbols=<\/code> spelling is the query marker unless the\s+longest usable compact prefix reaches through that <code>=<\/code>/);
    assert.match(html,
        /a shorter registered prefix does not claim it/);
    assert.match(html,
        /<code>find among S K \?x= \?y= \?z= \?y=<\/code> uses those four birds/);
    assert.match(html,
        /its match is <code>\?=K \?y=<\/code>/);
    assert.match(html,
        /If <code>\?x=<\/code> is\s+absent, <code>find among \?x=xx<\/code> has an empty catalog/);
    assert.match(html,
        /if it is\s+present, that bird is consumed and the unresolved <code>xx<\/code>\s+is not a marker/);
});

test("documents special question names and ordinary ampersand names in Studio", () => {
    const html = readFileSync(
        new URL("../web/index.html", import.meta.url), "utf8");

    assert.match(html, /An equals sign or question mark may begin a basis name/);
    assert.match(html, /set = = 3 C/);
    assert.match(html, /set =bar = 3 C/);
    assert.match(html, /set = 3 C/);
    assert.match(html, /define = bar = rab/);
    assert.match(html, /set \? = 3 C/);
    assert.match(html, /set \?bar = 3 C/);
    assert.match(html, /set \? 3 C/);
    assert.match(html, /define \? bar = rab/);
    assert.match(html, /ampersand follows the ordinary non-alphanumeric name rules/);
    assert.match(html, /set &amp;=3 C/);
    assert.match(html, /set &amp;bar=3 C/);
    assert.match(html, /set &amp;foo=bar/);
    assert.match(html, /defines\s+<code>&amp;foo<\/code>, not <code>&amp;foo=bar<\/code>/);
    assert.match(
        html,
        /required <code>\?symbols<\/code> marker keeps its\s+contextual meaning/);
});

test("documents leading-backslash basis names in Studio", () => {
    const html = readFileSync(
        new URL("../web/index.html", import.meta.url), "utf8");

    assert.match(html, /A backslash may also begin a basis name/);
    assert.match(html, /set \\ = 3 C/);
    assert.match(html, /set \\foo = 1 I/);
    assert.match(html, /define \\ bar = rab/);
    assert.match(html, /exact registered\s+backslash-prefixed name takes precedence/);
    assert.match(html, /quoted raw word <code>"\\"<\/code>/);
    assert.match(html, /each visible backslash\s+occupy two bytes/);
    assert.match(html, /doubled stored spelling with safe\s+operand separators/);
});

test("completes unambiguous abstract command forms", () => {
    const completers = [
        "abstract ?",
        "abstract ministeps ?",
        "abstract steps ?",
    ].map(phrase => createCommandCompleter([phrase]));
    const complete = source => {
        for (const completer of completers) {
            const completed = completer(source);
            if (completed !== undefined) {
                return completed;
            }
        }
        return undefined;
    };

    assert.equal(complete("abstract "), "abstract ?");
    assert.equal(complete("abstract   "), "abstract   ?");
    assert.equal(complete("abstract s"), "abstract steps ?");
    assert.equal(
        complete("abstract m"),
        "abstract ministeps ?");
    assert.equal(
        complete("  abstract\tsteps  "),
        "  abstract\tsteps  ?");
    assert.equal(
        complete("  abstract\tministeps  "),
        "  abstract\tministeps  ?");
    assert.equal(complete("abstract x"), undefined);
    assert.equal(complete("abstract ?x"), undefined);
});

test("completes unambiguous find command forms", () => {
    const completers = [
        "find ?",
        "find all ?",
        "find 1 ?",
        "find all 1 ?",
        "find 2 ?",
        "find all 2 ?",
        "find 3 ?",
        "find all 3 ?",
        "find 4 ?",
        "find all 4 ?",
    ].map(phrase => createCommandCompleter([phrase]));
    const complete = source => {
        for (const completer of completers) {
            const completed = completer(source);
            if (completed !== undefined) {
                return completed;
            }
        }
        return undefined;
    };

    assert.equal(complete("find "), "find ?");
    assert.equal(complete("find   "), "find   ?");
    assert.equal(complete("find a"), "find all ?");
    for (let size = 1; size <= 4; ++size) {
        assert.equal(
            complete(`find ${size}`),
            `find ${size} ?`);
        assert.equal(
            complete(`find all ${size}`),
            `find all ${size} ?`);
    }
    assert.equal(
        complete("  find\tall  4  "),
        "  find\tall  4  ?");
    assert.equal(complete("find 5"), undefined);
    assert.equal(complete("find all 5"), undefined);
    assert.equal(complete("find ?x"), undefined);

    const completeAmong = createCommandCompleter(
        ["find among", "find all among"],
        {appendSpaceToExact: true});
    assert.equal(completeAmong("find a"), undefined);
    assert.equal(completeAmong("find am"), "find among ");
    assert.equal(
        completeAmong("find all am"),
        "find all among ");
});

test("normalizes extensible command phrase definitions", () => {
    const complete = createCommandCompleter([
        "  key    step  ",
        "key step",
    ]);

    assert.equal(complete("ke  st"), "key  step");
});

test("uses the parser's ASCII command whitespace", () => {
    const complete = createCommandCompleter(["key step"]);

    assert.equal(complete("key\u00a0st"), undefined);
    assert.equal(complete("key\vst"), "key\vstep");
});

const historyStorageKey = "combdsl.studio.input-history.v1";
const historyEntryPrefix = `${historyStorageKey}.entry.`;
const historyLegacyDeletionPrefix =
    `${historyStorageKey}.legacy-deleted.`;

const createMemoryStorage = (initial = {}) => {
    const items = new Map(Object.entries(initial).map(
        ([key, value]) => [String(key), String(value)]));
    const operations = [];
    return {
        get length() {
            return items.size;
        },
        key(index) {
            return [...items.keys()][index] ?? null;
        },
        getItem(key) {
            key = String(key);
            return items.has(key) ? items.get(key) : null;
        },
        setItem(key, value) {
            key = String(key);
            value = String(value);
            operations.push(["set", key, value]);
            items.set(key, value);
        },
        removeItem(key) {
            key = String(key);
            operations.push(["remove", key]);
            items.delete(key);
        },
        clear() {
            operations.push(["clear"]);
            items.clear();
        },
        entries() {
            return [...items.entries()];
        },
        operations,
    };
};

const storedHistoryEntries = storage => storage.entries()
    .filter(([key]) => key.startsWith(historyEntryPrefix))
    .map(([, value]) => JSON.parse(value))
    .sort((left, right) => left.id < right.id ? -1 : 1);

const setStoredHistoryEntry = (storage, entry) => {
    storage.setItem(
        historyEntryPrefix + encodeURIComponent(entry.id),
        JSON.stringify({version: 2, outcome: "", ...entry}),
    );
};

test("starts with an empty visible history", () => {
    const history = createHistory();

    assert.deepEqual([...history.values()], []);
});

test("retains successful sources in submission order", () => {
    const history = createHistory();
    assert.equal(history.record("Ix"), "Ix");
    assert.equal(history.record("show M"), "show M");
    assert.equal(
        history.record("set Foo = 1 I"),
        "set Foo = 1 I");

    assert.deepEqual([...history.values()], [
        "Ix",
        "show M",
        "set Foo = 1 I",
    ]);
});

test("preserves exact entries and suppresses adjacent duplicate sources", () => {
    const history = createHistory();
    history.record("  show M  ");
    history.record("Ix");
    assert.equal(history.record("Ix"), undefined);
    assert.equal(history.record("Ix", "cancelled"), undefined);
    history.record("Kx");
    history.record("Ix");

    assert.deepEqual([...history.values()], [
        "  show M  ",
        "Ix",
        "Kx",
        "Ix",
    ]);
});

test("does not persist an adjacent duplicate source", () => {
    const storage = createMemoryStorage();
    const history = createHistory({storage});

    history.record("YI", "cancelled");
    const operationsAfterFirstRecord = storage.operations.length;
    assert.equal(history.record("YI", "timed out"), undefined);
    assert.equal(storage.operations.length, operationsAfterFirstRecord);
    assert.deepEqual(storedHistoryEntries(storage).map(
        ({source, outcome}) => ({source, outcome})), [
        {source: "YI", outcome: "cancelled"},
    ]);
});

test("appends non-normal evaluation outcomes", () => {
    const history = createHistory();
    assert.equal(
        history.record("YI", "cancelled"),
        "YI [cancelled]");
    assert.equal(
        history.record("BKM(BKM)", "timed out"),
        "BKM(BKM) [timed out]");
    assert.equal(
        history.record("IIIx", "step limit"),
        "IIIx [step limit]");
    assert.deepEqual([...history.values()], [
        "YI [cancelled]",
        "BKM(BKM) [timed out]",
        "IIIx [step limit]",
    ]);
});

test("returns immutable history snapshots", () => {
    const history = createHistory();
    history.record("Ix");
    const snapshot = history.values();

    assert.throws(
        () => snapshot.push("Kx"),
        /not extensible|read only/i);
    history.record("Kx");
    assert.deepEqual([...snapshot], ["Ix"]);
    assert.deepEqual([...history.values()], ["Ix", "Kx"]);
});

test("persists and restores structured history entries", () => {
    const storage = createMemoryStorage();
    const history = createHistory({
        storage,
        maximumStoredEntries: 3,
    });
    history.record("Ix");
    history.record("YI", "cancelled");
    history.record("BKM(BKM)", "timed out");
    history.record("IIIx", "step limit");

    assert.deepEqual(JSON.parse(storage.getItem(historyStorageKey)), {
        version: 2,
        format: "entry-keys",
    });
    assert.deepEqual(storedHistoryEntries(storage).map(
        ({source, outcome}) => ({source, outcome})), [
            {source: "YI", outcome: "cancelled"},
            {source: "BKM(BKM)", outcome: "timed out"},
            {source: "IIIx", outcome: "step limit"},
        ]);
    assert.deepEqual(
        [...createHistory({
            storage,
            maximumStoredEntries: 3,
        }).values()],
        [
            "YI [cancelled]",
            "BKM(BKM) [timed out]",
            "IIIx [step limit]",
        ],
    );
});

test("recalls raw sources and restores the editable draft", () => {
    const history = createHistory();
    history.record("Ix");
    history.record("YI", "cancelled");
    history.record("BKM(BKM)", "timed out");

    assert.equal(history.previous("draft"), "BKM(BKM)");
    assert.equal(history.previous("ignored"), "YI");
    assert.equal(history.previous("ignored"), "Ix");
    assert.equal(history.previous("ignored"), undefined);
    assert.equal(history.next(), "YI");
    assert.equal(history.next(), "BKM(BKM)");
    assert.equal(history.next(), "draft");
    assert.equal(history.next(), undefined);

    history.resetNavigation();
    assert.equal(history.next(), undefined);
    assert.equal(history.previous("new draft"), "BKM(BKM)");
    assert.equal(history.next(), "new draft");
});

test("reports whether a recalled history entry is current", () => {
    const history = createHistory();
    history.record("A");
    history.record("B");
    history.record("C");

    assert.equal(history.hasCurrent(), false);
    assert.equal(history.previous("draft"), "C");
    assert.equal(history.hasCurrent(), true);
    assert.equal(history.next(), "draft");
    assert.equal(history.hasCurrent(), false);

    assert.equal(history.previous("draft"), "C");
    assert.equal(history.previous("ignored"), "B");
    assert.equal(history.hasCurrent(), true);
    const removedB = history.removeCurrent();
    assert.equal(removedB.index, 1);
    assert.equal(removedB.nextSource, "C");
    assert.equal(history.hasCurrent(), true,
        "the replacement entry remains current");
    const removedC = history.removeCurrent();
    assert.equal(removedC.index, 1);
    assert.equal(removedC.nextSource, "draft");
    assert.equal(history.hasCurrent(), false,
        "removing the newest entry returns to the live draft");

    assert.equal(history.previous("new draft"), "A");
    assert.equal(history.hasCurrent(), true);
    const removedA = history.removeCurrent();
    assert.equal(removedA.index, 0);
    assert.equal(removedA.nextSource, "new draft");
    assert.equal(history.hasCurrent(), false);
});

test("removes the current recalled entry and persists the deletion", () => {
    const storage = createMemoryStorage();
    const history = createHistory({storage});
    history.record("A");
    history.record("B", "cancelled");
    history.record("C");

    assert.equal(history.removeCurrent(), undefined);
    assert.equal(history.previous("draft"), "C");
    assert.equal(history.previous("ignored"), "B");

    const removedB = history.removeCurrent();
    assert.equal(removedB.index, 1);
    assert.equal(removedB.nextSource, "C");
    assert.deepEqual([...history.values()], ["A", "C"]);
    assert.deepEqual(storedHistoryEntries(storage).map(
        ({source, outcome}) => ({source, outcome})), [
        {source: "A", outcome: ""},
        {source: "C", outcome: ""},
    ]);

    const removedC = history.removeCurrent();
    assert.equal(removedC.index, 1);
    assert.equal(removedC.nextSource, "draft");
    assert.deepEqual([...history.values()], ["A"]);
    assert.equal(history.removeCurrent(), undefined);
    assert.equal(history.previous("new draft"), "A");

    const removedA = history.removeCurrent();
    assert.equal(Object.isFrozen(removedA), true);
    assert.equal(removedA.index, 0);
    assert.equal(removedA.nextSource, "new draft");
    assert.deepEqual([...history.values()], []);
    assert.equal(history.previous("ignored"), undefined);
    assert.equal(history.next(), undefined);
});

test("removes loaded history entries by their visible index", () => {
    const storage = createMemoryStorage({
        [historyStorageKey]: JSON.stringify({
        version: 1,
        entries: [
            {source: "A", outcome: ""},
            {source: "B", outcome: "timed out"},
        ],
        }),
    });
    const history = createHistory({storage});

    assert.equal(history.previous(""), "B");
    const removed = history.removeCurrent();
    assert.equal(removed.index, 1);
    assert.equal(removed.nextSource, "");
    assert.deepEqual([...history.values()], ["A"]);
    assert.deepEqual(storedHistoryEntries(storage).map(
        ({source, outcome}) => ({source, outcome})), [
        {source: "A", outcome: ""},
    ]);
});

test("operates on a line and resumes at its next history entry", () => {
    const history = createHistory();
    history.record("A");
    history.record("B");
    history.record("C");
    assert.equal(history.previous("draft"), "C");
    assert.equal(history.previous("ignored"), "B");

    const afterB = history.prepareOperateAndGetNext();
    history.record("B");
    assert.equal(history.resumeOperateAndGetNext(afterB), "C");

    const afterC = history.prepareOperateAndGetNext();
    history.record("C");
    assert.equal(history.resumeOperateAndGetNext(afterC), "B");
    assert.equal(
        history.resumeOperateAndGetNext(afterC), undefined);
});

test("operate and get next returns blank from a live draft", () => {
    const history = createHistory();
    history.record("A");
    history.resetNavigation();

    const operation = history.prepareOperateAndGetNext();
    history.record("draft");
    assert.equal(history.resumeOperateAndGetNext(operation), "");
});

test("operate and get next recalls an unannotated source", () => {
    const history = createHistory();
    history.record("A");
    history.record("B", "timed out");
    assert.equal(history.previous("draft"), "B");
    assert.equal(history.previous("ignored"), "A");

    const operation = history.prepareOperateAndGetNext();
    history.record("A");
    assert.equal(history.resumeOperateAndGetNext(operation), "B");
});

test("ignores malformed or unsupported stored history", () => {
    const from = value => createHistory({
        storage: createMemoryStorage({
            [historyStorageKey]: value,
        }),
    });

    assert.deepEqual([...from("not json").values()], []);
    assert.deepEqual([
        ...from(JSON.stringify({version: 2, entries: []})).values(),
    ], []);
    assert.deepEqual([
        ...from(JSON.stringify({
            version: 1,
            entries: [
                {source: "Ix", outcome: ""},
                {source: 17, outcome: "cancelled"},
                {source: "Kx", outcome: "invented"},
                null,
            ],
        })).values(),
    ], ["Ix"]);
});

test("repairs malformed stored history on the next record", () => {
    const storage = createMemoryStorage({
        [historyStorageKey]: "not json",
    });
    const history = createHistory({storage});
    history.record("Ix");

    assert.deepEqual(
        [...createHistory({storage}).values()],
        ["Ix"],
    );
});

test("merges additions from independent tabs without overwriting", () => {
    const storage = createMemoryStorage();
    const first = createHistory({
        storage,
        now: () => 1000,
        writerId: "first",
    });
    const second = createHistory({
        storage,
        now: () => 1000,
        writerId: "second",
    });

    first.record("A");
    second.record("B", "cancelled");
    assert.deepEqual([...second.values()], ["A", "B [cancelled]"]);

    const secondKey = storage.entries().find(([key, value]) =>
        key.startsWith(historyEntryPrefix) &&
        JSON.parse(value).source === "B")[0];
    const update = first.applyStorageEvent(secondKey, "ignored");
    assert.equal(Object.isFrozen(update), true);
    assert.equal(update.changed, true);
    assert.equal(update.currentRemoved, false);
    assert.deepEqual([...first.values()], ["A", "B [cancelled]"]);
    assert.equal(storedHistoryEntries(storage).length, 2);

    assert.equal(first.handlesStorageKey(null), true);
    assert.equal(first.handlesStorageKey(historyStorageKey), true);
    assert.equal(first.handlesStorageKey(secondKey), true);
    assert.equal(first.handlesStorageKey("unrelated"), false);
    assert.equal(
        first.applyStorageEvent("unrelated", null), undefined);
});

test("propagates deletions without stale-tab resurrection", () => {
    const storage = createMemoryStorage();
    const first = createHistory({
        storage,
        now: () => 2000,
        writerId: "first",
    });
    first.record("A");
    first.record("B");
    const second = createHistory({
        storage,
        now: () => 2000,
        writerId: "second",
    });
    const removedEntry = storedHistoryEntries(storage)
        .find(entry => entry.source === "B");
    const removedKey = historyEntryPrefix +
        encodeURIComponent(removedEntry.id);
    const removedValue = storage.getItem(removedKey);

    assert.equal(first.previous("first draft"), "B");
    first.removeCurrent();
    assert.deepEqual([...second.values()], ["A", "B"]);

    second.record("C");
    assert.deepEqual([...second.values()], ["A", "C"]);
    assert.equal(storage.getItem(removedKey), null);
    const lateOldEvent = second.applyStorageEvent(
        removedKey, removedValue);
    assert.equal(lateOldEvent.changed, false);
    assert.deepEqual([...second.values()], ["A", "C"]);

    first.synchronizeStorage();
    assert.deepEqual([...first.values()], ["A", "C"]);
    assert.deepEqual([...createHistory({storage}).values()], ["A", "C"]);
});

test("tombstones deleted migrated entries before removing them", () => {
    const legacyText = JSON.stringify({
        version: 1,
        entries: [
            {source: "A", outcome: ""},
            {source: "B", outcome: "timed out"},
        ],
    });
    const storage = createMemoryStorage({
        [historyStorageKey]: legacyText,
    });
    const history = createHistory({storage});
    const migratedB = storedHistoryEntries(storage)
        .find(entry => entry.source === "B");
    const entryKey = historyEntryPrefix +
        encodeURIComponent(migratedB.id);
    const staleEntryValue = storage.getItem(entryKey);

    assert.equal(history.previous("draft"), "B");
    const operationStart = storage.operations.length;
    history.removeCurrent();
    const deletionOperations = storage.operations.slice(operationStart);
    const tombstoneIndex = deletionOperations.findIndex(
        ([operation, key]) => operation === "set" &&
            key.startsWith(historyLegacyDeletionPrefix));
    const removalIndex = deletionOperations.findIndex(
        ([operation, key]) => operation === "remove" &&
            key === entryKey);
    assert.ok(tombstoneIndex >= 0);
    assert.ok(removalIndex > tombstoneIndex);

    storage.setItem(entryKey, staleEntryValue);
    storage.setItem(historyStorageKey, legacyText);
    const afterStaleMigration = createHistory({storage});
    assert.deepEqual([...afterStaleMigration.values()], ["A"]);
    assert.equal(storage.getItem(entryKey), null);
    assert.equal(storage.entries().some(([key]) =>
        key === historyLegacyDeletionPrefix +
            encodeURIComponent(migratedB.id)), true);
});

test("keeps a remotely removed selection as a detached anchor", () => {
    const storage = createMemoryStorage();
    const owner = createHistory({
        storage,
        now: () => 3000,
        writerId: "owner",
    });
    owner.record("A");
    owner.record("B");
    owner.record("C");
    const upward = createHistory({storage});
    const downward = createHistory({storage});

    assert.equal(upward.previous("up draft"), "C");
    assert.equal(upward.previous("ignored"), "B");
    assert.equal(downward.previous("down draft"), "C");
    assert.equal(downward.previous("ignored"), "B");
    assert.equal(owner.previous("owner draft"), "C");
    assert.equal(owner.previous("ignored"), "B");
    owner.removeCurrent();

    const upwardUpdate = upward.synchronizeStorage();
    assert.equal(upwardUpdate.changed, true);
    assert.equal(upwardUpdate.currentRemoved, true);
    assert.equal(upward.hasCurrent(), false);
    assert.equal(upward.previous("stale editor text"), "A");
    assert.equal(upward.next(), "C");
    assert.equal(upward.next(), "up draft");

    const downwardUpdate = downward.synchronizeStorage();
    assert.equal(downwardUpdate.currentRemoved, true);
    assert.equal(downward.next(), "C");
    assert.equal(downward.next(), "down draft");
});

test("converges adjacent duplicate entries deterministically", () => {
    const storage = createMemoryStorage({
        [historyStorageKey]: JSON.stringify({
            version: 2,
            format: "entry-keys",
        }),
    });
    setStoredHistoryEntry(storage, {
        id: "r0000000000004000-00000000-first",
        source: "same",
        outcome: "cancelled",
    });
    setStoredHistoryEntry(storage, {
        id: "r0000000000004000-00000000-second",
        source: "same",
        outcome: "timed out",
    });

    const first = createHistory({storage});
    const second = createHistory({storage});
    assert.deepEqual([...first.values()], ["same [cancelled]"]);
    assert.deepEqual([...second.values()], ["same [cancelled]"]);
    assert.equal(storedHistoryEntries(storage).length, 1);
});

test("retains the same newest entries across tabs", () => {
    const storage = createMemoryStorage();
    const first = createHistory({
        storage,
        maximumStoredEntries: 3,
        now: () => 5000,
        writerId: "first",
    });
    const second = createHistory({
        storage,
        maximumStoredEntries: 3,
        now: () => 5000,
        writerId: "second",
    });

    first.record("A");
    second.record("B");
    first.record("C");
    second.record("D");
    first.record("E");
    second.synchronizeStorage();

    assert.deepEqual([...first.values()], ["C", "D", "E"]);
    assert.deepEqual([...second.values()], ["C", "D", "E"]);
    assert.equal(storedHistoryEntries(storage).length, 3);
    assert.deepEqual([
        ...createHistory({
            storage,
            maximumStoredEntries: 3,
        }).values(),
    ], ["C", "D", "E"]);
});

test("clear and late storage events cannot restore old entries", () => {
    const storage = createMemoryStorage();
    const first = createHistory({storage, writerId: "first"});
    first.record("A");
    first.record("B");
    const second = createHistory({storage, writerId: "second"});
    const oldB = storedHistoryEntries(storage)
        .find(entry => entry.source === "B");
    const oldBKey = historyEntryPrefix + encodeURIComponent(oldB.id);
    const oldBValue = storage.getItem(oldBKey);
    assert.equal(second.previous("saved draft"), "B");

    storage.clear();
    const clearUpdate = second.applyStorageEvent(null, null);
    assert.equal(clearUpdate.changed, true);
    assert.equal(clearUpdate.currentRemoved, true);
    assert.deepEqual([...second.values()], []);
    assert.equal(second.next(), "saved draft");

    const lateUpdate = second.applyStorageEvent(oldBKey, oldBValue);
    assert.equal(lateUpdate.changed, false);
    assert.equal(lateUpdate.currentRemoved, false);
    assert.deepEqual([...second.values()], []);
    first.synchronizeStorage();
    assert.deepEqual([...first.values()], []);
});

test("orders new entries after the newest observed entry when the clock rolls back", () => {
    const storage = createMemoryStorage({
        [historyStorageKey]: JSON.stringify({
            version: 2,
            format: "entry-keys",
        }),
    });
    const existing = {
        id: "r0000000000010000-00000000-first",
        source: "A",
        outcome: "",
    };
    setStoredHistoryEntry(storage, existing);

    const history = createHistory({
        storage,
        now: () => 1,
        writerId: "rollback",
    });
    history.record("B");

    const stored = storedHistoryEntries(storage);
    assert.deepEqual(stored.map(entry => entry.source), ["A", "B"]);
    assert.ok(stored[1].id > existing.id);
});

test("does not replace history from an unstable storage enumeration", () => {
    const storage = createMemoryStorage();
    const seed = createHistory({
        storage,
        now: () => 6000,
        writerId: "seed",
    });
    seed.record("A");
    seed.record("B");
    const history = createHistory({storage});
    assert.equal(history.previous("draft"), "B");

    const ordinaryKey = storage.key.bind(storage);
    let scan = 0;
    storage.key = index => {
        if (index === 0) {
            ++scan;
        }
        const keys = storage.entries().map(([key]) => key);
        const visible = scan % 2 === 0
            ? keys.filter(key => !key.startsWith(historyEntryPrefix) ||
                JSON.parse(storage.getItem(key)).source !== "B")
            : keys;
        return visible[index] ?? null;
    };

    const update = history.synchronizeStorage();
    assert.equal(update.changed, false);
    assert.equal(update.currentRemoved, false);
    assert.equal(history.hasCurrent(), true);
    assert.deepEqual([...history.values()], ["A", "B"]);

    storage.key = ordinaryKey;
    assert.equal(history.synchronizeStorage().changed, false);
});

test("ignores malformed legacy tombstones", () => {
    const storage = createMemoryStorage({
        [historyStorageKey]: JSON.stringify({
            version: 1,
            entries: [{source: "A", outcome: ""}],
        }),
    });
    const migrated = createHistory({storage});
    const [entry] = storedHistoryEntries(storage);
    assert.deepEqual([...migrated.values()], ["A"]);

    storage.setItem(
        historyLegacyDeletionPrefix + encodeURIComponent(entry.id),
        "not a valid tombstone",
    );
    assert.deepEqual([...createHistory({storage}).values()], ["A"]);
});

test("continues in memory when browser storage throws", () => {
    const blockedRead = createHistory({
        storage: {
            get length() {
                throw new Error("blocked");
            },
            key() {
                throw new Error("blocked");
            },
            getItem() {
                throw new Error("blocked");
            },
            setItem() {
                throw new Error("unexpected write");
            },
            removeItem() {
                throw new Error("unexpected remove");
            },
        },
    });
    assert.equal(blockedRead.record("Ix"), "Ix");
    assert.deepEqual([...blockedRead.values()], ["Ix"]);

    const fullStorage = createHistory({
        storage: {
            get length() {
                return 0;
            },
            key() {
                return null;
            },
            getItem() {
                return null;
            },
            setItem() {
                throw new Error("quota exceeded");
            },
            removeItem() {
                throw new Error("unexpected remove");
            },
        },
    });
    assert.equal(fullStorage.record("Kx"), "Kx");
    assert.equal(fullStorage.record("Sxyz"), "Sxyz");
    assert.deepEqual([...fullStorage.values()], ["Kx", "Sxyz"]);
});
