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
import {createRequire} from "node:module";
import test from "node:test";
import vm from "node:vm";

const workerSource = await readFile(
    new URL("../web/worker.js", import.meta.url),
    "utf8",
);
const require = createRequire(import.meta.url);
const createBuiltCombdslModule = require("../docs/combdsl.js");
const builtCombdslWasm = await readFile(
    new URL("../docs/combdsl.wasm", import.meta.url),
);

const loadBuiltCombdslModule = async () => {
    const previousSelf = globalThis.self;
    globalThis.self = {
        location: {href: "https://example.test/combdsl.js"},
    };
    try {
        return await createBuiltCombdslModule({
            instantiateWasm(imports, receiveInstance) {
                void WebAssembly.instantiate(
                    builtCombdslWasm, imports,
                ).then(result => receiveInstance(result.instance));
            },
        });
    } finally {
        if (previousSelf === undefined) {
            delete globalThis.self;
        } else {
            globalThis.self = previousSelf;
        }
    }
};

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

const noFurtherReductions = "No further reductions\n";

const completedStepStart = output => ({
    success: true,
    reduced: false,
    complete: true,
    definition: false,
    output,
    error: "",
    limitReached: false,
});

test("built active and legacy browser APIs share the zero-redex notice",
    async () => {
        const module = await loadBuiltCombdslModule();
        const results = [
            ["active ordinary", module.beginLimitedEval(
                "C*xy", 201, 1000, false)],
            ["active Single Step", module.beginSingleStep(
                "C*xy", false, false, 0)],
            ["active Basis/Key Step", module.beginSingleStep(
                "C*xy", true, false, 0)],
            ["legacy ordinary", module.parseEval(
                "C*xy", 202, false, 0)],
            ["legacy Single Step", module.singleStepRun(
                "C*xy", false, 203)],
            ["legacy Color/Basis Step", module.colorStepRun(
                "C*xy", true, 204)],
        ];

        for (const [description, result] of results) {
            assert.equal(result.success, true, description);
            assert.equal(result.definition, false, description);
            assert.equal(result.output, noFurtherReductions, description);
            assert.equal(result.error, "", description);
            assert.equal(result.limitReached, false, description);
            if ("complete" in result) {
                assert.equal(result.complete, true, description);
                assert.equal(result.reduced, false, description);
            } else {
                assert.equal(result.reductions, 0, description);
            }
        }
    });

