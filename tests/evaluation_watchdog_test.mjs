/*
 * C++ combinator DSL
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
    "../web/evaluation_watchdog.js", import.meta.url);
const context = vm.createContext({});
new vm.Script(readFileSync(sourceUrl, "utf8"), {
    filename: sourceUrl.pathname,
}).runInContext(context);

const watchdog = context.combdslEvaluationWatchdog;

test("exports watchdog timing constants", () => {
    assert.equal(watchdog.heartbeatIntervalMs, 100);
    assert.equal(watchdog.timeoutMs, 1000);
    assert.equal(watchdog.stepDisplayInterval, 1000);
});

test("rounds progress down to completed thousand-step milestones", () => {
    const expected = new Map([
        [1, 0],
        [999, 0],
        [1000, 1000],
        [1001, 1000],
        [1999, 1000],
        [2000, 2000],
        [3035, 3000],
    ]);

    for (const [reductions, milestone] of expected) {
        assert.equal(
            watchdog.displayedStepCount(reductions),
            milestone);
    }

    for (const invalid of [
        0,
        -1,
        1.5,
        Number.NaN,
        Number.POSITIVE_INFINITY,
        Number.MAX_SAFE_INTEGER + 1,
        "1000",
        null,
        undefined,
    ]) {
        assert.equal(watchdog.displayedStepCount(invalid), 0);
    }
});

test("preserves an exact valid final reduction count", () => {
    for (const reductions of [1, 999, 1000, 2537, 10_001]) {
        assert.equal(
            watchdog.exactStepCount(reductions),
            reductions);
    }

    for (const invalid of [
        0,
        -1,
        1.5,
        Number.NaN,
        Number.POSITIVE_INFINITY,
        Number.MAX_SAFE_INTEGER + 1,
        "2537",
        null,
        undefined,
    ]) {
        assert.equal(watchdog.exactStepCount(invalid), 0);
    }
});

test("only displays a final count after reaching one thousand steps", () => {
    const expected = new Map([
        [1, 0],
        [999, 0],
        [1000, 1000],
        [1001, 1001],
        [2537, 2537],
    ]);

    for (const [reductions, displayed] of expected) {
        assert.equal(
            watchdog.finalDisplayedStepCount(reductions),
            displayed);
    }

    for (const invalid of [
        0,
        -1,
        1.5,
        Number.NaN,
        Number.POSITIVE_INFINITY,
        Number.MAX_SAFE_INTEGER + 1,
        "1000",
        null,
        undefined,
    ]) {
        assert.equal(
            watchdog.finalDisplayedStepCount(invalid), 0);
    }
});

test("formats running and final step counts", () => {
    const expected = [
        [1, false, ""],
        [999, false, ""],
        [1000, false, "[1000 steps so far]"],
        [2537, false, "[2000 steps so far]"],
        [1, true, ""],
        [999, true, ""],
        [1000, true, "[1000 total steps]"],
        [2537, true, "[2537 total steps]"],
    ];

    for (const [reductions, final, message] of expected) {
        assert.equal(
            watchdog.stepCountMessage(reductions, final),
            message);
    }
});

test("creates empty progress state", () => {
    const state = watchdog.createProgressState();

    assert.equal(state.sequence, 0);
    assert.equal(state.reductions, 0);
    assert.deepEqual(Object.keys(state).sort(), [
        "reductions",
        "sequence",
    ]);
});

test("formats the last accepted reduction count for a timeout", () => {
    const state = watchdog.createProgressState();
    assert.equal(
        watchdog.timeoutMessage(state),
        "[timed out after more than 0 steps]");

    assert.equal(
        watchdog.acceptProgress(
            state, {sequence: 1, reductions: 137}),
        true);
    assert.equal(
        watchdog.timeoutMessage(state),
        "[timed out after more than 137 steps]");

    assert.equal(
        watchdog.acceptProgress(
            state, {sequence: 3, reductions: 999}),
        false);
    assert.equal(
        watchdog.timeoutMessage(state),
        "[timed out after more than 137 steps]");
});

test("accepts consecutive heartbeats with increasing cumulative counts", () => {
    const state = watchdog.createProgressState();
    const messages = [
        {sequence: 1, reductions: 1},
        {sequence: 2, reductions: 2},
        {sequence: 3, reductions: 105},
        {
            sequence: 4,
            reductions: Number.MAX_SAFE_INTEGER,
        },
    ];

    for (const message of messages) {
        assert.equal(
            watchdog.acceptProgress(state, message),
            true);
    }

    assert.equal(state.sequence, 4);
    assert.equal(
        state.reductions,
        Number.MAX_SAFE_INTEGER);
});

test("rejects duplicate, skipped, and out-of-order sequences", () => {
    const invalidSequences = [
        0,
        -1,
        2,
        1.5,
        Number.NaN,
        Number.POSITIVE_INFINITY,
        Number.MAX_SAFE_INTEGER + 1,
        "1",
        null,
        undefined,
    ];

    for (const sequence of invalidSequences) {
        const state = watchdog.createProgressState();
        assert.equal(
            watchdog.acceptProgress(
                state, {sequence, reductions: 1}),
            false);
        assert.equal(state.sequence, 0);
        assert.equal(state.reductions, 0);
    }

    const state = watchdog.createProgressState();
    assert.equal(
        watchdog.acceptProgress(
            state, {sequence: 1, reductions: 10}),
        true);
    for (const sequence of [1, 3, 0]) {
        assert.equal(
            watchdog.acceptProgress(
                state, {sequence, reductions: 20}),
            false);
        assert.equal(state.sequence, 1);
        assert.equal(state.reductions, 10);
    }
});

test("rejects non-increasing and invalid reduction counts", () => {
    const invalidReductions = [
        0,
        -1,
        1.5,
        Number.NaN,
        Number.POSITIVE_INFINITY,
        Number.MAX_SAFE_INTEGER + 1,
        "1",
        null,
        undefined,
    ];

    for (const reductions of invalidReductions) {
        const state = watchdog.createProgressState();
        assert.equal(
            watchdog.acceptProgress(
                state, {sequence: 1, reductions}),
            false);
        assert.equal(state.sequence, 0);
        assert.equal(state.reductions, 0);
    }

    const state = watchdog.createProgressState();
    assert.equal(
        watchdog.acceptProgress(
            state, {sequence: 1, reductions: 10}),
        true);
    for (const reductions of [10, 9, 1]) {
        assert.equal(
            watchdog.acceptProgress(
                state, {sequence: 2, reductions}),
            false);
        assert.equal(state.sequence, 1);
        assert.equal(state.reductions, 10);
    }
});

test("rejects non-object progress without changing state", () => {
    const state = watchdog.createProgressState();

    for (const message of [
        null,
        undefined,
        1,
        "progress",
        true,
    ]) {
        assert.equal(
            watchdog.acceptProgress(state, message),
            false);
        assert.equal(state.sequence, 0);
        assert.equal(state.reductions, 0);
    }

    assert.equal(
        watchdog.acceptProgress(
            null, {sequence: 1, reductions: 1}),
        false);
});

test("accepts the expected heartbeat after invalid messages", () => {
    const state = watchdog.createProgressState();

    assert.equal(
        watchdog.acceptProgress(
            state, {sequence: 2, reductions: 10}),
        false);
    assert.equal(
        watchdog.acceptProgress(
            state, {sequence: 1, reductions: 0}),
        false);
    assert.equal(
        watchdog.acceptProgress(
            state, {sequence: 1, reductions: 10}),
        true);
    assert.equal(
        watchdog.acceptProgress(
            state, {sequence: 2, reductions: 11}),
        true);

    assert.equal(state.sequence, 2);
    assert.equal(state.reductions, 11);
});
