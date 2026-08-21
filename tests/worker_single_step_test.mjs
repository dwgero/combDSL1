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
const cmakeSource = await readFile(
    new URL("../CMakeLists.txt", import.meta.url),
    "utf8",
);
const require = createRequire(import.meta.url);
const createBuiltCombdslModule = require("../docs/combdsl.js");
const builtCombdslWasm = await readFile(
    new URL("../docs/combdsl.wasm", import.meta.url),
);

const loadBuiltCombdslModule = async ({inspectInstance} = {}) => {
    const previousSelf = globalThis.self;
    globalThis.self = {
        location: {href: "https://example.test/combdsl.js"},
    };
    try {
        return await createBuiltCombdslModule({
            instantiateWasm(imports, receiveInstance) {
                void WebAssembly.instantiate(
                    builtCombdslWasm, imports,
                ).then(result => {
                    inspectInstance?.(result.instance);
                    receiveInstance(result.instance);
                });
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
    const scheduledDelays = [];
    return {
        setTimeout(callback, delay = 0) {
            const id = ++nextId;
            callbacks.set(id, {callback, delay});
            scheduledDelays.push(delay);
            return id;
        },
        clearTimeout(id) {
            callbacks.delete(id);
        },
        runNext() {
            const next = callbacks.entries().next();
            assert.equal(next.done, false);
            const [id, timer] = next.value;
            callbacks.delete(id);
            timer.callback();
        },
        delays() {
            return [...callbacks.values()].map(timer => timer.delay);
        },
        scheduledDelays,
        get size() {
            return callbacks.size;
        },
    };
};

const createWorkerHarness = async (
    module,
    {
        hardwareConcurrency = 4,
        nestedWorkersAvailable = true,
        now = () => 0,
        workerHref = "https://example.test/worker.js?v=test",
    } = {},
) => {
    const messages = [];
    let messageListener;
    const timers = createTimerQueue();
    const nestedWorkers = [];
    class NestedWorker {
        constructor(url) {
            this.url = String(url);
            this.messages = [];
            this.terminated = false;
            this.listeners = new Map();
            nestedWorkers.push(this);
        }

        addEventListener(type, listener) {
            const listeners = this.listeners.get(type) ?? [];
            listeners.push(listener);
            this.listeners.set(type, listeners);
        }

        postMessage(message) {
            this.messages.push({...message});
        }

        terminate() {
            this.terminated = true;
        }

        send(message) {
            for (const listener of this.listeners.get("message") ?? []) {
                listener({data: message});
            }
        }

        fail(message = "nested worker failed") {
            const event = {
                message,
                defaultPrevented: false,
                preventDefault() {
                    this.defaultPrevented = true;
                },
            };
            for (const listener of this.listeners.get("error") ?? []) {
                listener(event);
            }
            return event;
        }
    }
    const worker = {
        location: {href: workerHref},
        navigator: {hardwareConcurrency},
        postMessage: message => messages.push({...message}),
        addEventListener: (type, listener) => {
            if (type === "message") {
                messageListener = listener;
            }
        },
    };
    const context = vm.createContext({
        URL,
        Worker: NestedWorker,
        self: worker,
        importScripts: () => {},
        createCombdslModule: () => Promise.resolve(module),
        setTimeout: (callback, delay) =>
            timers.setTimeout(callback, delay),
        clearTimeout: id => timers.clearTimeout(id),
        performance: {now},
        Error,
        String,
        Boolean,
    });
    if (!nestedWorkersAvailable) {
        delete context.Worker;
    }

    vm.runInContext(workerSource, context, {
        filename: "worker.js",
    });
    await Promise.resolve();
    const expectedReadyType = new URL(workerHref).searchParams.get("role") ===
        "find-helper"
        ? "find-helper-ready"
        : "ready";
    assert.equal(
        messages.some(message => message.type === expectedReadyType),
        true,
    );

    return {
        messages,
        nestedWorkers,
        timers,
        send: data => messageListener({data}),
    };
};

const flushMicrotasks = async (turns = 8) => {
    for (let turn = 0; turn < turns; ++turn) {
        await Promise.resolve();
    }
};

const nextNestedRequest = (worker, type) => {
    const request = worker.messages.find(message =>
        message.type === type && !message.testAnswered);
    assert.ok(request, `missing nested-worker request ${type}`);
    request.testAnswered = true;
    return request;
};

const answerNestedRequest = (worker, type, result) => {
    const request = nextNestedRequest(worker, type);
    worker.send({
        type: "find-helper-result",
        jobId: request.jobId,
        result,
    });
    return request;
};

const restrictedFindEvaluationMessage = ({
    id = 501,
    source = "find among A B ?x = x",
} = {}) => ({
    type: "evaluate",
    id,
    source,
    findAmong: true,
    singleStep: false,
    basisStep: false,
    keyStep: false,
    colorize: false,
    stepLimitEnabled: false,
    stepLimit: 0,
});

const readyAndRestoreFindHelpers = async (
    harness,
    expectedSetList,
) => {
    for (const helper of harness.nestedWorkers) {
        helper.send({type: "find-helper-ready"});
    }
    await flushMicrotasks();
    for (const helper of harness.nestedWorkers) {
        const load = answerNestedRequest(
            helper,
            "find-helper-load",
            {success: true, loaded: 0, line: 0, error: ""},
        );
        assert.equal(load.source, expectedSetList);
    }
    await flushMicrotasks();
};

const prepareFindHelpers = async (
    harness,
    {
        allSizes = false,
        catalogSize = 2,
        searchable = true,
        timedOut = false,
    } = {},
) => {
    for (const helper of harness.nestedWorkers.filter(
        worker => !worker.terminated)) {
        answerNestedRequest(helper, "find-helper-prepare", {
            success: true,
            timedOut,
            searchable,
            allSizes,
            catalogSize,
            error: "",
        });
    }
    await flushMicrotasks();
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

test("builds each Studio WebAssembly instance with a 16 MiB initial heap",
    async () => {
        assert.match(
            cmakeSource,
            /-sINITIAL_HEAP=16777216(?:\s|$)/,
        );
        assert.doesNotMatch(
            cmakeSource,
            /-sINITIAL_HEAP=67108864(?:\s|$)/,
        );

        let initialMemoryBytes = 0;
        await loadBuiltCombdslModule({
            inspectInstance(instance) {
                const memory = Object.values(instance.exports).find(
                    value => value instanceof WebAssembly.Memory);
                assert.ok(memory, "built Wasm must export its memory");
                initialMemoryBytes = memory.buffer.byteLength;
            },
        });
        assert.ok(initialMemoryBytes >= 16 * 1024 * 1024);
        assert.ok(initialMemoryBytes < 32 * 1024 * 1024,
            "static data may add to the heap, but the old 64 MiB baseline " +
            "must not return");
    });

const completedStepStart = output => ({
    success: true,
    reduced: false,
    complete: true,
    definition: false,
    output,
    error: "",
    limitReached: false,
});

const takeFindAmongShardMatches = result => {
    const matches = [];
    try {
        for (let index = 0; index < result.matches.size(); ++index) {
            const match = result.matches.get(index);
            matches.push({
                index: Number(match.index),
                expression: String(match.expression),
            });
        }
    } finally {
        result.matches.delete();
    }
    return matches;
};

const applyBuiltDefinition = (module, source, requestId) => {
    const result = module.beginLimitedEval(
        source, requestId, 1000, false);
    assert.equal(result.success, true, result.error);
    assert.equal(result.definition, true, source);
    assert.equal(result.output, "", source);
};

test("built Studio Wasm routes and reloads equals-prefixed basis names",
    async () => {
        const module = await loadBuiltCombdslModule();
        const malformed = module.inspectDefinition("set = 3 C");
        assert.equal(malformed.success, false);
        assert.match(
            malformed.error,
            /Parse error at position 7: expected '='/,
        );

        let requestId = 280;
        for (const source of [
            "set = = 3 C",
            "set =bar = 3 C",
            "define = bar = rab",
        ]) {
            const inspection = module.inspectDefinition(source);
            assert.equal(inspection.success, true, source);
            assert.equal(inspection.definition, true, source);
            assert.equal(inspection.displayOnly, false, source);
            applyBuiltDefinition(module, source, ++requestId);
        }

        const showInspection = module.inspectDefinition("show =");
        assert.equal(showInspection.success, true);
        assert.equal(showInspection.definition, false);
        assert.equal(showInspection.displayOnly, true);

        const equals = module.parseEval("=xyz", ++requestId, false, 0);
        assert.equal(equals.success, true, equals.error);
        assert.equal(equals.output, "zyx\n");
        const equalsBar = module.parseEval(
            "=bar x y z", ++requestId, false, 0);
        assert.equal(equalsBar.success, true, equalsBar.error);
        assert.equal(equalsBar.output, "xzy\n");

        const setList = module.setList();
        assert.equal(
            setList,
            "references captured\n" +
            "set = = 3 C\n" +
            "set =bar = 3 C\n" +
            "define = bar = rab",
        );

        const restored = await loadBuiltCombdslModule();
        const load = restored.loadSetList(setList, "equals names");
        assert.equal(load.success, true, load.error);
        assert.equal(load.loaded, 4);
        const restoredEquals = restored.parseEval(
            "=xyz", ++requestId, false, 0);
        assert.equal(restoredEquals.success, true, restoredEquals.error);
        assert.equal(restoredEquals.output, "zyx\n");
        const restoredEqualsBar = restored.parseEval(
            "=bar x y z", ++requestId, false, 0);
        assert.equal(restoredEqualsBar.success, true,
            restoredEqualsBar.error);
        assert.equal(restoredEqualsBar.output, "xzy\n");
    });

test("built Studio Wasm keeps ampersand under ordinary punctuation rules",
    async () => {
        const module = await loadBuiltCombdslModule();
        const malformed = module.inspectDefinition("set & 3 C");
        assert.equal(malformed.success, false);
        assert.match(
            malformed.error,
            /Parse error at position 7: expected '='/,
        );
        const missingAssignment = module.inspectDefinition("set && 3 C");
        assert.equal(missingAssignment.success, false);
        assert.match(
            missingAssignment.error,
            /Parse error at position 8: expected '='/,
        );
        const adjacentAssignment = module.inspectDefinition("set &=1 I");
        assert.equal(adjacentAssignment.success, true,
            adjacentAssignment.error);
        const unspacedAssignment = module.inspectDefinition("set &bar=1 I");
        assert.equal(unspacedAssignment.success, true,
            unspacedAssignment.error);

        let requestId = 320;
        for (const source of [
            "set &=1 I",
            "set &bar=1 I",
            "set &foo=bar",
            "define & bar = rab",
            "define &bar = rab",
            "set A&B=1 I",
        ]) {
            const inspection = module.inspectDefinition(source);
            assert.equal(inspection.success, true, source);
            assert.equal(inspection.definition, true, source);
            assert.equal(inspection.displayOnly, false, source);
            applyBuiltDefinition(module, source, ++requestId);
        }

        const showInspection = module.inspectDefinition("show &");
        assert.equal(showInspection.success, true);
        assert.equal(showInspection.definition, false);
        assert.equal(showInspection.displayOnly, true);

        const ampersand = module.parseEval("&xyz", ++requestId, false, 0);
        assert.equal(ampersand.success, true, ampersand.error);
        assert.equal(ampersand.output, "zyx\n");
        const ampersandBar = module.parseEval(
            "&bar x y z", ++requestId, false, 0);
        assert.equal(ampersandBar.success, true, ampersandBar.error);
        assert.equal(ampersandBar.output, "xyz\n");
        const oldWholeName = module.inspectDefinition("show &foo=bar");
        assert.equal(oldWholeName.success, false);
        assert.match(oldWholeName.error, /is not a defined name/);
        const ordinaryAmpersand = module.parseEval(
            "A&B x", ++requestId, false, 0);
        assert.equal(ordinaryAmpersand.success, true,
            ordinaryAmpersand.error);
        assert.equal(ordinaryAmpersand.output, "x\n");

        const findInspection = module.inspectDefinition(
            "find among &bar ?x=x");
        assert.equal(findInspection.success, true, findInspection.error);
        assert.equal(findInspection.find, true);
        assert.equal(findInspection.findAmong, true);
        assert.equal(findInspection.findCatalogSize, 1);

        const compared = module.parseEval(
            "compare ?x &bar x = x",
            ++requestId, false, 0);
        assert.equal(compared.success, true, compared.error);
        assert.equal(compared.output, "both reduce to: xx\n");

        const setList = module.setList();
        assert.equal(
            setList,
            "references captured\n" +
            "set & = 1 I\n" +
            "set &bar = 1 I\n" +
            "set &foo = 0 bar\n" +
            "define & bar = rab\n" +
            "set A&B = 1 I",
        );

        const restored = await loadBuiltCombdslModule();
        const load = restored.loadSetList(setList, "ampersand names");
        assert.equal(load.success, true, load.error);
        assert.equal(load.loaded, 6);
        const restoredAmpersand = restored.parseEval(
            "&xyz", ++requestId, false, 0);
        assert.equal(restoredAmpersand.success, true,
            restoredAmpersand.error);
        assert.equal(restoredAmpersand.output, "zyx\n");
        const restoredAmpersandBar = restored.parseEval(
            "&bar x y z", ++requestId, false, 0);
        assert.equal(restoredAmpersandBar.success, true,
            restoredAmpersandBar.error);
        assert.equal(restoredAmpersandBar.output, "xyz\n");
        const restoredOldWholeName = restored.inspectDefinition(
            "show &foo=bar");
        assert.equal(restoredOldWholeName.success, false);
        assert.match(restoredOldWholeName.error, /is not a defined name/);
        const restoredOrdinaryAmpersand = restored.parseEval(
            "A&B x", ++requestId, false, 0);
        assert.equal(restoredOrdinaryAmpersand.success, true,
            restoredOrdinaryAmpersand.error);
        assert.equal(restoredOrdinaryAmpersand.output, "x\n");
    });

test("built Studio Wasm routes, queries, and reloads question-prefixed names",
    async () => {
        const module = await loadBuiltCombdslModule();
        for (const [source, position] of [
            ["set ? 3 C", 7],
            ["set ?? 3 C", 8],
            ["set ?= 3 C", 8],
            ["set ?bar=3 C", 12],
        ]) {
            const malformed = module.inspectDefinition(source);
            assert.equal(malformed.success, false, source);
            assert.match(malformed.error,
                new RegExp(`Parse error at position ${position}: expected '='`),
                source);
        }
        for (const [source, position, diagnostic] of [
            ["set ?Bad@1 = I", 9,
                "version suffix is not allowed in a definition name"],
            ["define ?Bad@1 x = x", 12,
                "version suffix is not allowed in a definition name"],
            ["remove ?Bad@1", 12,
                "version suffix is not allowed in a removal name"],
            ["revisions ?Bad@1", 15,
                "version suffix is not allowed in a revisions name"],
        ]) {
            const invalid = module.inspectDefinition(source);
            assert.equal(invalid.success, false, source);
            assert.match(invalid.error,
                new RegExp(`Parse error at position ${position}:`), source);
            assert.match(invalid.error, new RegExp(diagnostic), source);
        }
        const unregisteredSoleMarker = module.inspectDefinition(
            "find among ?x=xx");
        assert.equal(unregisteredSoleMarker.success, false);
        assert.match(unregisteredSoleMarker.error,
            /Parse error at position 12: expected at least one bird name/);

        let requestId = 340;
        for (const source of [
            "set ? = 3 C",
            "set ?bar = 3 C",
            "set ?foo = 1 I",
            "define ? bar = rab",
            "define ?bar = rab",
            "set ?foo=bar = 1 I",
            "set ?foo?bar = 1 I",
        ]) {
            const inspection = module.inspectDefinition(source);
            assert.equal(inspection.success, true, source);
            assert.equal(inspection.definition, true, source);
            assert.equal(inspection.displayOnly, false, source);
            applyBuiltDefinition(module, source, ++requestId);
        }

        const bare = module.parseEval("?xyz", ++requestId, false, 0);
        assert.equal(bare.success, true, bare.error);
        assert.equal(bare.output, "zyx\n");
        const longer = module.parseEval(
            "?bar x y z", ++requestId, false, 0);
        assert.equal(longer.success, true, longer.error);
        assert.equal(longer.output, "xzy\n");
        const interiorEquals = module.parseEval(
            "?foo=bar x", ++requestId, false, 0);
        assert.equal(interiorEquals.success, true, interiorEquals.error);
        assert.equal(interiorEquals.output, "x\n");
        const longerExactNameOnly = module.inspectDefinition(
            "find among ?foo=bar");
        assert.equal(longerExactNameOnly.success, false);
        assert.match(longerExactNameOnly.error,
            /Parse error at position 20: expected '\?'/);
        const interiorQuestion = module.parseEval(
            "?foo?bar x", ++requestId, false, 0);
        assert.equal(interiorQuestion.success, true, interiorQuestion.error);
        assert.equal(interiorQuestion.output, "x\n");

        const inspected = module.parseEval(
            "inspect ?foo=bar x", ++requestId, false, 0);
        assert.equal(inspected.success, true, inspected.error);
        assert.match(inspected.output, /free symbols: x/);
        assert.match(inspected.output, /\?foo=bar \[captured\]/);

        for (const source of ["set ?x= = B", "set ?y= = C"]) {
            applyBuiltDefinition(module, source, ++requestId);
        }

        for (const [source, catalogSize] of [
            ["find among ? ?bar ?foo ?q=q", 3],
            ["find among ?foo@1 ?q=q", 1],
            ["find among ?@1 ?q=q", 1],
            ["find among I ?q=q", 1],
            ["find among I ?x = x", 1],
            ["find among I?foo=bar ?q=q", 2],
            ["find among ?foo=bar ?q=q", 1],
            ["find among ?foo=barI ?q=q", 2],
            ["find among I ?foo=x", 1],
            ["find among ?x= ?y= ?z=zz", 2],
            ["find among ?y= ?x= ?z=zz", 2],
            ["find among ?x=\t?y=\n?z \f=zz", 2],
            ["find among I?x=?y= ?z=zz", 3],
            ["find among ?x=@1 ?y=@1 ?z=zz", 2],
            ["find among ?x= ?y= ?z = (?foo=bar)", 2],
            ["find among I ?z=?foo=bar", 1],
            ["find among I ?z=(K ?x=xx)", 1],
            ['find among I ?z="raw ?x=xx"', 1],
        ]) {
            const find = module.inspectDefinition(source);
            assert.equal(find.success, true, `${source}: ${find.error}`);
            assert.equal(find.find, true, source);
            assert.equal(find.findAmong, true, source);
            assert.equal(find.findCatalogSize, catalogSize, source);
        }
        for (const source of ["find among ?q=q", "find among ?q =q"]) {
            const empty = module.inspectDefinition(source);
            assert.equal(empty.success, false, source);
            assert.match(empty.error,
                /Parse error at position 12: expected at least one bird name/,
                source);
        }
        const undefinedBeforeMarker = module.inspectDefinition(
            "find among ?x= Missing ?z=zz");
        assert.equal(undefinedBeforeMarker.success, false);
        assert.match(undefinedBeforeMarker.error,
            /Parse error at position 17: issing is not a defined name/);

        applyBuiltDefinition(module, "set ?foo=barI = K", ++requestId);
        const exactWholeCompact = module.inspectDefinition(
            "find among ?foo=barI ?q=q");
        assert.equal(exactWholeCompact.success, true,
            exactWholeCompact.error);
        assert.equal(exactWholeCompact.findCatalogSize, 1);

        const compactSuffixMarker = module.inspectDefinition(
            "find among I?z=zz");
        assert.equal(compactSuffixMarker.success, false);
        assert.match(compactSuffixMarker.error,
            /Parse error at position 14: z=zz is not a defined name/);

        applyBuiltDefinition(module, "set ?space = I", ++requestId);
        for (const phase of ["current", "removed singleton"]) {
            const spacedRegisteredName = module.inspectDefinition(
                "find among I ?space = ?q=K");
            assert.equal(spacedRegisteredName.success, false, phase);
            assert.match(spacedRegisteredName.error,
                /Parse error at position 21: = is not a defined name/,
                phase);
            if (phase === "current") {
                applyBuiltDefinition(module, "remove ?space", ++requestId);
            }
        }

        applyBuiltDefinition(module, "set ?rev= = I", ++requestId);
        applyBuiltDefinition(module, "set ?rev= = K", ++requestId);
        applyBuiltDefinition(module, "remove ?rev=", ++requestId);
        const explicitRemovedRevision = module.inspectDefinition(
            "find among ?rev=@1 ?q=q");
        assert.equal(explicitRemovedRevision.success, true,
            explicitRemovedRevision.error);
        assert.equal(explicitRemovedRevision.findCatalogSize, 1);
        const unavailableRemovedName = module.inspectDefinition(
            "find among I ?rev=K");
        assert.equal(unavailableRemovedName.success, true,
            unavailableRemovedName.error);
        assert.equal(unavailableRemovedName.findCatalogSize, 1);

        const requestedFind = module.parseEval(
            "find among S K ?x= ?y= ?z= ?y=",
            ++requestId, false, 0);
        assert.equal(requestedFind.success, true, requestedFind.error);
        assert.equal(requestedFind.output, "?=K ?y=\n");

        const registeredSoleMarker = module.inspectDefinition(
            "find among ?x=xx");
        assert.equal(registeredSoleMarker.success, false);
        assert.match(registeredSoleMarker.error,
            /Parse error at position 15: xx is not a defined name/);
        const registeredShortTarget = module.inspectDefinition(
            "find among ?x=x");
        assert.equal(registeredShortTarget.success, false);
        assert.match(registeredShortTarget.error,
            /Parse error at position 15: x is not a defined name/);
        const spacedRegisteredPrefix = module.inspectDefinition(
            "find among ?x =x");
        assert.equal(spacedRegisteredPrefix.success, false);
        assert.match(spacedRegisteredPrefix.error,
            /Parse error at position 12: expected at least one bird name/);
        applyBuiltDefinition(module, "set ?z= = 1 I", ++requestId);
        const registeredMarkerNameIsCatalog = module.inspectDefinition(
            "find among ?x= ?y= ?z=zz");
        assert.equal(registeredMarkerNameIsCatalog.success, false);
        assert.match(registeredMarkerNameIsCatalog.error,
            /Parse error at position 23: zz is not a defined name/);
        applyBuiltDefinition(module, "remove ?y=", ++requestId);
        const retainedRevision = module.inspectDefinition(
            "find among ?x= ?y=@1 ?u=uu");
        assert.equal(retainedRevision.success, true,
            retainedRevision.error);
        assert.equal(retainedRevision.findCatalogSize, 2);
        applyBuiltDefinition(module, "remove ?x=", ++requestId);
        const retainedBareSingleton = module.inspectDefinition(
            "find among ?x=xx");
        assert.equal(retainedBareSingleton.success, false);
        assert.match(retainedBareSingleton.error,
            /Parse error at position 15: xx is not a defined name/);

        const compared = module.parseEval(
            "compare ?x ?foo=bar x = x",
            ++requestId, false, 0);
        assert.equal(compared.success, true, compared.error);
        assert.equal(compared.output, "both reduce to: xx\n");
        const abstracted = module.parseEval(
            "abstract ?x=x", ++requestId, false, 0);
        assert.equal(abstracted.success, true, abstracted.error);
        assert.equal(abstracted.output, "?=I\n");
        const revisions = module.parseEval(
            "revisions ?", ++requestId, false, 0);
        assert.equal(revisions.success, true, revisions.error);
        assert.match(revisions.output,
            /^\?@1 arity:3 C \[captured\]\n/);
        assert.match(revisions.output,
            /\?@2 arity:3 .* \[captured\] \[current\]\n$/);

        for (const source of [
            "set captured QCaptured = 3 ?",
            "set live QLive = 3 ?",
        ]) {
            applyBuiltDefinition(module, source, ++requestId);
        }
        const usedBy = module.parseEval(
            "usedby QCaptured", ++requestId, false, 0);
        assert.equal(usedBy.success, true, usedBy.error);
        assert.equal(usedBy.output, "QCaptured directly uses: ?\n");
        const dependsOn = module.parseEval(
            "dependson ?", ++requestId, false, 0);
        assert.equal(dependsOn.success, true, dependsOn.error);
        assert.equal(dependsOn.output,
            "? is directly depended on by: QCaptured QLive\n");
        const path = module.parseEval(
            "usedby path QCaptured ?", ++requestId, false, 0);
        assert.equal(path.success, true, path.error);
        assert.equal(path.output,
            "QCaptured uses ? via:\n" +
            "  QCaptured -> ?@2  [captured]\n");

        applyBuiltDefinition(module, "set ? = 3 C", ++requestId);
        applyBuiltDefinition(module, "remove ?", ++requestId);
        const capturedAfterRemove = module.parseEval(
            "QCaptured xyz", ++requestId, false, 0);
        assert.equal(capturedAfterRemove.success, true,
            capturedAfterRemove.error);
        assert.equal(capturedAfterRemove.output, "zyx\n");
        const liveAfterRemove = module.parseEval(
            "QLive xyz", ++requestId, false, 0);
        assert.equal(liveAfterRemove.success, true, liveAfterRemove.error);
        assert.equal(liveAfterRemove.output, "xzy\n");
        const removedRevision = module.parseEval(
            "?@3xyz", ++requestId, false, 0);
        assert.equal(removedRevision.success, true, removedRevision.error);
        assert.equal(removedRevision.output, "xzy\n");
        applyBuiltDefinition(module, "set ? = 3 I", ++requestId);
        applyBuiltDefinition(module, "remove ?foo?bar", ++requestId);
        applyBuiltDefinition(module, "remove ?foo=bar", ++requestId);

        const setList = module.setList();
        assert.match(setList, /^references captured\nset \? = 3 C\n/);
        assert.match(setList, /\nset \?foo=bar = 1 I\n/);
        assert.doesNotMatch(setList, /\nset &foo=bar = /);
        assert.match(setList, /\nremove \?\nset \? = 3 I\n/);
        assert.match(setList, /\nremove \?foo\?bar\nremove \?foo=bar$/);

        const invalidLoad = module.loadSetList(
            "set ?LoadBefore = 1 I\n" +
            "set ?Bad@ = 1 I\n" +
            "set ?LoadAfter = 1 K\n",
            "invalid question names",
        );
        assert.equal(invalidLoad.success, false);
        assert.equal(invalidLoad.loaded, 0);
        assert.match(invalidLoad.error,
            /combdsl::basis names cannot end with @/);
        assert.equal(module.setList(), setList,
            "a rejected question-name load must restore registry and journal");
        for (const name of ["?LoadBefore", "?LoadAfter"]) {
            const absent = module.inspectDefinition(`show ${name}`);
            assert.equal(absent.success, false, name);
            assert.match(absent.error, /is not a defined name/, name);
        }

        const restored = await loadBuiltCombdslModule();
        const load = restored.loadSetList(setList, "question names");
        assert.equal(load.success, true, load.error);
        assert.equal(load.loaded, setList.split("\n").length);
        assert.equal(restored.setList(), setList);
        const restoredBare = restored.parseEval(
            "? x y z", ++requestId, false, 0);
        assert.equal(restoredBare.success, true, restoredBare.error);
        assert.equal(restoredBare.output, "xyz\n");
        const restoredLonger = restored.parseEval(
            "?bar x y z", ++requestId, false, 0);
        assert.equal(restoredLonger.success, true, restoredLonger.error);
        assert.equal(restoredLonger.output, "xzy\n");
        const restoredRemoved = restored.parseEval(
            "?foo=bar x", ++requestId, false, 0);
        assert.equal(restoredRemoved.success, true, restoredRemoved.error);
        assert.equal(restoredRemoved.output, "x\n");
    });

test("built Studio Wasm preserves literal and named leading backslashes",
    async () => {
        const module = await loadBuiltCombdslModule();
        const slash = "\\";
        const quotedSlash = `"${slash}${slash}"`;
        let requestId = 340;

        const literalBeforeRegistration = module.parseEval(
            `I ${slash}`, ++requestId, false, 0);
        assert.equal(literalBeforeRegistration.success, true,
            literalBeforeRegistration.error);
        assert.equal(
            literalBeforeRegistration.output,
            `${quotedSlash}\n`,
            "an unregistered bare backslash must retain its old raw value",
        );

        for (const source of [
            `set ${slash} = 3 C`,
            `set ${slash}foo = 1 I`,
            `set ${slash}12345678901234 = 1 I`,
            `define ${slash} bar = rab`,
        ]) {
            const inspection = module.inspectDefinition(source);
            assert.equal(inspection.success, true, source);
            assert.equal(inspection.definition, true, source);
            assert.equal(inspection.displayOnly, false, source);
            applyBuiltDefinition(module, source, ++requestId);
        }

        const bare = module.parseEval(
            `${slash}xyz`, ++requestId, false, 0);
        assert.equal(bare.success, true, bare.error);
        assert.equal(bare.output, "zyx\n");
        const longer = module.parseEval(
            `${slash}foo x`, ++requestId, false, 0);
        assert.equal(longer.success, true, longer.error);
        assert.equal(longer.output, "x\n",
            "the exact longer name must win over the bare-name prefix");
        const maximumLength = module.parseEval(
            `${slash}12345678901234 x`, ++requestId, false, 0);
        assert.equal(maximumLength.success, true, maximumLength.error);
        assert.equal(maximumLength.output, "x\n",
            "a direct slash plus fourteen bytes must fit the name limit");

        const explicitLiteral = module.parseEval(
            `I ${quotedSlash}`, ++requestId, false, 0);
        assert.equal(explicitLiteral.success, true, explicitLiteral.error);
        assert.equal(explicitLiteral.output, `${quotedSlash}\n`,
            "the quoted raw word must preserve access to the literal");

        const show = module.parseEval(
            `show ${slash}foo`, ++requestId, false, 0);
        assert.equal(show.success, true, show.error);
        assert.equal(show.output, "arity:1 I\n");
        const inspected = module.parseEval(
            `inspect ${slash}foo x`, ++requestId, false, 0);
        assert.equal(inspected.success, true, inspected.error);
        assert.match(inspected.output, /free symbols: x/);
        assert.match(inspected.output, /\\foo \[captured\]/);

        const findInspection = module.inspectDefinition(
            `find among ${slash}foo ?x=x`);
        assert.equal(findInspection.success, true,
            findInspection.error);
        assert.equal(findInspection.find, true);
        assert.equal(findInspection.findAmong, true);
        assert.equal(findInspection.findCatalogSize, 1);
        const compared = module.parseEval(
            `compare ?x ${slash}foo x = x`,
            ++requestId, false, 0);
        assert.equal(compared.success, true, compared.error);
        assert.equal(compared.output, "both reduce to: xx\n");

        const setList = module.setList();
        assert.equal(
            setList,
            "references captured\n" +
            `set ${slash} = 3 C\n` +
            `set ${slash}foo = 1 I\n` +
            `set ${slash}12345678901234 = 1 I\n` +
            `define ${slash} bar = rab`,
        );

        const invalidLoad = module.loadSetList(
            `set SlashLoadBefore = 1 K\n` +
            `set ${slash}123456789012345 = 1 I\n` +
            `set SlashLoadAfter = 1 I\n`,
            "overlong backslash names",
        );
        assert.equal(invalidLoad.success, false);
        assert.equal(invalidLoad.loaded, 0);
        assert.match(
            invalidLoad.error,
            /combdsl::basis names are limited to 15 characters/,
        );
        assert.equal(module.setList(), setList,
            "a rejected load must roll the registry and journal back");
        for (const name of ["SlashLoadBefore", "SlashLoadAfter"]) {
            const absent = module.inspectDefinition(`show ${name}`);
            assert.equal(absent.success, false, name);
            assert.match(absent.error, /is not a defined name/, name);
        }

        const restored = await loadBuiltCombdslModule();
        const load = restored.loadSetList(setList, "backslash names");
        assert.equal(load.success, true, load.error);
        assert.equal(load.loaded, 5);
        const restoredBare = restored.parseEval(
            `${slash}xyz`, ++requestId, false, 0);
        assert.equal(restoredBare.success, true, restoredBare.error);
        assert.equal(restoredBare.output, "zyx\n");
        const restoredLonger = restored.parseEval(
            `${slash}foo x`, ++requestId, false, 0);
        assert.equal(restoredLonger.success, true, restoredLonger.error);
        assert.equal(restoredLonger.output, "x\n");
        const restoredMaximum = restored.parseEval(
            `${slash}12345678901234 x`, ++requestId, false, 0);
        assert.equal(restoredMaximum.success, true, restoredMaximum.error);
        assert.equal(restoredMaximum.output, "x\n");
        const restoredLiteral = restored.parseEval(
            `I ${quotedSlash}`, ++requestId, false, 0);
        assert.equal(restoredLiteral.success, true,
            restoredLiteral.error);
        assert.equal(restoredLiteral.output, `${quotedSlash}\n`);
    });

test("built Studio Wasm parses and reloads directly quoted strings",
    async () => {
        const module = await loadBuiltCombdslModule();
        const mixedWord = String.raw`"fo\"\\o"`;
        const slashLeadingWord = String.raw`"\\fo\"\\bar\\"`;
        let requestId = 350;

        const mixed = module.parseEval(
            `I ${mixedWord}`, ++requestId, false, 0);
        assert.equal(mixed.success, true, mixed.error);
        assert.equal(mixed.output, `fo"\\o\n`);

        const slashLeading = module.parseEval(
            `I ${slashLeadingWord}`, ++requestId, false, 0);
        assert.equal(slashLeading.success, true, slashLeading.error);
        assert.equal(slashLeading.output, `${slashLeadingWord}\n`);

        const doubledTrailing = module.parseEval(
            String.raw`I "tail\\"`, ++requestId, false, 0);
        assert.equal(doubledTrailing.success, true,
            doubledTrailing.error);
        assert.equal(doubledTrailing.output, "tail\\\n");

        for (const source of [
            String.raw`"a\b"`,
            String.raw`"tail\"`,
            String.raw`""`,
        ]) {
            const invalid = module.parseEval(
                source, ++requestId, false, 0);
            assert.equal(invalid.success, false, source);
            assert.match(invalid.error, /Parse error/, source);
        }

        for (const source of [
            `set WasmQuotedWord = 0 ${mixedWord}`,
            `set WasmSlashWord = 0 ${slashLeadingWord}`,
        ]) {
            const inspection = module.inspectDefinition(source);
            assert.equal(inspection.success, true, source);
            assert.equal(inspection.definition, true, source);
            applyBuiltDefinition(module, source, ++requestId);
        }

        const setList = module.setList();
        assert.equal(
            setList,
            "references captured\n" +
            `set WasmQuotedWord = 0 ${mixedWord}\n` +
            `set WasmSlashWord = 0 ${slashLeadingWord}`,
        );

        const restored = await loadBuiltCombdslModule();
        const load = restored.loadSetList(setList, "quoted strings");
        assert.equal(load.success, true, load.error);
        assert.equal(load.loaded, 3);
        for (const [source, expected] of [
            ["WasmQuotedWord", `fo"\\o\n`],
            ["WasmSlashWord", `${slashLeadingWord}\n`],
        ]) {
            const result = restored.parseEval(
                source, ++requestId, false, 0);
            assert.equal(result.success, true, result.error);
            assert.equal(result.output, expected, source);
        }
    });

test("built Studio Wasm repeatedly rescans duplicate takeout at parser boundaries",
    async () => {
        const module = await loadBuiltCombdslModule();
        let requestId = 380;

        const steps = module.beginLimitedEval(
            "abstract steps ?x = BxB",
            ++requestId,
            1000,
            false,
        );
        assert.equal(steps.success, true, steps.error);
        assert.equal(
            steps.output,
            "takeout x from BxB: CBB\n" +
            "optimize: CBB -> WCB\n" +
            "optimize: WC -> N\n" +
            "?=NB\n",
        );

        const rescannedSteps = module.beginLimitedEval(
            "abstract steps ?xy = xxxy",
            ++requestId,
            1000,
            false,
        );
        assert.equal(
            rescannedSteps.success, true, rescannedSteps.error);
        assert.equal(
            rescannedSteps.output,
            "takeout y from xxxy: xxx\n" +
            "optimize: xxx -> Nxx\n" +
            "optimize: Nxx -> WNx\n" +
            "takeout x from WNx: WN\n" +
            "?=WN\n",
        );

        const hSteps = module.beginLimitedEval(
            "abstract steps ?x = C*Hxx",
            ++requestId,
            1000,
            false,
        );
        assert.equal(hSteps.success, true, hSteps.error);
        assert.equal(
            hSteps.output,
            "takeout x from C*Hxx: W(C*H)\n" +
            "optimize: W(C*H) -> HH\n" +
            "optimize: HH -> MH\n" +
            "?=MH\n",
        );

        const hCompound = module.parseEval(
            "abstract ?x = C*(a(bc))xx",
            ++requestId,
            false,
            0,
        );
        assert.equal(hCompound.success, true, hCompound.error);
        assert.equal(hCompound.output, "?=H(a(bc))\n");

        const rescannedPlain = module.parseEval(
            "abstract ?xy = xxxy", ++requestId, false, 0);
        assert.equal(
            rescannedPlain.success, true, rescannedPlain.error);
        assert.equal(rescannedPlain.output, "?=WN\n");

        const rescannedMinisteps = module.parseEval(
            "abstract ministeps ?xy = xxxy",
            ++requestId,
            false,
            0,
        );
        assert.equal(
            rescannedMinisteps.success,
            true,
            rescannedMinisteps.error,
        );
        assert.equal(
            rescannedMinisteps.output,
            "takeout y from xxxy: xxx\n" +
            "optimize: xxx -> Nxx\n" +
            "optimize: Nxx -> WNx\n" +
            "takeout x from WNx: WN\n" +
            "?=WN\n",
        );

        const ministeps = module.parseEval(
            "abstract ministeps ?xy = ya(ya)",
            ++requestId,
            false,
            0,
        );
        assert.equal(ministeps.success, true, ministeps.error);
        assert.equal(
            ministeps.output,
            "takeout y from ya(ya): " +
            "S[takeout y from ya][takeout y from ya]\n" +
            "= S(Ta)[takeout y from ya]\n" +
            "= S(Ta)(Ta)\n" +
            "optimize: S(Ta)(Ta) -> WS(Ta)\n" +
            "takeout x from WS(Ta): K(WS(Ta))\n" +
            "?=K(WS(Ta))\n",
        );

        const definition = module.beginLimitedEval(
            "define WasmDup x = xa(xa)",
            ++requestId,
            1000,
            false,
        );
        assert.equal(definition.success, true, definition.error);
        assert.equal(definition.definition, true);
        assert.equal(definition.output, "");

        const shown = module.parseEval(
            "show WasmDup", ++requestId, false, 0);
        assert.equal(shown.success, true, shown.error);
        assert.equal(shown.output, "arity:1 WS(Ta)\n");

        const applied = module.parseEval(
            "WasmDup b", ++requestId, false, 0);
        assert.equal(applied.success, true, applied.error);
        assert.equal(applied.output, "ba(ba)\n");

        const rescannedDefinition = module.beginLimitedEval(
            "define WasmRescan xy = xxxy",
            ++requestId,
            1000,
            false,
        );
        assert.equal(
            rescannedDefinition.success,
            true,
            rescannedDefinition.error,
        );
        assert.equal(rescannedDefinition.definition, true);
        assert.equal(rescannedDefinition.output, "");

        const rescannedShown = module.parseEval(
            "show WasmRescan", ++requestId, false, 0);
        assert.equal(
            rescannedShown.success, true, rescannedShown.error);
        assert.equal(rescannedShown.output, "arity:2 WN\n");

        const rescannedApplied = module.parseEval(
            "WasmRescan a b", ++requestId, false, 0);
        assert.equal(
            rescannedApplied.success, true, rescannedApplied.error);
        assert.equal(rescannedApplied.output, "aaab\n");
    });

test("built Studio Wasm rescans duplicates after final optimization",
    async () => {
        const module = await loadBuiltCombdslModule();
        let requestId = 460;

        const steps = module.beginLimitedEval(
            "abstract steps ?xy = yxxx",
            ++requestId,
            1000,
            false,
        );
        assert.equal(steps.success, true, steps.error);
        assert.equal(
            steps.output,
            "takeout y from yxxx: C(C(Tx)x)x\n" +
            "takeout x from C(C(Tx)x)x: W(BC(W(BCT)))\n" +
            "optimize: BC -> C*\n" +
            "optimize: BC -> C*\n" +
            "optimize: C*T -> V\n" +
            "optimize: WV -> W1\n" +
            "optimize: W(C*W1) -> HW1\n" +
            "?=HW1\n",
        );

        const plain = module.parseEval(
            "abstract ?xy = yxxx", ++requestId, false, 0);
        assert.equal(plain.success, true, plain.error);
        assert.equal(plain.output, "?=HW1\n");

        const ministeps = module.parseEval(
            "abstract ministeps ?xy = yxxx",
            ++requestId,
            false,
            0,
        );
        assert.equal(ministeps.success, true, ministeps.error);
        assert.equal(
            ministeps.output,
            "takeout y from yxxx: C[takeout y from yxx]x\n" +
            "= C(C[takeout y from yx]x)x\n" +
            "= C(C(Tx)x)x\n" +
            "takeout x from C(C(Tx)x)x: " +
            "W[takeout x from C(C(Tx)x)]\n" +
            "= W(BC[takeout x from C(Tx)x])\n" +
            "= W(BC(W[takeout x from C(Tx)]))\n" +
            "= W(BC(W(BC[takeout x from Tx])))\n" +
            "= W(BC(W(BCT)))\n" +
            "optimize: BC -> C*\n" +
            "optimize: BC -> C*\n" +
            "optimize: C*T -> V\n" +
            "optimize: WV -> W1\n" +
            "optimize: W(C*W1) -> HW1\n" +
            "?=HW1\n",
        );

        const definition = module.beginLimitedEval(
            "define WasmFinalDup xy = yxxx",
            ++requestId,
            1000,
            false,
        );
        assert.equal(definition.success, true, definition.error);
        assert.equal(definition.definition, true);
        assert.equal(definition.output, "");

        const shown = module.parseEval(
            "show WasmFinalDup", ++requestId, false, 0);
        assert.equal(shown.success, true, shown.error);
        assert.equal(shown.output, "arity:2 HW1\n");

        const applied = module.parseEval(
            "WasmFinalDup a b", ++requestId, false, 0);
        assert.equal(applied.success, true, applied.error);
        assert.equal(applied.output, "baaa\n");
    });

test("built Studio Wasm rejects integer basis names without mutation",
    async () => {
        const module = await loadBuiltCombdslModule();
        const numericNameError =
            /combdsl::basis names cannot be non-negative integer literals/;
        for (const [source, position] of [
            ["set 0 = I", 5],
            ["set 000 = I", 5],
            ["set 123456789012345 = I", 5],
            ["define 0 = I", 8],
            ["define 000 x = x", 8],
            ["define 7x = x", 8],
            ["remove 0", 8],
            ["revisions 000", 11],
            ["dependson 123456789012345", 11],
            ["usedby 0", 8],
        ]) {
            const inspection = module.inspectDefinition(source);
            assert.equal(inspection.success, false, source);
            assert.match(inspection.error, numericNameError, source);
            assert.match(
                inspection.error,
                new RegExp(`Parse error at position ${position}:`),
                source,
            );
        }

        let requestId = 360;
        const validDefinitions = [
            "set +8 = 1 I",
            "set -8 = 1 I",
            "set 8.0 = 1 I",
            "set 8e2 = 1 I",
            "set 8x = 1 I",
            "set \u0667 = 1 I",
            "define 7x y = y",
        ];
        for (const source of validDefinitions) {
            const inspection = module.inspectDefinition(source);
            assert.equal(inspection.success, true, source);
            assert.equal(inspection.definition, true, source);
            applyBuiltDefinition(module, source, ++requestId);
        }

        for (const source of ["+8 x", "-8 x", "8.0 x", "8e2 x", "8x x",
            "\u0667 x", "7x x"]) {
            const result = module.parseEval(
                source, ++requestId, false, 0);
            assert.equal(result.success, true, result.error);
            assert.equal(result.output, "x\n", source);
        }

        for (const source of ["0", "000", "123456789012345"]) {
            const numeric = module.parseEval(
                source, ++requestId, false, 0);
            assert.equal(numeric.success, true, numeric.error);
            assert.equal(numeric.output, noFurtherReductions, source);
        }
        const compared = module.parseEval(
            "compare ?x K 000 = K 0", ++requestId, false, 0);
        assert.equal(compared.success, true, compared.error);
        assert.equal(compared.output, "both reduce to: 0\n");
        const numericFindTarget = module.inspectDefinition(
            "find 1 ?x = 0");
        assert.equal(numericFindTarget.success, true,
            numericFindTarget.error);
        assert.equal(numericFindTarget.find, true);
        const numericCatalog = module.inspectDefinition(
            "find among 7 ?x = x");
        assert.equal(numericCatalog.success, false);
        assert.match(numericCatalog.error, /7 is not a defined name/);

        const show = module.inspectDefinition("show 0");
        assert.equal(show.success, false);
        assert.match(show.error, /0 is not a defined name/);
        const expectedSetList =
            "references captured\n" +
            "set +8 = 1 I\n" +
            "set -8 = 1 I\n" +
            "set 8.0 = 1 I\n" +
            "set 8e2 = 1 I\n" +
            "set 8x = 1 I\n" +
            "set \u0667 = 1 I\n" +
            "define 7x y = y";
        assert.equal(module.setList(), expectedSetList);

        const invalidLoad = module.loadSetList(
            "set LoadedBeforeNumeric = 1 K\n" +
            "set 000 = I\n" +
            "set LoadedAfterNumeric = 1 I\n",
            "numeric names",
        );
        assert.equal(invalidLoad.success, false);
        assert.equal(invalidLoad.loaded, 0);
        assert.match(invalidLoad.error, numericNameError);
        assert.match(invalidLoad.error, /on line 2 at position 5/);
        assert.equal(module.setList(), expectedSetList,
            "a rejected load must restore the complete registry and journal");
        for (const name of ["LoadedBeforeNumeric", "LoadedAfterNumeric"]) {
            const absent = module.inspectDefinition(`show ${name}`);
            assert.equal(absent.success, false, name);
            assert.match(absent.error, /is not a defined name/, name);
        }

        const restored = await loadBuiltCombdslModule();
        const load = restored.loadSetList(expectedSetList, "numeric-like names");
        assert.equal(load.success, true, load.error);
        assert.equal(load.loaded, 8);
        for (const source of ["+8 x", "-8 x", "8.0 x", "8e2 x", "8x x",
            "\u0667 x", "7x x"]) {
            const result = restored.parseEval(
                source, ++requestId, false, 0);
            assert.equal(result.success, true, result.error);
            assert.equal(result.output, "x\n", source);
        }
    });

test("built inspection identifies only restricted Find metadata",
    async () => {
        const module = await loadBuiltCombdslModule();
        const ordinary = module.inspectDefinition("find ?xy = x(yx)");
        assert.equal(ordinary.success, true);
        assert.equal(ordinary.find, true);
        assert.equal(ordinary.findAmong, false);
        assert.equal(ordinary.findAllSizes, false);
        assert.equal(ordinary.findCatalogSize, 0);

        const restricted = module.inspectDefinition(
            "find all among A A@1 K ?xy = x(yx)");
        assert.equal(restricted.success, true);
        assert.equal(restricted.find, true);
        assert.equal(restricted.findAmong, true);
        assert.equal(restricted.findAllSizes, true);
        assert.equal(restricted.findCatalogSize, 2,
            "catalog metadata must count resolved, deduplicated birds");

        const malformed = module.inspectDefinition(
            "find all among MissingBird ?x = x");
        assert.equal(malformed.success, false);
        assert.equal(malformed.findAmong, false);
        assert.equal(malformed.findAllSizes, false);
        assert.equal(malformed.findCatalogSize, 0);
    });

test("built prepared Find reuses one target across serial and sharded sizes",
    async () => {
        const module = await loadBuiltCombdslModule();
        applyBuiltDefinition(module, "set PoolLeaf = 100 I", 301);
        const command =
            "find among PoolLeaf ?x = " +
            "PoolLeaf PoolLeaf PoolLeaf x";

        const preparation = module.prepareFindAmong(command, 10000);
        assert.deepEqual({
            success: preparation.success,
            timedOut: preparation.timedOut,
            searchable: preparation.searchable,
            allSizes: preparation.allSizes,
            catalogSize: preparation.catalogSize,
            error: preparation.error,
        }, {
            success: true,
            timedOut: false,
            searchable: true,
            allSizes: false,
            catalogSize: 1,
            error: "",
        });

        for (const leafCount of [1, 2]) {
            const result = module.findAmongPreparedSizeShard(
                leafCount, 0, 1, 10000);
            assert.equal(result.success, true, result.error);
            assert.equal(result.timedOut, false);
            assert.deepEqual(takeFindAmongShardMatches(result), [],
                `size ${leafCount} should remain a completed serial size`);
        }

        const serialResult = module.findAmongPreparedSizeShard(
            3, 0, 1, 10000);
        assert.equal(serialResult.success, true, serialResult.error);
        assert.equal(serialResult.timedOut, false);
        const serialMatches = takeFindAmongShardMatches(serialResult);
        assert.deepEqual(serialMatches, [{
            index: 0,
            expression: "PoolLeaf PoolLeaf PoolLeaf",
        }]);

        const shardResults = [1, 0].map(shardIndex => {
            const result = module.findAmongPreparedSizeShard(
                3, shardIndex, 2, 10000);
            assert.equal(result.success, true, result.error);
            assert.equal(result.timedOut, false);
            return takeFindAmongShardMatches(result);
        });
        const merged = shardResults.flat()
            .sort((left, right) => left.index - right.index);
        assert.deepEqual(merged, serialMatches,
            "out-of-order shard completion must merge by global index");

        const serial = module.parseEval(command, 302, false, 0);
        assert.equal(serial.success, true, serial.error);
        assert.equal(
            serial.output,
            serialMatches.map(match => `?=${match.expression}\n`).join(""),
            "the sharded browser primitive must match serial command output");

        module.resetFindAmong();
        const afterReset = module.findAmongPreparedSizeShard(
            3, 0, 1, 10000);
        assert.equal(afterReset.success, false);
        assert.match(afterReset.error, /no find among command is prepared/);
        assert.deepEqual(takeFindAmongShardMatches(afterReset), []);
    });

test("built prepared Find prunes only targeted M and W shapes",
    async () => {
        const module = await loadBuiltCombdslModule();

        const flatPreparation = module.prepareFindAmong(
            "find among M K ?x = MKx", 10000);
        assert.equal(
            flatPreparation.success, true, flatPreparation.error);
        assert.equal(flatPreparation.searchable, true);
        const flatResult = module.findAmongPreparedSizeShard(
            2, 0, 1, 10000);
        assert.equal(flatResult.success, true, flatResult.error);
        assert.equal(flatResult.timedOut, false);
        const flatMatches = takeFindAmongShardMatches(flatResult);
        assert.equal(
            flatMatches.some(match => match.expression === "MK"),
            false,
            "M applied to an atomic catalog bird must be pruned",
        );
        assert.equal(
            flatMatches.some(match => match.expression === "KK"),
            true,
            "an equivalent unpruned candidate should still be found",
        );

        const compositePreparation = module.prepareFindAmong(
            "find all among M A ?x = M(AA)x", 10000);
        assert.equal(
            compositePreparation.success,
            true,
            compositePreparation.error,
        );
        assert.equal(compositePreparation.searchable, true);
        const compositeResult = module.findAmongPreparedSizeShard(
            3, 0, 1, 10000);
        assert.equal(
            compositeResult.success, true, compositeResult.error);
        assert.equal(compositeResult.timedOut, false);
        const compositeMatches =
            takeFindAmongShardMatches(compositeResult);
        assert.equal(
            compositeMatches.some(
                match => match.expression === "M(AA)"),
            true,
            "an unrelated M-composite must remain eligible",
        );

        const kAtomicPreparation = module.prepareFindAmong(
            "find all among M W K A ?x = Ax", 10000);
        assert.equal(
            kAtomicPreparation.success,
            true,
            kAtomicPreparation.error,
        );
        assert.equal(kAtomicPreparation.searchable, true);
        const kAtomicResult = module.findAmongPreparedSizeShard(
            3, 0, 1, 10000);
        assert.equal(
            kAtomicResult.success, true, kAtomicResult.error);
        assert.equal(kAtomicResult.timedOut, false);
        const kAtomicMatches =
            takeFindAmongShardMatches(kAtomicResult);
        assert.equal(
            kAtomicMatches.some(
                match => match.expression === "M(KA)"),
            false,
            "M(K A) must be pruned",
        );
        assert.equal(
            kAtomicMatches.some(
                match => match.expression === "W(KA)"),
            false,
            "W(K A) must be pruned",
        );
        assert.equal(
            kAtomicMatches.some(
                match => match.expression === "KAA"),
            true,
            "an equivalent untargeted size-three candidate must remain",
        );

        const kCompositePreparation = module.prepareFindAmong(
            "find all among M W K A ?x = AAx", 10000);
        assert.equal(
            kCompositePreparation.success,
            true,
            kCompositePreparation.error,
        );
        assert.equal(kCompositePreparation.searchable, true);
        const kCompositeResult = module.findAmongPreparedSizeShard(
            4, 0, 1, 10000);
        assert.equal(
            kCompositeResult.success,
            true,
            kCompositeResult.error,
        );
        assert.equal(kCompositeResult.timedOut, false);
        const kCompositeMatches =
            takeFindAmongShardMatches(kCompositeResult);
        assert.equal(
            kCompositeMatches.some(
                match => match.expression === "M(K(AA))"),
            false,
            "M(K A) must be pruned for compound A",
        );
        assert.equal(
            kCompositeMatches.some(
                match => match.expression === "W(K(AA))"),
            false,
            "W(K A) must be pruned for compound A",
        );
        assert.equal(
            kCompositeMatches.some(
                match => match.expression === "K(AA)A"),
            true,
            "an equivalent untargeted size-four candidate must remain",
        );
    });

test("built prepared Find prunes structural identity trips",
    async () => {
        const module = await loadBuiltCombdslModule();
        const atomicPreparation = module.prepareFindAmong(
            "find all among A B C C* C** G I K Q R T W1 Z " +
            "?xyzw = Axyzw",
            10000,
        );
        assert.equal(
            atomicPreparation.success,
            true,
            atomicPreparation.error,
        );
        assert.equal(atomicPreparation.searchable, true);
        const atomicResult = module.findAmongPreparedSizeShard(
            3, 0, 1, 10000);
        assert.equal(atomicResult.success, true, atomicResult.error);
        assert.equal(atomicResult.timedOut, false);
        const atomicMatches = takeFindAmongShardMatches(atomicResult);
        const atomicExpressions = new Set(
            atomicMatches.map(match => match.expression));
        for (const excluded of [
            "BAI",
            "C(CA)",
            "C*CA",
            "C*(C*A)",
            "C**C*A",
            "GTA",
            "QAI",
            "RAT",
            "TAI",
            "W1 AK",
            "ZCA",
        ]) {
            assert.equal(
                atomicExpressions.has(excluded),
                false,
                `${excluded} must be pruned`,
            );
        }
        assert.equal(
            atomicExpressions.has("KAA"),
            true,
            "an equivalent untargeted atomic-capture candidate must remain",
        );

        const compoundPreparation = module.prepareFindAmong(
            "find all among A B C C* I K Z ?xyzw = AAxyzw",
            10000,
        );
        assert.equal(
            compoundPreparation.success,
            true,
            compoundPreparation.error,
        );
        assert.equal(compoundPreparation.searchable, true);
        const compoundResult = module.findAmongPreparedSizeShard(
            4, 0, 1, 10000);
        assert.equal(
            compoundResult.success, true, compoundResult.error);
        assert.equal(compoundResult.timedOut, false);
        const compoundExpressions = new Set(
            takeFindAmongShardMatches(compoundResult).map(
                match => match.expression));
        for (const excluded of [
            "B(AA)I",
            "C(C(AA))",
            "C*C(AA)",
            "ZC(AA)",
        ]) {
            assert.equal(
                compoundExpressions.has(excluded),
                false,
                `${excluded} must be pruned`,
            );
        }
        assert.equal(
            compoundExpressions.has("K(AA)A"),
            true,
            "an equivalent untargeted compound-capture candidate must remain",
        );
    });

test("built prepared Find retains Z C-star identity trips",
    async () => {
        const module = await loadBuiltCombdslModule();
        const atomicPreparation = module.prepareFindAmong(
            "find all among A C C* K Z ?xyzw = Axyzw",
            10000,
        );
        assert.equal(
            atomicPreparation.success,
            true,
            atomicPreparation.error,
        );
        assert.equal(atomicPreparation.searchable, true);
        const atomicResult = module.findAmongPreparedSizeShard(
            3, 0, 1, 10000);
        assert.equal(atomicResult.success, true, atomicResult.error);
        assert.equal(atomicResult.timedOut, false);
        const atomicExpressions = new Set(
            takeFindAmongShardMatches(atomicResult).map(
                match => match.expression));
        assert.equal(
            atomicExpressions.has("ZCA"),
            false,
            "ZCA must stay pruned",
        );
        assert.equal(
            atomicExpressions.has("ZC*A"),
            true,
            "ZC*A must remain an eligible identity trip",
        );
        assert.equal(
            atomicExpressions.has("KAA"),
            true,
            "an eligible atomic-capture control must remain",
        );

        const compoundPreparation = module.prepareFindAmong(
            "find all among A C C* K Z ?xyzw = AAxyzw",
            10000,
        );
        assert.equal(
            compoundPreparation.success,
            true,
            compoundPreparation.error,
        );
        assert.equal(compoundPreparation.searchable, true);
        const compoundResult = module.findAmongPreparedSizeShard(
            4, 0, 1, 10000);
        assert.equal(
            compoundResult.success, true, compoundResult.error);
        assert.equal(compoundResult.timedOut, false);
        const compoundExpressions = new Set(
            takeFindAmongShardMatches(compoundResult).map(
                match => match.expression));
        assert.equal(
            compoundExpressions.has("ZC(AA)"),
            false,
            "ZC(AA) must stay pruned",
        );
        assert.equal(
            compoundExpressions.has("ZC*(AA)"),
            true,
            "ZC*(AA) must remain eligible for a compound capture",
        );
        assert.equal(
            compoundExpressions.has("K(AA)A"),
            true,
            "an eligible compound-capture control must remain",
        );
    });

test("built prepared Find prunes G K trips exactly",
    async () => {
        const module = await loadBuiltCombdslModule();
        const targetCommand =
            "find all among A G I K ?xy = y";

        const atomicPreparation = module.prepareFindAmong(
            targetCommand, 10000);
        assert.equal(
            atomicPreparation.success,
            true,
            atomicPreparation.error,
        );
        assert.equal(atomicPreparation.searchable, true);
        const atomicResult = module.findAmongPreparedSizeShard(
            3, 0, 1, 10000);
        assert.equal(atomicResult.success, true, atomicResult.error);
        assert.equal(atomicResult.timedOut, false);
        const atomicExpressions = new Set(
            takeFindAmongShardMatches(atomicResult).map(
                match => match.expression));
        assert.equal(
            atomicExpressions.has("GKA"),
            false,
            "GKA must be pruned",
        );
        assert.equal(
            atomicExpressions.has("AIK"),
            true,
            "an eligible size-three K-I equivalent must remain",
        );

        const compoundPreparation = module.prepareFindAmong(
            targetCommand, 10000);
        assert.equal(
            compoundPreparation.success,
            true,
            compoundPreparation.error,
        );
        assert.equal(compoundPreparation.searchable, true);
        const compoundResult = module.findAmongPreparedSizeShard(
            4, 0, 1, 10000);
        assert.equal(
            compoundResult.success, true, compoundResult.error);
        assert.equal(compoundResult.timedOut, false);
        const compoundExpressions = new Set(
            takeFindAmongShardMatches(compoundResult).map(
                match => match.expression));
        assert.equal(
            compoundExpressions.has("GK(AA)"),
            false,
            "GK(AA) must be pruned",
        );
        assert.equal(
            compoundExpressions.has("K(KI)A"),
            true,
            "an eligible size-four K-I equivalent must remain",
        );

        const oppositePreparation = module.prepareFindAmong(
            "find all among A G K ?xyzw = G(KA)xyzw",
            10000,
        );
        assert.equal(
            oppositePreparation.success,
            true,
            oppositePreparation.error,
        );
        assert.equal(oppositePreparation.searchable, true);
        const oppositeResult = module.findAmongPreparedSizeShard(
            3, 0, 1, 10000);
        assert.equal(
            oppositeResult.success, true, oppositeResult.error);
        assert.equal(oppositeResult.timedOut, false);
        assert.equal(
            takeFindAmongShardMatches(oppositeResult).some(
                match => match.expression === "G(KA)"),
            true,
            "G(KA) must retain its opposite association",
        );
    });

test("built prepared Find prunes constant identity trips exactly",
    async () => {
        const module = await loadBuiltCombdslModule();
        const identityCommand =
            "find all among A C I K R ?xyzw = xyzw";

        const atomicPreparation = module.prepareFindAmong(
            identityCommand, 10000);
        assert.equal(
            atomicPreparation.success,
            true,
            atomicPreparation.error,
        );
        assert.equal(atomicPreparation.searchable, true);
        const atomicResult = module.findAmongPreparedSizeShard(
            3, 0, 1, 10000);
        assert.equal(atomicResult.success, true, atomicResult.error);
        assert.equal(atomicResult.timedOut, false);
        const atomicExpressions = new Set(
            takeFindAmongShardMatches(atomicResult).map(
                match => match.expression));
        for (const excluded of ["CKA", "RAK"]) {
            assert.equal(
                atomicExpressions.has(excluded),
                false,
                `${excluded} must be pruned`,
            );
        }
        assert.equal(
            atomicExpressions.has("KIA"),
            true,
            "an eligible size-three identity must remain",
        );

        const compoundPreparation = module.prepareFindAmong(
            identityCommand, 10000);
        assert.equal(
            compoundPreparation.success,
            true,
            compoundPreparation.error,
        );
        assert.equal(compoundPreparation.searchable, true);
        const compoundResult = module.findAmongPreparedSizeShard(
            4, 0, 1, 10000);
        assert.equal(
            compoundResult.success, true, compoundResult.error);
        assert.equal(compoundResult.timedOut, false);
        const compoundExpressions = new Set(
            takeFindAmongShardMatches(compoundResult).map(
                match => match.expression));
        for (const excluded of ["CK(AA)", "R(AA)K"]) {
            assert.equal(
                compoundExpressions.has(excluded),
                false,
                `${excluded} must be pruned`,
            );
        }
        assert.equal(
            compoundExpressions.has("AIII"),
            true,
            "an eligible size-four identity must remain",
        );

        for (const [command, retained] of [
            [
                "find all among A C K ?xyzw = C(KA)xyzw",
                "C(KA)",
            ],
            [
                "find all among A K R ?xyzw = R(AK)xyzw",
                "R(AK)",
            ],
        ]) {
            const preparation = module.prepareFindAmong(command, 10000);
            assert.equal(preparation.success, true, preparation.error);
            assert.equal(preparation.searchable, true);
            const result = module.findAmongPreparedSizeShard(
                3, 0, 1, 10000);
            assert.equal(result.success, true, result.error);
            assert.equal(result.timedOut, false);
            assert.equal(
                takeFindAmongShardMatches(result).some(
                    match => match.expression === retained),
                true,
                `${retained} must retain its opposite association`,
            );
        }
    });

test("built prepared Find reports shared-budget exhaustion transactionally",
    async () => {
        const module = await loadBuiltCombdslModule();
        const command = "find all among I K ?x = x";

        const expiredPreparation = module.prepareFindAmong(command, 0);
        assert.equal(expiredPreparation.success, true);
        assert.equal(expiredPreparation.timedOut, true);
        assert.equal(expiredPreparation.searchable, false);

        const preparation = module.prepareFindAmong(command, 10000);
        assert.equal(preparation.success, true, preparation.error);
        assert.equal(preparation.searchable, true);
        const expiredShard = module.findAmongPreparedSizeShard(
            3, 0, 1, 0);
        assert.equal(expiredShard.success, true, expiredShard.error);
        assert.equal(expiredShard.timedOut, true);
        assert.deepEqual(takeFindAmongShardMatches(expiredShard), [],
            "a timed-out size must not expose partial matches to the merger");

        for (const invalidBudget of [-1, Number.NaN, Number.POSITIVE_INFINITY]) {
            const invalid = module.prepareFindAmong(
                command, invalidBudget);
            assert.equal(invalid.success, false);
            assert.match(invalid.error, /finite and nonnegative/);
        }
        const clearedByFailure = module.findAmongPreparedSizeShard(
            1, 0, 1, 10000);
        assert.equal(clearedByFailure.success, false,
            "a failed prepare must clear the previous retained search");
        assert.deepEqual(takeFindAmongShardMatches(clearedByFailure), []);
    });

test("built helper state restore preserves live, captured, and removed birds",
    async () => {
        const primary = await loadBuiltCombdslModule();
        const helper = await loadBuiltCombdslModule();
        let requestId = 320;
        for (const definition of [
            "references live",
            "set PoolTarget = 0 I",
            "set captured PoolCaptured = 0 PoolTarget",
            "set live PoolLive = 0 PoolTarget",
            "set PoolTarget = 0 K",
            "remove PoolTarget",
        ]) {
            applyBuiltDefinition(primary, definition, ++requestId);
        }

        const setList = primary.setList();
        const restored = helper.loadSetList(setList, "pool state");
        assert.equal(restored.success, true, restored.error);
        const command =
            "find all among PoolCaptured PoolLive PoolTarget@1 ?x = x";
        const results = [];
        for (const module of [primary, helper]) {
            const preparation = module.prepareFindAmong(command, 10000);
            assert.equal(preparation.success, true, preparation.error);
            assert.equal(preparation.searchable, true);
            assert.equal(preparation.catalogSize, 3);
            const shard = module.findAmongPreparedSizeShard(
                1, 0, 1, 10000);
            assert.equal(shard.success, true, shard.error);
            assert.equal(shard.timedOut, false);
            results.push(takeFindAmongShardMatches(shard));
            module.resetFindAmong();
        }
        assert.deepEqual(results[1], results[0],
            "a helper Wasm instance must search the exact restored state");
        assert.deepEqual(
            results[0].map(match => match.expression),
            ["PoolCaptured", "PoolTarget@1"],
            "live removed and captured retained revisions must stay distinct");
    });

test("Find helper role restores, prepares, shards, cleans up, and resets",
    async () => {
        const calls = [];
        let vectorDeletes = 0;
        const module = {
            setList: () => "",
            loadSetList(source, name) {
                calls.push(["load", source, name]);
                return {success: true, loaded: 3, line: 0, error: ""};
            },
            prepareFindAmong(source, budget) {
                calls.push(["prepare", source, budget]);
                return {
                    success: true,
                    timedOut: false,
                    searchable: true,
                    allSizes: false,
                    catalogSize: 2,
                    error: "",
                };
            },
            findAmongPreparedSizeShard(
                leafCount, shardIndex, shardCount, budget,
            ) {
                calls.push([
                    "size", leafCount, shardIndex, shardCount, budget,
                ]);
                const values = [
                    {index: 5, expression: "late"},
                    {index: 2, expression: "early"},
                ];
                return {
                    success: true,
                    timedOut: false,
                    error: "",
                    matches: {
                        size: () => values.length,
                        get: index => values[index],
                        delete() {
                            ++vectorDeletes;
                        },
                    },
                };
            },
            resetFindAmong() {
                calls.push(["reset"]);
            },
        };
        const harness = await createWorkerHarness(module, {
            workerHref:
                "https://example.test/worker.js?v=test&role=find-helper",
        });
        assert.deepEqual(
            harness.messages.map(message => message.type),
            ["find-helper-ready"],
        );

        const sendJob = async (jobId, type, fields = {}) => {
            await harness.send({jobId, type, ...fields});
            return harness.messages.find(
                message => message.type === "find-helper-result" &&
                    message.jobId === jobId);
        };
        const load = await sendJob(1, "find-helper-load", {
            source: "set PoolLeaf = 100 I\n",
        });
        assert.equal(load.result.success, true);
        const prepare = await sendJob(2, "find-helper-prepare", {
            source: "find among PoolLeaf ?x = x",
            budget: 9750,
        });
        assert.equal(prepare.result.catalogSize, 2);
        const size = await sendJob(3, "find-helper-size", {
            leafCount: 3,
            shardIndex: 1,
            shardCount: 3,
            budget: 9400,
        });
        assert.deepEqual(Array.from(
            size.result.matches,
            match => ({
                index: match.index,
                expression: match.expression,
            }),
        ), [
            {index: 5, expression: "late"},
            {index: 2, expression: "early"},
        ]);
        assert.equal(vectorDeletes, 1,
            "every Embind result vector must be explicitly released");
        const reset = await sendJob(4, "find-helper-reset");
        assert.equal(reset.result.success, true);
        assert.deepEqual(calls, [
            ["load", "set PoolLeaf = 100 I\n", "Find helper definitions"],
            ["prepare", "find among PoolLeaf ?x = x", 9750],
            ["size", 3, 1, 3, 9400],
            ["reset"],
        ]);
    });

test("restricted Find creates the hardware-bounded helper pool",
    async () => {
        for (const [hardwareConcurrency, expectedWorkers] of [
            [1, 1],
            [2, 1],
            [4, 3],
            [100, 8],
        ]) {
            const module = {setList: () => ""};
            const harness = await createWorkerHarness(module, {
                hardwareConcurrency,
            });
            const message = restrictedFindEvaluationMessage({
                id: 500 + expectedWorkers,
            });
            const evaluation = harness.send(message);
            await flushMicrotasks();
            assert.equal(
                harness.nestedWorkers.length,
                expectedWorkers,
                `hardwareConcurrency=${hardwareConcurrency}`,
            );
            assert.equal(harness.nestedWorkers.every(worker =>
                new URL(worker.url).searchParams.get("role") ===
                    "find-helper"), true);
            assert.equal(harness.nestedWorkers.every(worker =>
                new URL(worker.url).searchParams.get("v") === "test"), true);

            await harness.send({type: "pause", id: message.id});
            await evaluation;
            assert.equal(harness.nestedWorkers.every(
                worker => worker.terminated), true);
            assert.equal(harness.messages.some(output =>
                output.type === "paused" && output.id === message.id), true);
            assert.equal(harness.messages.some(output =>
                output.type === "result" && output.id === message.id), false);
        }
    });

test("restricted Find keeps sizes one and two serial then merges shards",
    async () => {
        let serialCalls = 0;
        const setList = "set PoolLeaf = 100 I\n";
        const module = {
            setList: () => setList,
            beginLimitedEval() {
                ++serialCalls;
                throw new Error("pooled Find must not use serial fallback");
            },
        };
        const harness = await createWorkerHarness(module, {
            hardwareConcurrency: 4,
        });
        const message = restrictedFindEvaluationMessage({id: 520});
        const evaluation = harness.send(message);
        await flushMicrotasks();
        assert.equal(harness.nestedWorkers.length, 3);
        assert.deepEqual(harness.timers.delays(), [10000, 10000, 10000],
            "startup watchdogs are separate from the semantic deadline");

        for (const helper of harness.nestedWorkers) {
            helper.send({type: "find-helper-ready"});
        }
        await flushMicrotasks();
        assert.equal(harness.timers.size, 0,
            "state restore must also precede the semantic deadline");
        for (const helper of harness.nestedWorkers) {
            const load = answerNestedRequest(helper, "find-helper-load", {
                success: true,
                loaded: 1,
                line: 0,
                error: "",
            });
            assert.equal(load.source, setList);
        }
        await flushMicrotasks();
        assert.deepEqual(harness.timers.delays(), [10000]);
        await prepareFindHelpers(harness, {catalogSize: 2});

        const firstSize = answerNestedRequest(
            harness.nestedWorkers[0],
            "find-helper-size",
            {success: true, timedOut: false, matches: [], error: ""},
        );
        assert.equal(firstSize.leafCount, 1);
        assert.equal(firstSize.shardIndex, 0);
        assert.equal(firstSize.shardCount, 1);
        assert.equal(harness.nestedWorkers.slice(1).every(worker =>
            worker.messages.every(request =>
                request.type !== "find-helper-size")), true);
        await flushMicrotasks();

        const secondSize = answerNestedRequest(
            harness.nestedWorkers[0],
            "find-helper-size",
            {success: true, timedOut: false, matches: [], error: ""},
        );
        assert.equal(secondSize.leafCount, 2);
        assert.equal(secondSize.shardCount, 1);
        await flushMicrotasks();

        const sizeThree = harness.nestedWorkers.map(
            (helper, shardIndex) => {
                const request = nextNestedRequest(
                    helper, "find-helper-size");
                assert.equal(request.leafCount, 3);
                assert.equal(request.shardIndex, shardIndex);
                assert.equal(request.shardCount, 3);
                return request;
            });
        harness.nestedWorkers[2].send({
            type: "find-helper-result",
            jobId: sizeThree[2].jobId,
            result: {
                success: true,
                timedOut: false,
                matches: [
                    {index: 2, expression: "middle"},
                    {index: 5, expression: "same"},
                ],
                error: "",
            },
        });
        harness.nestedWorkers[1].send({
            type: "find-helper-result",
            jobId: sizeThree[1].jobId,
            result: {
                success: true,
                timedOut: false,
                matches: [{index: 4, expression: "last"}],
                error: "",
            },
        });
        harness.nestedWorkers[0].send({
            type: "find-helper-result",
            jobId: sizeThree[0].jobId,
            result: {
                success: true,
                timedOut: false,
                matches: [
                    {index: 6, expression: "same"},
                    {index: 0, expression: "first"},
                ],
                error: "",
            },
        });
        await evaluation;

        const result = harness.messages.find(output =>
            output.type === "result" && output.id === message.id);
        assert.equal(result.result.success, true, result.result.error);
        assert.equal(
            result.result.output,
            "?=first\n?=middle\n?=last\n?=same\n",
            "global indexes must restore serial order before deduplication",
        );
        assert.equal(serialCalls, 0);
        assert.equal(harness.nestedWorkers.every(
            worker => worker.terminated), true);
        assert.equal(harness.timers.size, 0);
    });

test("restricted Find bounds active size-three shards by candidate work",
    async () => {
        const harness = await createWorkerHarness(
            {setList: () => ""},
            {hardwareConcurrency: 100},
        );
        const message = restrictedFindEvaluationMessage({id: 521});
        const evaluation = harness.send(message);
        await flushMicrotasks();
        assert.equal(harness.nestedWorkers.length, 8,
            "the reusable pool is bounded independently of one size");
        await readyAndRestoreFindHelpers(harness, "");
        await prepareFindHelpers(harness, {catalogSize: 1});
        for (const leafCount of [1, 2]) {
            const request = answerNestedRequest(
                harness.nestedWorkers[0],
                "find-helper-size",
                {success: true, timedOut: false, matches: [], error: ""},
            );
            assert.equal(request.leafCount, leafCount);
            assert.equal(request.shardCount, 1);
            await flushMicrotasks();
        }

        const active = harness.nestedWorkers.filter(worker =>
            worker.messages.some(request =>
                request.type === "find-helper-size" &&
                request.leafCount === 3));
        assert.equal(active.length, 2,
            "one-bird size three has only two tree-shape work items");
        assert.deepEqual(active.map(worker =>
            nextNestedRequest(worker, "find-helper-size").shardCount),
        [2, 2]);

        await harness.send({type: "pause", id: message.id});
        await evaluation;
        assert.equal(harness.nestedWorkers.every(
            worker => worker.terminated), true);
    });

test("restricted Find keeps completed sizes and discards a timed-out size",
    async () => {
        let currentTime = 0;
        const harness = await createWorkerHarness(
            {setList: () => ""},
            {
                hardwareConcurrency: 4,
                now: () => currentTime,
            },
        );
        const message = restrictedFindEvaluationMessage({
            id: 522,
            source: "find all among A B ?x = x",
        });
        const evaluation = harness.send(message);
        await flushMicrotasks();
        await readyAndRestoreFindHelpers(harness, "");
        currentTime = 500;
        await prepareFindHelpers(harness, {
            allSizes: true,
            catalogSize: 2,
        });

        const sizeOne = answerNestedRequest(
            harness.nestedWorkers[0],
            "find-helper-size",
            {
                success: true,
                timedOut: false,
                matches: [{index: 0, expression: "A"}],
                error: "",
            },
        );
        assert.equal(sizeOne.budget, 9500);
        currentTime = 1000;
        await flushMicrotasks();
        const sizeTwo = answerNestedRequest(
            harness.nestedWorkers[0],
            "find-helper-size",
            {
                success: true,
                timedOut: false,
                matches: [
                    {index: 0, expression: "A"},
                    {index: 1, expression: "B"},
                ],
                error: "",
            },
        );
        assert.equal(sizeTwo.budget, 9000);
        currentTime = 1500;
        await flushMicrotasks();

        const sizeThree = harness.nestedWorkers.map(helper =>
            nextNestedRequest(helper, "find-helper-size"));
        assert.equal(sizeThree.every(request =>
            request.leafCount === 3 &&
            request.shardCount === 3 &&
            request.budget === 8500), true);
        harness.nestedWorkers[0].send({
            type: "find-helper-result",
            jobId: sizeThree[0].jobId,
            result: {
                success: true,
                timedOut: false,
                matches: [{index: 0, expression: "partial"}],
                error: "",
            },
        });
        harness.timers.runNext();
        await evaluation;

        const result = harness.messages.find(output =>
            output.type === "result" && output.id === message.id);
        assert.equal(result.result.output, "?=A\n?=B\n");
        assert.equal(result.result.output.includes("partial"), false);
        const resultCount = harness.messages.filter(output =>
            output.type === "result" && output.id === message.id).length;
        harness.nestedWorkers[1].send({
            type: "find-helper-result",
            jobId: sizeThree[1].jobId,
            result: {
                success: true,
                timedOut: false,
                matches: [{index: 1, expression: "stale"}],
                error: "",
            },
        });
        await flushMicrotasks();
        assert.equal(harness.messages.filter(output =>
            output.type === "result" && output.id === message.id).length,
        resultCount, "late shard replies must be ignored");
    });

test("Pause terminates helpers even while one never becomes ready",
    async () => {
        const harness = await createWorkerHarness(
            {setList: () => ""},
            {hardwareConcurrency: 4},
        );
        const message = restrictedFindEvaluationMessage({id: 523});
        const evaluation = harness.send(message);
        await flushMicrotasks();
        assert.equal(harness.nestedWorkers.length, 3);
        harness.nestedWorkers[0].send({type: "find-helper-ready"});
        harness.nestedWorkers[1].send({type: "find-helper-ready"});

        await harness.send({type: "pause", id: message.id});
        await evaluation;
        assert.equal(harness.nestedWorkers.every(
            worker => worker.terminated), true);
        assert.equal(harness.messages.some(output =>
            output.type === "paused" && output.id === message.id), true);
        assert.equal(harness.messages.some(output =>
            output.type === "result" && output.id === message.id), false);
    });

test("restricted Find resumes with a fresh helper pool and full window",
    async () => {
        const harness = await createWorkerHarness(
            {setList: () => ""},
            {hardwareConcurrency: 2},
        );
        const message = restrictedFindEvaluationMessage({id: 524});
        const firstRun = harness.send(message);
        await flushMicrotasks();
        const firstHelper = harness.nestedWorkers[0];
        await harness.send({type: "pause", id: message.id});
        await firstRun;
        assert.equal(firstHelper.terminated, true);

        const resumed = harness.send({type: "resume", id: message.id});
        await flushMicrotasks();
        assert.equal(harness.nestedWorkers.length, 2);
        const resumedHelper = harness.nestedWorkers[1];
        assert.notStrictEqual(resumedHelper, firstHelper);
        await readyAndRestoreFindHelpers({
            ...harness,
            nestedWorkers: [resumedHelper],
        }, "");
        assert.deepEqual(harness.timers.delays(), [10000],
            "the resumed semantic search gets a fresh full window");
        await prepareFindHelpers({
            ...harness,
            nestedWorkers: [resumedHelper],
        }, {catalogSize: 1});
        const sizeOne = answerNestedRequest(
            resumedHelper,
            "find-helper-size",
            {
                success: true,
                timedOut: false,
                matches: [{index: 0, expression: "A"}],
                error: "",
            },
        );
        assert.equal(sizeOne.budget, 10000);
        await resumed;
        const result = harness.messages.find(output =>
            output.type === "result" && output.id === message.id);
        assert.equal(result.result.output, "?=A\n");
        assert.equal(resumedHelper.terminated, true);
    });

test("restricted Find falls back to the primary when nesting is unavailable",
    async () => {
        let serialCalls = 0;
        const module = {
            setList: () => "",
            beginLimitedEval(source, id, limit, checkAtLimit) {
                ++serialCalls;
                assert.equal(source, "find among A ?x = x");
                assert.equal(id, 525);
                assert.equal(limit, 1000);
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
        const harness = await createWorkerHarness(module, {
            nestedWorkersAvailable: false,
        });
        await harness.send(restrictedFindEvaluationMessage({
            id: 525,
            source: "find among A ?x = x",
        }));
        assert.equal(serialCalls, 1);
        assert.equal(harness.nestedWorkers.length, 0);
        assert.equal(harness.timers.scheduledDelays.includes(10000), false);
        const mode = harness.messages.find(output =>
            output.type === "find-pool-mode" && output.id === 525);
        assert.deepEqual({pooled: mode.pooled, workers: mode.workers}, {
            pooled: false,
            workers: 0,
        });
        const result = harness.messages.find(output =>
            output.type === "result" && output.id === 525);
        assert.equal(result.result.output, "?=A\n");
    });

test("a Find helper failure stops the pool and reports one clean error",
    async () => {
        const harness = await createWorkerHarness(
            {setList: () => ""},
            {hardwareConcurrency: 2},
        );
        const message = restrictedFindEvaluationMessage({id: 526});
        const evaluation = harness.send(message);
        await flushMicrotasks();
        await readyAndRestoreFindHelpers(harness, "");
        await prepareFindHelpers(harness, {catalogSize: 1});
        const pending = nextNestedRequest(
            harness.nestedWorkers[0], "find-helper-size");
        const errorEvent = harness.nestedWorkers[0].fail("shard exploded");
        assert.equal(errorEvent.defaultPrevented, true);
        await evaluation;

        const results = harness.messages.filter(output =>
            output.type === "result" && output.id === message.id);
        assert.equal(results.length, 1);
        assert.equal(results[0].result.success, false);
        assert.equal(results[0].result.error, "shard exploded");
        assert.equal(harness.nestedWorkers[0].terminated, true);
        harness.nestedWorkers[0].send({
            type: "find-helper-result",
            jobId: pending.jobId,
            result: {
                success: true,
                timedOut: false,
                matches: [{index: 0, expression: "stale"}],
                error: "",
            },
        });
        await flushMicrotasks();
        assert.equal(harness.messages.filter(output =>
            output.type === "result" && output.id === message.id).length, 1);
    });

test("ordinary commands never create Find helpers", async () => {
    const module = {
        setList: () => "",
        beginLimitedEval: () => ({
            success: true,
            definition: false,
            recoverWorker: false,
            output: "x\n",
            error: "",
            reductions: 1,
            limitReached: false,
        }),
    };
    const harness = await createWorkerHarness(module, {
        hardwareConcurrency: 100,
    });
    await harness.send({
        ...restrictedFindEvaluationMessage({id: 527, source: "Ix"}),
        findAmong: false,
    });
    assert.equal(harness.nestedWorkers.length, 0);
    assert.equal(harness.messages.some(output =>
        output.type === "find-pool-mode"), false);
    const result = harness.messages.find(output =>
        output.type === "result" && output.id === 527);
    assert.equal(result.result.output, "x\n");
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

test("built Emscripten restricted Find keeps size-three command output",
    async () => {
        const module = await loadBuiltCombdslModule();
        const definition = module.beginLimitedEval(
            "set WasmFindLeaf = 100 I", 240, 1000, false);
        assert.equal(definition.success, true, definition.error);
        assert.equal(definition.definition, true);
        assert.equal(definition.output, "");

        const command =
            "find among WasmFindLeaf ?x = " +
            "WasmFindLeaf WasmFindLeaf WasmFindLeaf x";
        const expected =
            "?=WasmFindLeaf WasmFindLeaf WasmFindLeaf\n";
        const active = module.beginLimitedEval(
            command, 241, 1000, false);
        assert.equal(active.success, true);
        assert.equal(active.output, expected);
        assert.equal(active.limitReached, false);

        const legacy = module.parseEval(command, 242, false, 0);
        assert.equal(legacy.success, true);
        assert.equal(legacy.output, expected);
        assert.equal(legacy.limitReached, false);
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
