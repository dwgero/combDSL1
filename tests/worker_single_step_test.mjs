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
        beginSingleStep: (
            source, basisStep, stepLimitEnabled, stepLimit,
        ) => {
            assert.equal(source, "BKM(BKM)");
            assert.equal(basisStep, false);
            assert.equal(stepLimitEnabled, false);
            assert.equal(stepLimit, 0);
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
        stepLimitEnabled: false,
        stepLimit: 0,
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
        stepLimitEnabled: false,
        stepLimit: 0,
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
        stepLimitEnabled: false,
        stepLimit: 0,
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
        beginSingleStep: (
            source, basisStep, stepLimitEnabled, stepLimit,
        ) => {
            assert.equal(source, "Mxy");
            assert.equal(basisStep, true);
            assert.equal(stepLimitEnabled, false);
            assert.equal(stepLimit, 0);
            return incompleteStepStart;
        },
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
        stepLimitEnabled: true,
        stepLimit: 1,
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

test("passes a disabled step limit to ordinary evaluation", async () => {
    const module = {
        setList: () => "",
        parseEval: (
            source, requestId, stepLimitEnabled, stepLimit,
        ) => {
            assert.equal(source, "Ix");
            assert.equal(requestId, 11);
            assert.equal(stepLimitEnabled, false);
            assert.equal(stepLimit, 0);
            return {
                success: true,
                definition: false,
                recoverWorker: false,
                output: "x\n",
                error: "",
                reductions: 1,
                limitReached: false,
            };
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 11,
        source: "Ix",
        singleStep: false,
        basisStep: false,
        keyStep: false,
        colorize: false,
        stepLimitEnabled: false,
        stepLimit: 0,
    });

    const result = harness.messages.find(
        message => message.type === "result" && message.id === 11);
    assert.equal(result.result.output, "x\n");
    assert.equal(result.result.limitReached, false);
});

test("resumes an ordinary limited evaluation without reparsing", async () => {
    let beginCalls = 0;
    let resumeCalls = 0;
    const results = [
        {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "I(I(Ix))\n",
            error: "",
            reductions: 2,
            limitReached: true,
        },
        {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "Ix\n",
            error: "",
            reductions: 4,
            limitReached: true,
        },
        {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "x\n",
            error: "",
            reductions: 5,
            limitReached: false,
        },
    ];
    const module = {
        setList: () => "",
        beginLimitedEval: (
            source, requestId, stepLimit,
        ) => {
            ++beginCalls;
            assert.equal(source, "I(I(I(I(Ix))))");
            assert.equal(requestId, 12);
            assert.equal(stepLimit, 2);
            return results.shift();
        },
        resumeLimitedEval: requestId => {
            ++resumeCalls;
            assert.equal(requestId, 12);
            return results.shift();
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 12,
        source: "I(I(I(I(Ix))))",
        singleStep: false,
        basisStep: false,
        keyStep: false,
        colorize: false,
        stepLimitEnabled: true,
        stepLimit: 2,
    });

    let evaluationMessages = harness.messages.filter(
        message => message.id === 12);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        ["eval-started", "result"],
    );
    assert.equal(evaluationMessages.at(-1).result.output, "I(I(Ix))\n");
    assert.equal(evaluationMessages.at(-1).result.reductions, 2);
    assert.equal(evaluationMessages.at(-1).result.limitReached, true);

    const messageCountBeforeStaleResume = harness.messages.length;
    await harness.send({type: "resume", id: 99});
    assert.equal(harness.messages.length, messageCountBeforeStaleResume);
    assert.equal(resumeCalls, 0);

    await harness.send({type: "resume", id: 12});
    evaluationMessages = harness.messages.filter(
        message => message.id === 12);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        ["eval-started", "result", "eval-started", "result"],
    );
    assert.equal(evaluationMessages.at(-1).result.output, "Ix\n");
    assert.equal(evaluationMessages.at(-1).result.reductions, 4);
    assert.equal(evaluationMessages.at(-1).result.limitReached, true);

    await harness.send({type: "resume", id: 12});
    evaluationMessages = harness.messages.filter(
        message => message.id === 12);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        [
            "eval-started",
            "result",
            "eval-started",
            "result",
            "eval-started",
            "result",
        ],
    );
    assert.equal(evaluationMessages.at(-1).result.output, "x\n");
    assert.equal(evaluationMessages.at(-1).result.reductions, 5);
    assert.equal(evaluationMessages.at(-1).result.limitReached, false);
    assert.equal(beginCalls, 1);
    assert.equal(resumeCalls, 2);
    assert.equal(results.length, 0);

    const messageCountAfterCompletion = harness.messages.length;
    await harness.send({type: "resume", id: 12});
    assert.equal(harness.messages.length, messageCountAfterCompletion);
    assert.equal(resumeCalls, 2);
});

