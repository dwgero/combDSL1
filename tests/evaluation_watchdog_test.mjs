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

const scheduledMessage = (sequence, elapsedMs) => {
    const phase = Math.floor((sequence - 1) / 10);
    const position = (sequence - 1) % 10 + 1;
    const stepsPerMessage = 100 * 10 ** phase;
    let reductions = position * stepsPerMessage;
    for (let completedPhase = 0;
        completedPhase < phase;
        ++completedPhase) {
        reductions += 10 * 100 * 10 ** completedPhase;
    }
    return {
        sequence,
        reductions,
        stepsPerMessage,
        nextStepsPerMessage: position === 10
            ? stepsPerMessage * 10
            : stepsPerMessage,
        elapsedMs,
    };
};

test("exports watchdog timing constants", () => {
    assert.equal(watchdog.initialWaitMs, 10000);
    assert.equal(watchdog.minimumWaitMs, 250);
});

test("formats the accumulated reduction count for a timeout", () => {
    const state = watchdog.createProgressState();
    assert.equal(
        watchdog.timeoutMessage(state),
        "[timed out after more than 0 steps]");

    for (let sequence = 1; sequence <= 20; ++sequence) {
        watchdog.acceptProgress(
            state, scheduledMessage(sequence, sequence * 100));
        if (sequence === 1) {
            assert.equal(
                watchdog.timeoutMessage(state),
                "[timed out after more than 100 steps]");
        } else if (sequence === 10) {
            assert.equal(
                watchdog.timeoutMessage(state),
                "[timed out after more than 1000 steps]");
        }
    }

    assert.equal(
        watchdog.timeoutMessage(state),
        "[timed out after more than 11000 steps]");

    watchdog.acceptProgress(
        state, scheduledMessage(20, 2100));
    assert.equal(
        watchdog.timeoutMessage(state),
        "[timed out after more than 11000 steps]");
});

test("uses a phase-local arithmetic mean with a 1.5 margin", () => {
    const state = watchdog.createProgressState();

    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(1, 400)),
        600);
    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(2, 1000)),
        750);
});

test("enforces the minimum wait", () => {
    const state = watchdog.createProgressState();

    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(1, 0)),
        250);
    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(2, 0)),
        250);
});

test("scales a phase boundary and replaces its seed", () => {
    const state = watchdog.createProgressState();
    let elapsedMs = 0;

    for (let sequence = 1; sequence <= 9; ++sequence) {
        elapsedMs += 400;
        assert.equal(
            watchdog.acceptProgress(
                state, scheduledMessage(sequence, elapsedMs)),
            600);
    }

    elapsedMs += 400;
    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(10, elapsedMs)),
        6000);

    elapsedMs += 3000;
    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(11, elapsedMs)),
        4500);

    elapsedMs += 5000;
    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(12, elapsedMs)),
        6000);
});

test("accepts the exact 100, 1000, and 10000 step schedule", () => {
    const state = watchdog.createProgressState();
    let elapsedMs = 0;

    for (let sequence = 1; sequence <= 21; ++sequence) {
        elapsedMs += sequence <= 10
            ? 10
            : sequence <= 20
                ? 100
                : 1000;
        assert.notEqual(
            watchdog.acceptProgress(
                state, scheduledMessage(sequence, elapsedMs)),
            undefined);
    }

    assert.equal(state.sequence, 21);
    assert.equal(state.reductions, 21000);
    assert.equal(state.stepsPerMessage, 10000);
});

test("rejects invalid progress without changing state", () => {
    const invalidMessages = [
        {...scheduledMessage(1, 100), sequence: 2},
        {...scheduledMessage(1, 100), reductions: 101},
        {...scheduledMessage(1, 100), stepsPerMessage: 1000},
        {...scheduledMessage(1, 100), nextStepsPerMessage: 1000},
        {...scheduledMessage(1, 100), elapsedMs: -1},
        {...scheduledMessage(1, 100), sequence: "1"},
    ];

    for (const message of invalidMessages) {
        const state = watchdog.createProgressState();
        assert.equal(
            watchdog.acceptProgress(state, message),
            undefined);
        assert.equal(state.sequence, 0);
        assert.equal(state.reductions, 0);
        assert.equal(
            watchdog.acceptProgress(
                state, scheduledMessage(1, 100)),
            250);
    }
});

test("rejects duplicate, skipped, and decreasing progress", () => {
    const state = watchdog.createProgressState();
    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(1, 500)),
        750);

    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(1, 600)),
        undefined);
    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(3, 600)),
        undefined);
    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(2, 499)),
        undefined);

    assert.equal(state.sequence, 1);
    assert.equal(state.reductions, 100);
    assert.equal(
        watchdog.acceptProgress(
            state, scheduledMessage(2, 1000)),
        750);
});

test("requires the interval change on the tenth message", () => {
    const state = watchdog.createProgressState();
    for (let sequence = 1; sequence <= 9; ++sequence) {
        assert.notEqual(
            watchdog.acceptProgress(
                state, scheduledMessage(sequence, sequence * 100)),
            undefined);
    }

    const invalidBoundary = {
        ...scheduledMessage(10, 1000),
        nextStepsPerMessage: 100,
    };
    assert.equal(
        watchdog.acceptProgress(state, invalidBoundary),
        undefined);
    assert.equal(state.sequence, 9);
    assert.notEqual(
        watchdog.acceptProgress(
            state, scheduledMessage(10, 1000)),
        undefined);
});
