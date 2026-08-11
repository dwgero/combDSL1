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
        "define",
        "depends on",
        "depends-on",
        "dependson",
        "find",
        "references",
        "remove",
        "set",
        "show",
        "used by",
        "used-by",
        "usedby",
    ]);

    assert.equal(complete("abs"), "abstract");
    assert.equal(complete("def"), "define");
    assert.equal(complete("depends o"), "depends on");
    assert.equal(complete("depends-"), "depends-on");
    assert.equal(complete("dependso"), "dependson");
    assert.equal(complete("fin"), "find");
    assert.equal(complete("re"), undefined);
    assert.equal(complete("ref"), "references");
    assert.equal(complete("rem"), "remove");
    assert.equal(complete("se"), "set");
    assert.equal(complete("sho"), "show");
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

test("completes dependency commands with their delimiter", () => {
    const complete = createCommandCompleter([
        "depends on",
        "depends-on",
        "dependson",
        "used by",
        "used-by",
        "usedby",
    ], {appendSpaceToExact: true});

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
    assert.equal(complete("depends on Foo"), undefined);
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

test("keeps references as a typed command without a UI button", () => {
    const html = readFileSync(
        new URL("../web/index.html", import.meta.url), "utf8");

    assert.match(html, /references &lt;captured \| live&gt;/);
    assert.doesNotMatch(
        html,
        /<button\b[^>]*\bid=["']references["']/i);
});

test("completes unambiguous abstract command forms", () => {
    const completers = [
        "abstract ?",
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
        complete("  abstract\tsteps  "),
        "  abstract\tsteps  ?");
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

test("preserves exact entries and repeated successful sources", () => {
    const history = createHistory();
    history.record("  show M  ");
    history.record("Ix");
    history.record("Ix");

    assert.deepEqual([...history.values()], [
        "  show M  ",
        "Ix",
        "Ix",
    ]);
});

test("appends cancellation and timeout outcomes", () => {
    const history = createHistory();
    assert.equal(
        history.record("YI", "cancelled"),
        "YI [cancelled]");
    assert.equal(
        history.record("BKM(BKM)", "timed out"),
        "BKM(BKM) [timed out]");
    assert.deepEqual([...history.values()], [
        "YI [cancelled]",
        "BKM(BKM) [timed out]",
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
    let stored = null;
    const storage = {
        getItem(key) {
            assert.equal(
                key, "combdsl.studio.input-history.v1");
            return stored;
        },
        setItem(key, value) {
            assert.equal(
                key, "combdsl.studio.input-history.v1");
            stored = value;
        },
    };
    const history = createHistory({
        storage,
        maximumStoredEntries: 2,
    });
    history.record("Ix");
    history.record("YI", "cancelled");
    history.record("BKM(BKM)", "timed out");

    assert.deepEqual(JSON.parse(stored), {
        version: 1,
        entries: [
            {source: "YI", outcome: "cancelled"},
            {source: "BKM(BKM)", outcome: "timed out"},
        ],
    });
    assert.deepEqual(
        [...createHistory({
            storage,
            maximumStoredEntries: 2,
        }).values()],
        ["YI [cancelled]", "BKM(BKM) [timed out]"],
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
        storage: {
            getItem() {
                return value;
            },
            setItem() {},
        },
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
    let stored = "not json";
    const storage = {
        getItem() {
            return stored;
        },
        setItem(_key, value) {
            stored = value;
        },
    };
    const history = createHistory({storage});
    history.record("Ix");

    assert.deepEqual(
        [...createHistory({storage}).values()],
        ["Ix"],
    );
});

test("continues in memory when browser storage throws", () => {
    const blockedRead = createHistory({
        storage: {
            getItem() {
                throw new Error("blocked");
            },
            setItem() {
                throw new Error("unexpected write");
            },
        },
    });
    assert.equal(blockedRead.record("Ix"), "Ix");
    assert.deepEqual([...blockedRead.values()], ["Ix"]);

    const fullStorage = createHistory({
        storage: {
            getItem() {
                return null;
            },
            setItem() {
                throw new Error("quota exceeded");
            },
        },
    });
    assert.equal(fullStorage.record("Kx"), "Kx");
    assert.equal(fullStorage.record("Sxyz"), "Sxyz");
    assert.deepEqual([...fullStorage.values()], ["Kx", "Sxyz"]);
});