test("built browser APIs preserve commands, errors, reductions, and limits",
    async () => {
        const module = await loadBuiltCombdslModule();
        let requestId = 220;
        const apiCalls = [
            ["active ordinary", source => module.beginLimitedEval(
                source, ++requestId, 1000, false)],
            ["active stepped", source => module.beginSingleStep(
                source, false, false, 0)],
            ["legacy ordinary", source => module.parseEval(
                source, ++requestId, false, 0)],
            ["legacy Single Step", source => module.singleStepRun(
                source, false, ++requestId)],
            ["legacy Color Step", source => module.colorStepRun(
                source, true, ++requestId)],
        ];

        for (const [index, [description, call]] of apiCalls.entries()) {
            const display = call("show I");
            assert.equal(display.success, true, description);
            assert.equal(display.definition, false, description);
            assert.match(display.output, /fundamental name/, description);
            assert.equal(
                display.output.includes(noFurtherReductions.trim()),
                false,
                description,
            );

            const definition = call(`set ApiBranch${index} = 1 I`);
            assert.equal(definition.success, true, description);
            assert.equal(definition.definition, true, description);
            assert.equal(definition.output, "", description);

            const error = call("@");
            assert.equal(error.success, false, description);
            assert.match(error.error, /unknown operand/, description);
            assert.equal(error.output, "", description);
        }

        const activeOrdinary = module.beginLimitedEval(
            "Ix", ++requestId, 1000, false);
        assert.equal(activeOrdinary.output, "x\n");
        assert.equal(activeOrdinary.reductions, 1);
        assert.equal(activeOrdinary.limitReached, false);

        const activeStepStart = module.beginSingleStep(
            "Ix", false, false, 0);
        assert.equal(activeStepStart.complete, false);
        const activeStep = module.takeSingleStep(false, false, false);
        assert.equal(activeStep.output, "x\n");
        assert.equal(activeStep.reduced, true);
        assert.equal(activeStep.complete, false);
        const activeCompletionProbe = module.takeSingleStep(
            false, false, false);
        assert.equal(activeCompletionProbe.output, "");
        assert.equal(activeCompletionProbe.reduced, false);
        assert.equal(activeCompletionProbe.complete, true);

        const activeKeyStart = module.beginSingleStep(
            "Ix", false, false, 0);
        assert.equal(activeKeyStart.complete, false);
        const activeKeyStep = module.takeSingleStep(false, false, true);
        assert.equal(activeKeyStep.output, "x\n");
        assert.equal(activeKeyStep.reduced, true);
        assert.equal(activeKeyStep.complete, true);

        const activeColorStart = module.beginSingleStep(
            "Mx", true, false, 0);
        assert.equal(activeColorStart.complete, false);
        const activeColorStep = module.takeSingleStep(true, true, false);
        assert.equal(activeColorStep.reduced, true);
        assert.match(activeColorStep.output, /class="wor"/);
        assert.equal(
            activeColorStep.output.includes(noFurtherReductions.trim()),
            false,
        );

        for (const [description, result] of [
            ["legacy ordinary", module.parseEval(
                "Ix", ++requestId, false, 0)],
            ["legacy Single Step", module.singleStepRun(
                "Ix", false, ++requestId)],
            ["legacy Color Step", module.colorStepRun(
                "Ix", false, ++requestId)],
        ]) {
            assert.equal(result.success, true, description);
            assert.equal(result.reductions, 1, description);
            assert.equal(
                result.output.includes(noFurtherReductions.trim()),
                false,
                description,
            );
        }

        const activeZeroLimit = module.beginSingleStep(
            "Ix", false, true, 0);
        assert.equal(activeZeroLimit.success, true);
        assert.equal(activeZeroLimit.complete, true);
        assert.equal(activeZeroLimit.output, "Ix\n");
        assert.equal(activeZeroLimit.limitReached, true);

        const activeOrdinaryLimit = module.beginLimitedEval(
            "IIx", ++requestId, 1, true);
        assert.equal(activeOrdinaryLimit.success, true);
        assert.equal(activeOrdinaryLimit.output, "");
        assert.equal(activeOrdinaryLimit.reductions, 1);
        assert.equal(activeOrdinaryLimit.limitReached, true);

        const legacyOrdinaryLimit = module.parseEval(
            "IIx", ++requestId, true, 1);
        assert.equal(legacyOrdinaryLimit.success, true);
        assert.equal(legacyOrdinaryLimit.output, "");
        assert.equal(legacyOrdinaryLimit.reductions, 1);
        assert.equal(legacyOrdinaryLimit.limitReached, true);
    });

test("ordinary zero-redex evaluation reports no further reductions",
    async () => {
        let resumeCalls = 0;
        const module = {
            setList: () => "",
            beginLimitedEval: (
                source, requestId, sliceLimit, checkAtLimit,
            ) => {
                assert.equal(source, "C*xy");
                assert.equal(requestId, 106);
                assert.equal(sliceLimit, 1000);
                assert.equal(checkAtLimit, false);
                return {
                    success: true,
                    definition: false,
                    recoverWorker: false,
                    output: noFurtherReductions,
                    error: "",
                    reductions: 0,
                    limitReached: false,
                };
            },
            resumeLimitedEval: () => {
                ++resumeCalls;
            },
        };
        const harness = await createWorkerHarness(module);

        await harness.send({
            type: "evaluate",
            id: 106,
            source: "C*xy",
            singleStep: false,
            basisStep: false,
            keyStep: false,
            colorize: false,
            stepLimitEnabled: false,
            stepLimit: 0,
        });

        const messages = harness.messages.filter(message => message.id === 106);
        assert.deepEqual(
            messages.map(message => message.type),
            ["eval-started", "result"],
        );
        assert.equal(messages[1].result.output, noFurtherReductions);
        assert.equal(messages[1].result.reductions, 0);
        assert.equal(messages[1].result.limitReached, false);
        assert.equal(harness.timers.size, 0);
        assert.equal(resumeCalls, 0);
    });

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

