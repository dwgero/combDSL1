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