test("resumes automatic Single Step after a step-limit pause", async () => {
    let beginCalls = 0;
    let resumeCalls = 0;
    const steps = [
        {
            success: true,
            reduced: true,
            complete: false,
            definition: false,
            output: "IIx\n",
            error: "",
            limitReached: false,
        },
        {
            success: true,
            reduced: true,
            complete: true,
            definition: false,
            output: "Ix\n",
            error: "",
            limitReached: true,
        },
        {
            success: true,
            reduced: true,
            complete: true,
            definition: false,
            output: "x\n",
            error: "",
            limitReached: false,
        },
    ];
    const module = {
        setList: () => "",
        beginSingleStep: (
            source, basisStep, stepLimitEnabled, stepLimit,
        ) => {
            ++beginCalls;
            assert.equal(source, "IIIx");
            assert.equal(basisStep, false);
            assert.equal(stepLimitEnabled, true);
            assert.equal(stepLimit, 2);
            return incompleteStepStart;
        },
        takeSingleStep: (basisStep, colorize, lookAhead) => {
            assert.equal(basisStep, false);
            assert.equal(colorize, false);
            assert.equal(lookAhead, false);
            return steps.shift();
        },
        resumeSingleStep: () => {
            ++resumeCalls;
            return true;
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 13,
        source: "IIIx",
        singleStep: true,
        basisStep: false,
        keyStep: false,
        colorize: false,
        stepLimitEnabled: true,
        stepLimit: 2,
    });
    assert.equal(harness.timers.size, 1);

    harness.timers.runNext();
    assert.equal(harness.timers.size, 1);
    harness.timers.runNext();
    assert.equal(harness.timers.size, 0);

    let evaluationMessages = harness.messages.filter(
        message => message.id === 13);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        [
            "eval-started",
            "single-step-output",
            "result",
        ],
    );
    assert.equal(
        evaluationMessages.filter(
            message => message.type === "single-step-output").at(-1)
            .output,
        "IIx\n",
    );
    assert.equal(evaluationMessages.at(-1).result.reductions, 2);
    assert.equal(evaluationMessages.at(-1).result.limitReached, true);

    await harness.send({type: "resume", id: 13});
    assert.equal(resumeCalls, 1);
    assert.equal(harness.timers.size, 1);
    evaluationMessages = harness.messages.filter(
        message => message.id === 13);
    assert.deepEqual(
        evaluationMessages.slice(-2).map(message => message.type),
        ["single-step-output", "eval-started"],
    );
    assert.equal(
        evaluationMessages.at(-2).output,
        "Ix\n",
        "the limiting step is deferred until Resume",
    );

    harness.timers.runNext();
    assert.equal(harness.timers.size, 0);
    evaluationMessages = harness.messages.filter(
        message => message.id === 13);
    assert.deepEqual(
        evaluationMessages.slice(-2).map(message => message.type),
        ["single-step-output", "result"],
    );
    assert.equal(evaluationMessages.at(-1).result.reductions, 3);
    assert.equal(evaluationMessages.at(-1).result.limitReached, false);
    assert.equal(beginCalls, 1);
    assert.equal(resumeCalls, 1);
    assert.equal(steps.length, 0);
});

test("Key Step ignores a supplied step limit", async () => {
    const steps = [
        {
            success: true,
            reduced: true,
            complete: false,
            definition: false,
            output: "IIx\n",
            error: "",
            limitReached: false,
        },
        {
            success: true,
            reduced: true,
            complete: false,
            definition: false,
            output: "Ix\n",
            error: "",
            limitReached: false,
        },
        {
            success: true,
            reduced: true,
            complete: true,
            definition: false,
            output: "x\n",
            error: "",
            limitReached: false,
        },
    ];
    const module = {
        setList: () => "",
        beginSingleStep: (
            source, basisStep, stepLimitEnabled, stepLimit,
        ) => {
            assert.equal(source, "IIIx");
            assert.equal(basisStep, false);
            assert.equal(stepLimitEnabled, false);
            assert.equal(stepLimit, 0);
            return incompleteStepStart;
        },
        takeSingleStep: (basisStep, colorize, lookAhead) => {
            assert.equal(basisStep, false);
            assert.equal(colorize, false);
            assert.equal(lookAhead, true);
            return steps.shift();
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 14,
        source: "IIIx",
        singleStep: false,
        basisStep: false,
        keyStep: true,
        colorize: false,
        stepLimitEnabled: true,
        stepLimit: 1,
    });
    await harness.send({type: "step", id: 14});
    await harness.send({type: "step", id: 14});
    await harness.send({type: "step", id: 14});

    const evaluationMessages = harness.messages.filter(
        message => message.id === 14);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        [
            "step-ready",
            "step-result",
            "step-result",
            "step-result",
        ],
    );
    assert.deepEqual(
        evaluationMessages.slice(1).map(
            message => message.result.output),
        ["IIx\n", "Ix\n", "x\n"],
    );
    assert.equal(
        evaluationMessages.slice(1).every(
            message => !message.result.limitReached),
        true,
    );
    assert.equal(evaluationMessages.at(-1).result.output, "x\n");
    assert.equal(steps.length, 0);
});
