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
import {readFile} from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

const workerSource = await readFile(
    new URL("../web/worker.js", import.meta.url),
    "utf8",
);

const createTimerQueue = () => {
    let nextId = 0;
    const callbacks = new Map();
    return {
        setTimeout(callback) {
            const id = ++nextId;
            callbacks.set(id, callback);
            return id;
        },
        clearTimeout(id) {
            callbacks.delete(id);
        },
        runNext() {
            const next = callbacks.entries().next();
            assert.equal(next.done, false);
            const [id, callback] = next.value;
            callbacks.delete(id);
            callback();
        },
        get size() {
            return callbacks.size;
        },
    };
};

const createWorkerHarness = async module => {
    const messages = [];
    let messageListener;
    const timers = createTimerQueue();
    const worker = {
        location: {href: "https://example.test/worker.js?v=test"},
        postMessage: message => messages.push({...message}),
        addEventListener: (type, listener) => {
            if (type === "message") {
                messageListener = listener;
            }
        },
    };
    const context = vm.createContext({
        URL,
        self: worker,
        importScripts: () => {},
        createCombdslModule: () => Promise.resolve(module),
        setTimeout: callback => timers.setTimeout(callback),
        clearTimeout: id => timers.clearTimeout(id),
        performance: {now: () => 0},
        Error,
        String,
        Boolean,
    });

    vm.runInContext(workerSource, context, {
        filename: "worker.js",
    });
    await Promise.resolve();
    assert.equal(
        messages.some(message => message.type === "ready"),
        true,
    );

    return {
        messages,
        timers,
        send: data => messageListener({data}),
    };
};

const incompleteStepStart = {
    success: true,
    reduced: false,
    complete: false,
    definition: false,
    output: "",
    error: "",
};

test("automatic Single Step streams while reduction is running", async () => {
    const steps = [
        {
            success: true,
            reduced: true,
            complete: false,
            definition: false,
            output: "K(M(BKM))\n",
            error: "",
        },
        {
            success: true,
            reduced: true,
            complete: true,
            definition: false,
            output: "K(BKM(BKM))\n",
            error: "",
        },
    ];
    const module = {
        setList: () => "",
        beginSingleStep: source => {
            assert.equal(source, "BKM(BKM)");
            return incompleteStepStart;
        },
        takeSingleStep: (basisStep, colorize, lookAhead) => {
            assert.equal(basisStep, false);
            assert.equal(colorize, false);
            assert.equal(lookAhead, false);
            return steps.shift();
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 7,
        source: "BKM(BKM)",
        singleStep: true,
        basisStep: false,
        keyStep: false,
        colorize: false,
    });
    assert.equal(harness.timers.size, 1);
    assert.deepEqual(
        harness.messages.filter(message => message.id === 7)
            .map(message => message.type),
        ["eval-started"],
    );

    harness.timers.runNext();
    let evaluationMessages = harness.messages.filter(
        message => message.id === 7);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        ["eval-started", "single-step-output"],
    );
    assert.equal(
        evaluationMessages.some(message => message.type === "result"),
        false,
    );
    assert.equal(evaluationMessages[1].output, "K(M(BKM))\n");
    assert.equal(evaluationMessages[1].html, false);
    assert.equal(harness.timers.size, 1);

    harness.timers.runNext();
    evaluationMessages = harness.messages.filter(
        message => message.id === 7);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        [
            "eval-started",
            "single-step-output",
            "single-step-output",
            "result",
        ],
    );
    assert.equal(
        evaluationMessages[2].output,
        "K(BKM(BKM))\n",
    );
    assert.equal(
        evaluationMessages[3].result.reductions,
        2,
    );
});

test("automatic Colorize identifies streamed HTML", async () => {
    const markedOutput =
        "  S<span class=\"wor\">x</span>\n" +
        "-><span class=\"wor\">x</span>\n";
    const module = {
        setList: () => "",
        beginSingleStep: () => incompleteStepStart,
        takeSingleStep: (basisStep, colorize, lookAhead) => {
            assert.equal(basisStep, true);
            assert.equal(colorize, true);
            assert.equal(lookAhead, false);
            return {
                success: true,
                reduced: true,
                complete: true,
                definition: false,
                output: markedOutput,
                error: "",
            };
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 8,
        source: "Ix",
        singleStep: true,
        basisStep: true,
        keyStep: false,
        colorize: true,
    });
    harness.timers.runNext();

    const outputMessage = harness.messages.find(
        message => message.type === "single-step-output");
    assert.equal(outputMessage.output, markedOutput);
    assert.equal(outputMessage.html, true);
});

test("display-only Colorize results remain plain text", async () => {
    const module = {
        setList: () => "",
        beginSingleStep: () => ({
            success: true,
            reduced: false,
            complete: true,
            definition: false,
            output: "<word>&\n",
            error: "",
        }),
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 9,
        source: "show Word",
        singleStep: true,
        basisStep: false,
        keyStep: false,
        colorize: true,
    });

    const result = harness.messages.find(
        message => message.type === "result" &&
            message.id === 9);
    assert.equal(result.html, false);
    assert.equal(result.result.output, "<word>&\n");
    assert.equal(harness.timers.size, 0);
});

test("colorized Basis Key Step keeps completion look-ahead", async () => {
    const markedOutput =
        "  <span class=\"wor\">M</span>xy\n" +
        "-><span class=\"wor\">SII</span>xy\n";
    const module = {
        setList: () => "",
        beginSingleStep: () => incompleteStepStart,
        takeSingleStep: (basisStep, colorize, lookAhead) => {
            assert.equal(basisStep, true);
            assert.equal(colorize, true);
            assert.equal(lookAhead, true);
            return {
                success: true,
                reduced: true,
                complete: true,
                definition: false,
                output: markedOutput,
                error: "",
            };
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 10,
        source: "Mxy",
        singleStep: false,
        basisStep: true,
        keyStep: true,
        colorize: true,
    });
    await harness.send({type: "step", id: 10});

    assert.deepEqual(
        harness.messages.filter(message => message.id === 10)
            .map(message => message.type),
        ["step-ready", "step-result"],
    );
    const result = harness.messages.find(
        message => message.type === "step-result");
    assert.equal(result.result.output, markedOutput);
});
