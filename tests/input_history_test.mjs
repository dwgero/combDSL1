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
        "define",
        "set",
        "show",
    ]);

    assert.equal(complete("def"), "define");
    assert.equal(complete("se"), "set");
    assert.equal(complete("sho"), "show");
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