test("zero-redex Single Step reports no further reductions immediately",
    async () => {
        let takeCalls = 0;
        const module = {
            setList: () => "",
            beginSingleStep: (
                source, basisStep, stepLimitEnabled, stepLimit,
            ) => {
                assert.equal(source, "C*xy");
                assert.equal(basisStep, false);
                assert.equal(stepLimitEnabled, true);
                assert.equal(stepLimit, 0);
                return completedStepStart(noFurtherReductions);
            },
            takeSingleStep: () => {
                ++takeCalls;
            },
        };
        const harness = await createWorkerHarness(module);

        await harness.send({
            type: "evaluate",
            id: 107,
            source: "C*xy",
            singleStep: true,
            basisStep: false,
            keyStep: false,
            colorize: false,
            stepLimitEnabled: true,
            stepLimit: 0,
        });

        const messages = harness.messages.filter(message => message.id === 107);
        assert.deepEqual(
            messages.map(message => message.type),
            ["eval-started", "result"],
        );
        assert.equal(messages[1].result.output, noFurtherReductions);
        assert.equal(messages[1].result.reductions, 0);
        assert.equal(messages[1].result.success, true);
        assert.equal(messages[1].html, false);
        assert.equal(harness.timers.size, 0);
        assert.equal(takeCalls, 0);
    });

test("zero-redex Key Step reports no further reductions without a key press",
    async () => {
        let takeCalls = 0;
        const module = {
            setList: () => "",
            beginSingleStep: (
                source, basisStep, stepLimitEnabled, stepLimit,
            ) => {
                assert.equal(source, "C*xy");
                assert.equal(basisStep, true);
                assert.equal(stepLimitEnabled, false);
                assert.equal(stepLimit, 0);
                return completedStepStart(noFurtherReductions);
            },
            takeSingleStep: () => {
                ++takeCalls;
            },
        };
        const harness = await createWorkerHarness(module);

        await harness.send({
            type: "evaluate",
            id: 108,
            source: "C*xy",
            singleStep: false,
            basisStep: true,
            keyStep: true,
            colorize: true,
            stepLimitEnabled: true,
            stepLimit: 99,
        });

        const messages = harness.messages.filter(message => message.id === 108);
        assert.deepEqual(
            messages.map(message => message.type),
            ["result"],
        );
        assert.equal(messages[0].result.output, noFurtherReductions);
        assert.equal(messages[0].result.complete, true);
        assert.equal(messages[0].result.reduced, false);
        assert.equal(messages[0].html, false);
        assert.equal(harness.timers.size, 0);
        assert.equal(takeCalls, 0);

        await harness.send({type: "step", id: 108});
        assert.equal(
            harness.messages.filter(message => message.id === 108).length,
            1,
        );
        assert.equal(takeCalls, 0);
    });

test("reducible Single Step at a zero limit remains limit-reached",
    async () => {
        let takeCalls = 0;
        const module = {
            setList: () => "",
            beginSingleStep: (
                source, basisStep, stepLimitEnabled, stepLimit,
            ) => {
                assert.equal(source, "Ix");
                assert.equal(basisStep, false);
                assert.equal(stepLimitEnabled, true);
                assert.equal(stepLimit, 0);
                return {
                    success: true,
                    reduced: false,
                    complete: true,
                    definition: false,
                    output: "Ix\n",
                    error: "",
                    limitReached: true,
                };
            },
            takeSingleStep: () => {
                ++takeCalls;
            },
        };
        const harness = await createWorkerHarness(module);

        await harness.send({
            type: "evaluate",
            id: 109,
            source: "Ix",
            singleStep: true,
            basisStep: false,
            keyStep: false,
            colorize: false,
            stepLimitEnabled: true,
            stepLimit: 0,
        });

        const messages = harness.messages.filter(message => message.id === 109);
        assert.deepEqual(
            messages.map(message => message.type),
            ["eval-started", "result"],
        );
        assert.equal(messages[1].result.output, "Ix\n");
        assert.equal(messages[1].result.reductions, 0);
        assert.equal(messages[1].result.limitReached, true);
        assert.equal(harness.timers.size, 0);
        assert.equal(takeCalls, 0);
    });

test("definition, display-only, and error begin paths keep their protocol",
    async () => {
        const cases = [
            {
                source: "set Bird = 1 I",
                keyStep: false,
                result: {
                    success: true,
                    reduced: false,
                    complete: true,
                    definition: true,
                    output: "",
                    error: "",
                },
                types: ["eval-started", "result"],
                setList: "set Bird = 1 I",
            },
            {
                source: "show Bird",
                keyStep: true,
                result: completedStepStart("arity:1 I\n"),
                types: ["result"],
            },
            {
                source: "@",
                keyStep: true,
                result: {
                    success: false,
                    reduced: false,
                    complete: false,
                    definition: false,
                    output: "",
                    error: "Parse error at position 1: unknown operand",
                },
                types: ["step-ready"],
            },
        ];

        for (const [index, item] of cases.entries()) {
            let takeCalls = 0;
            const module = {
                setList: () => item.setList ?? "",
                beginSingleStep: () => item.result,
                takeSingleStep: () => {
                    ++takeCalls;
                },
            };
            const harness = await createWorkerHarness(module);
            const id = 120 + index;

            await harness.send({
                type: "evaluate",
                id,
                source: item.source,
                singleStep: !item.keyStep,
                basisStep: false,
                keyStep: item.keyStep,
                colorize: false,
                stepLimitEnabled: false,
                stepLimit: 0,
            });

            const messages = harness.messages.filter(
                message => message.id === id);
            assert.deepEqual(
                messages.map(message => message.type),
                item.types,
            );
            const response = messages.at(-1);
            assert.equal(response.result.success, item.result.success);
            assert.equal(response.result.definition, item.result.definition);
            assert.equal(response.result.output, item.result.output);
            assert.equal(response.result.error, item.result.error);
            assert.equal(response.setList, item.setList);
            assert.equal(harness.timers.size, 0);
            assert.equal(takeCalls, 0);

            await harness.send({type: "step", id});
            assert.equal(
                harness.messages.filter(message => message.id === id).length,
                messages.length,
            );
            assert.equal(takeCalls, 0);
        }
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

test("runs ordinary evaluation in cooperative slices", async () => {
    let beginCalls = 0;
    let resumeCalls = 0;
    const results = [
        {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "Ix\n",
            error: "",
            reductions: 1,
            limitReached: true,
        },
        {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "x\n",
            error: "",
            reductions: 2,
            limitReached: false,
        },
    ];
    const module = {
        setList: () => "",
        beginLimitedEval: (
            source, requestId, sliceLimit, checkAtLimit,
        ) => {
            ++beginCalls;
            assert.equal(source, "Ix");
            assert.equal(requestId, 11);
            assert.equal(sliceLimit, 1000);
            assert.equal(checkAtLimit, false);
            return results.shift();
        },
        resumeLimitedEval: (requestId, sliceLimit, checkAtLimit) => {
            ++resumeCalls;
            assert.equal(requestId, 11);
            assert.equal(sliceLimit, 1000);
            assert.equal(checkAtLimit, false);
            return results.shift();
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

    assert.deepEqual(
        harness.messages.filter(message => message.id === 11)
            .map(message => message.type),
        ["eval-started"],
        "an internal slice yield must not be exposed as a result",
    );
    assert.equal(harness.timers.size, 1);

    harness.timers.runNext();
    const result = harness.messages.find(
        message => message.type === "result" && message.id === 11);
    assert.equal(result.result.output, "x\n");
    assert.equal(result.result.limitReached, false);
    assert.equal(harness.timers.size, 0);
    assert.equal(beginCalls, 1);
    assert.equal(resumeCalls, 1);
    assert.equal(results.length, 0);
});

test("lets restricted Find finish under its own deadline", async () => {
    const command = "find among AKIS ?xy = x(yx)";
    const module = {
        setList: () => "",
        beginLimitedEval: (
            source, requestId, sliceLimit, checkAtLimit,
        ) => {
            assert.equal(source, command);
            assert.equal(requestId, 22);
            assert.equal(sliceLimit, 1000);
            assert.equal(checkAtLimit, false);
            return {
                success: true,
                definition: false,
                recoverWorker: false,
                output: "?=A\n",
                error: "",
                reductions: 0,
                limitReached: false,
            };
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 22,
        source: command,
        singleStep: false,
        basisStep: false,
        keyStep: false,
        colorize: false,
        stepLimitEnabled: false,
        stepLimit: 0,
    });

    assert.deepEqual(
        harness.messages.filter(message => message.id === 22)
            .map(message => message.type),
        ["eval-started", "result"],
    );
    const result = harness.messages.find(
        message => message.type === "result" && message.id === 22);
    assert.equal(result.result.output, "?=A\n");
    assert.equal(harness.timers.size, 0,
        "the worker must not impose a second timer on Find");
});

test("checks normal form only at a configured limit boundary", async () => {
    const sliceCalls = [];
    const module = {
        setList: () => "",
        beginLimitedEval: (
            source, requestId, sliceLimit, checkAtLimit,
        ) => {
            assert.equal(source, "long expression");
            assert.equal(requestId, 18);
            sliceCalls.push([sliceLimit, checkAtLimit]);
            return {
                success: true,
                definition: false,
                recoverWorker: false,
                output: "",
                error: "",
                reductions: 1000,
                limitReached: true,
            };
        },
        resumeLimitedEval: (
            requestId, sliceLimit, checkAtLimit,
        ) => {
            assert.equal(requestId, 18);
            sliceCalls.push([sliceLimit, checkAtLimit]);
            return {
                success: true,
                definition: false,
                recoverWorker: false,
                output: "",
                error: "",
                reductions: 1500,
                limitReached: true,
            };
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 18,
        source: "long expression",
        singleStep: false,
        basisStep: false,
        keyStep: false,
        colorize: false,
        stepLimitEnabled: true,
        stepLimit: 1500,
    });
    assert.deepEqual(sliceCalls, [[1000, false]]);
    assert.equal(
        harness.messages.some(message =>
            message.id === 18 && message.type === "result"),
        false,
    );

    harness.timers.runNext();
    assert.deepEqual(sliceCalls, [
        [1000, false],
        [500, true],
    ]);
    const result = harness.messages.find(
        message => message.id === 18 && message.type === "result");
    assert.equal(result.result.reductions, 1500);
    assert.equal(result.result.limitReached, true);
    assert.equal(harness.timers.size, 0);
});

test("preserves ordinary step-limit windows across internal slices", async () => {
    let beginCalls = 0;
    let resumeCalls = 0;
    const resumeSliceLimits = [];
    const resumeLimitChecks = [];
    const results = [
        {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "I(I(I(Ix)))\n",
            error: "",
            reductions: 1,
            limitReached: true,
        },
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
            output: "I(Ix)\n",
            error: "",
            reductions: 3,
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
            source, requestId, stepLimit, checkAtLimit,
        ) => {
            ++beginCalls;
            assert.equal(source, "I(I(I(I(Ix))))");
            assert.equal(requestId, 12);
            assert.equal(stepLimit, 2);
            assert.equal(checkAtLimit, true);
            return results.shift();
        },
        resumeLimitedEval: (
            requestId, sliceLimit, checkAtLimit,
        ) => {
            ++resumeCalls;
            assert.equal(requestId, 12);
            resumeSliceLimits.push(sliceLimit);
            resumeLimitChecks.push(checkAtLimit);
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
        ["eval-started"],
        "the first internal slice yield must remain internal",
    );
    assert.equal(harness.timers.size, 1);

    harness.timers.runNext();
    evaluationMessages = harness.messages.filter(
        message => message.id === 12);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        ["eval-started", "result"],
    );
    assert.equal(evaluationMessages.at(-1).result.output, "I(I(Ix))\n");
    assert.equal(evaluationMessages.at(-1).result.reductions, 2);
    assert.equal(evaluationMessages.at(-1).result.limitReached, true);
    assert.equal(harness.timers.size, 0);

    const messageCountBeforeStaleResume = harness.messages.length;
    await harness.send({type: "resume", id: 99});
    assert.equal(harness.messages.length, messageCountBeforeStaleResume);
    assert.equal(resumeCalls, 1);

    await harness.send({type: "resume", id: 12});
    assert.equal(harness.timers.size, 1);
    assert.equal(resumeCalls, 1);
    harness.timers.runNext();
    assert.equal(harness.timers.size, 1);
    assert.equal(resumeCalls, 2);
    harness.timers.runNext();
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
    assert.equal(harness.timers.size, 1);
    harness.timers.runNext();
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
    assert.equal(resumeCalls, 4);
    assert.deepEqual(resumeSliceLimits, [1, 2, 1, 2]);
    assert.deepEqual(resumeLimitChecks, [true, true, true, true]);
    assert.equal(results.length, 0);

    const messageCountAfterCompletion = harness.messages.length;
    await harness.send({type: "resume", id: 12});
    assert.equal(harness.messages.length, messageCountAfterCompletion);
    assert.equal(resumeCalls, 4);
});

test("pauses and resumes ordinary evaluation between slices", async () => {
    let resumeCalls = 0;
    const resumeSliceLimits = [];
    const resumeLimitChecks = [];
    const results = [
        {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "Ix\n",
            error: "",
            reductions: 3,
            limitReached: true,
        },
        {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "x\n",
            error: "",
            reductions: 4,
            limitReached: false,
        },
    ];
    const module = {
        setList: () => "",
        beginLimitedEval: (
            source, requestId, sliceLimit, checkAtLimit,
        ) => {
            assert.equal(source, "IIIIx");
            assert.equal(requestId, 15);
            assert.equal(sliceLimit, 3);
            assert.equal(checkAtLimit, true);
            return {
                success: true,
                definition: false,
                recoverWorker: false,
                output: "IIIx\n",
                error: "",
                reductions: 1,
                limitReached: true,
            };
        },
        resumeLimitedEval: (
            requestId, sliceLimit, checkAtLimit,
        ) => {
            ++resumeCalls;
            assert.equal(requestId, 15);
            resumeSliceLimits.push(sliceLimit);
            resumeLimitChecks.push(checkAtLimit);
            return results.shift();
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 15,
        source: "IIIIx",
        singleStep: false,
        basisStep: false,
        keyStep: false,
        colorize: false,
        stepLimitEnabled: true,
        stepLimit: 3,
    });
    assert.equal(harness.timers.size, 1);

    await harness.send({type: "pause", id: 15});
    assert.equal(harness.timers.size, 0);
    assert.equal(resumeCalls, 0);
    let evaluationMessages = harness.messages.filter(
        message => message.id === 15);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        ["eval-started", "paused"],
    );
    assert.equal(evaluationMessages.at(-1).reductions, 1);

    const messageCountBeforeStaleResume = harness.messages.length;
    await harness.send({type: "resume", id: 99});
    assert.equal(harness.messages.length, messageCountBeforeStaleResume);
    assert.equal(harness.timers.size, 0);

    await harness.send({type: "resume", id: 15});
    assert.equal(harness.timers.size, 1);
    assert.equal(resumeCalls, 0);
    evaluationMessages = harness.messages.filter(
        message => message.id === 15);
    assert.deepEqual(
        evaluationMessages.slice(-2).map(message => message.type),
        ["paused", "eval-started"],
    );

    harness.timers.runNext();
    evaluationMessages = harness.messages.filter(
        message => message.id === 15);
    assert.equal(evaluationMessages.at(-1).type, "result");
    assert.equal(evaluationMessages.at(-1).result.output, "Ix\n");
    assert.equal(evaluationMessages.at(-1).result.reductions, 3);
    assert.equal(evaluationMessages.at(-1).result.limitReached, true);
    assert.equal(resumeCalls, 1);
    assert.equal(harness.timers.size, 0);
    assert.deepEqual(resumeSliceLimits, [2],
        "manual Resume must preserve the remaining limit window");
    assert.deepEqual(resumeLimitChecks, [true]);

    await harness.send({type: "resume", id: 15});
    assert.equal(harness.timers.size, 1);
    harness.timers.runNext();
    evaluationMessages = harness.messages.filter(
        message => message.id === 15);
    assert.equal(evaluationMessages.at(-1).type, "result");
    assert.equal(evaluationMessages.at(-1).result.output, "x\n");
    assert.equal(evaluationMessages.at(-1).result.reductions, 4);
    assert.equal(evaluationMessages.at(-1).result.limitReached, false);
    assert.deepEqual(resumeSliceLimits, [2, 3],
        "step-limit Resume must start a fresh limit window");
    assert.deepEqual(resumeLimitChecks, [true, true]);
    assert.equal(results.length, 0);
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

test("pauses and resumes automatic Single Step between reductions", async () => {
    let limitResumeCalls = 0;
    const steps = [
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
        beginSingleStep: () => incompleteStepStart,
        takeSingleStep: (basisStep, colorize, lookAhead) => {
            assert.equal(basisStep, false);
            assert.equal(colorize, false);
            assert.equal(lookAhead, false);
            return steps.shift();
        },
        resumeSingleStep: () => {
            ++limitResumeCalls;
            return true;
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 16,
        source: "IIx",
        singleStep: true,
        basisStep: false,
        keyStep: false,
        colorize: false,
        stepLimitEnabled: false,
        stepLimit: 0,
    });
    harness.timers.runNext();
    assert.equal(harness.timers.size, 1);

    await harness.send({type: "pause", id: 16});
    assert.equal(harness.timers.size, 0);
    let evaluationMessages = harness.messages.filter(
        message => message.id === 16);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        ["eval-started", "single-step-output", "paused"],
    );
    assert.equal(evaluationMessages.at(-1).reductions, 1);
    assert.equal(limitResumeCalls, 0);

    await harness.send({type: "resume", id: 16});
    assert.equal(harness.timers.size, 1);
    assert.equal(limitResumeCalls, 0,
        "manual Resume must not reset the step-limit window");
    evaluationMessages = harness.messages.filter(
        message => message.id === 16);
    assert.equal(evaluationMessages.at(-1).type, "eval-started");

    harness.timers.runNext();
    evaluationMessages = harness.messages.filter(
        message => message.id === 16);
    assert.deepEqual(
        evaluationMessages.slice(-2).map(message => message.type),
        ["single-step-output", "result"],
    );
    assert.equal(evaluationMessages.at(-1).result.reductions, 2);
    assert.equal(harness.timers.size, 0);
    assert.equal(steps.length, 0);
});

test("pauses Key Step and refuses steps until Resume", async () => {
    let takeCalls = 0;
    const steps = [
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
        beginSingleStep: () => incompleteStepStart,
        takeSingleStep: (basisStep, colorize, lookAhead) => {
            ++takeCalls;
            assert.equal(basisStep, false);
            assert.equal(colorize, false);
            assert.equal(lookAhead, true);
            return steps.shift();
        },
    };
    const harness = await createWorkerHarness(module);

    await harness.send({
        type: "evaluate",
        id: 17,
        source: "IIx",
        singleStep: false,
        basisStep: false,
        keyStep: true,
        colorize: false,
        stepLimitEnabled: false,
        stepLimit: 0,
    });
    await harness.send({type: "step", id: 17});
    assert.equal(takeCalls, 1);

    await harness.send({type: "pause", id: 17});
    let evaluationMessages = harness.messages.filter(
        message => message.id === 17);
    assert.deepEqual(
        evaluationMessages.map(message => message.type),
        ["step-ready", "step-result", "paused"],
    );
    assert.equal(evaluationMessages.at(-1).reductions, 1);

    const messageCountWhilePaused = harness.messages.length;
    await harness.send({type: "step", id: 17});
    assert.equal(takeCalls, 1);
    assert.equal(harness.messages.length, messageCountWhilePaused);

    await harness.send({type: "resume", id: 17});
    evaluationMessages = harness.messages.filter(
        message => message.id === 17);
    assert.equal(evaluationMessages.at(-1).type, "step-ready");
    assert.equal(evaluationMessages.at(-1).result.success, true);

    await harness.send({type: "step", id: 17});
    evaluationMessages = harness.messages.filter(
        message => message.id === 17);
    assert.equal(evaluationMessages.at(-1).type, "step-result");
    assert.equal(evaluationMessages.at(-1).result.output, "x\n");
    assert.equal(takeCalls, 2);
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
