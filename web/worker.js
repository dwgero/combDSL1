/*
 * Combinator Studio
 * Part of C++ Combinator DSL
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

"use strict";

const workerUrl = new URL(self.location.href);
const baseUrl = new URL(".", workerUrl);
const assetVersion = workerUrl.searchParams.get("v");
const workerRole = workerUrl.searchParams.get("role") ?? "primary";
const findHelperRole = workerRole === "find-helper";
const assetUrl = file => {
    const url = new URL(file, baseUrl);
    if (assetVersion !== null) {
        url.searchParams.set("v", assetVersion);
    }
    return url.href;
};
let modulePromise;
let busy = false;
let ordinaryRequestId;
let ordinaryStepLimit;
let ordinaryWindowReductions = 0;
let ordinaryTotalReductions = 0;
let ordinaryPausedAtLimit = false;
let ordinaryPausedManually = false;
let ordinaryContinuationTimer;
let steppingRequestId;
let steppingBasisStep = false;
let steppingColorize = false;
let steppingAutomatically = false;
let steppingKeyStep = false;
let steppingPausedAtLimit = false;
let steppingPausedManually = false;
let steppingDeferredOutput = "";
let steppingDeferredHtml = false;
let steppingReductions = 0;
let steppingProgressSequence = 0;
let steppingLastProgressAt = 0;
let automaticStepTimer;
const automaticStepIntervalMs = 16;
const ordinaryEvaluationSliceReductions = 1000;
const findAmongWindowMs = 10_000;
const findAmongHelperLimit = 8;
const findAmongHelperStartupTimeoutMs = 10_000;
let activeFindAmong;
let pausedFindAmongMessage;

const errorMessage = error =>
    error instanceof Error ? error.message : String(error);

class FindAmongCancelled extends Error {}
class FindAmongTimedOut extends Error {}

const takeFindAmongMatches = result => {
    const matches = result.matches;
    const copied = [];
    try {
        for (let index = 0; index < matches.size(); ++index) {
            const match = matches.get(index);
            copied.push({
                index: Number(match.index),
                expression: String(match.expression),
            });
        }
    } finally {
        matches.delete();
    }
    return copied;
};

const maximumFindAmongWork = (leafCount, catalogSize, cap) => {
    if (leafCount < 1 || catalogSize < 1 || cap < 1) {
        return 0;
    }
    const multiply = (left, right) =>
        left >= cap || right >= cap || left > cap / right
            ? cap
            : left * right;
    let labels = 1;
    if (catalogSize > 1) {
        for (let leaf = 0; leaf < leafCount && labels < cap; ++leaf) {
            labels = multiply(labels, catalogSize);
        }
    }
    let shapes = 1;
    for (let index = 1; index < leafCount && shapes < cap; ++index) {
        shapes = Math.min(
            cap,
            shapes * (4 * index - 2) / (index + 1),
        );
    }
    return multiply(shapes, labels);
};

const reportedFindAmongHelperCount = () => {
    const reported = Number(self.navigator?.hardwareConcurrency);
    const hardwareLimit = Number.isSafeInteger(reported) && reported > 1
        ? reported - 1
        : 1;
    return Math.min(findAmongHelperLimit, hardwareLimit);
};

const makeFindHelperUrl = () => {
    const url = new URL(workerUrl.href);
    url.searchParams.set("role", "find-helper");
    return url;
};

const createFindHelper = index => {
    if (typeof Worker !== "function") {
        throw new Error("nested Web Workers are unavailable");
    }
    const child = new Worker(makeFindHelperUrl());
    let readyResolve;
    let readyReject;
    let stopped = false;
    let readySettled = false;
    let nextJobId = 0;
    const pending = new Map();
    const ready = new Promise((resolve, reject) => {
        readyResolve = resolve;
        readyReject = reject;
    });
    const rejectPending = error => {
        if (!readySettled) {
            readySettled = true;
            clearTimeout(readyTimer);
        }
        readyReject(error);
        for (const {reject} of pending.values()) {
            reject(error);
        }
        pending.clear();
    };
    child.addEventListener("message", event => {
        const message = event.data;
        if (message.type === "find-helper-ready") {
            if (readySettled) {
                return;
            }
            readySettled = true;
            clearTimeout(readyTimer);
            readyResolve();
            return;
        }
        if (message.type === "find-helper-fatal") {
            rejectPending(new Error(String(message.error)));
            return;
        }
        const job = pending.get(message.jobId);
        if (job === undefined) {
            return;
        }
        pending.delete(message.jobId);
        job.resolve(message.result);
    });
    child.addEventListener("error", event => {
        event.preventDefault?.();
        const error = new Error(
            event.message || `Find helper ${index + 1} failed`);
        rejectPending(error);
    });
    const readyTimer = setTimeout(() => {
        if (readySettled) {
            return;
        }
        const error = new Error(
            `Find helper ${index + 1} did not start`);
        rejectPending(error);
        stopped = true;
        child.terminate();
    }, findAmongHelperStartupTimeoutMs);
    return {
        index,
        ready,
        request(type, fields = {}) {
            if (stopped) {
                return Promise.reject(new FindAmongCancelled());
            }
            const jobId = ++nextJobId;
            return new Promise((resolve, reject) => {
                pending.set(jobId, {resolve, reject});
                child.postMessage({type, jobId, ...fields});
            });
        },
        terminate(error = new FindAmongCancelled()) {
            if (stopped) {
                return;
            }
            stopped = true;
            rejectPending(error);
            child.terminate();
        },
    };
};

const resetOrdinaryEvaluation = () => {
    if (ordinaryContinuationTimer !== undefined) {
        clearTimeout(ordinaryContinuationTimer);
        ordinaryContinuationTimer = undefined;
    }
    ordinaryRequestId = undefined;
    ordinaryStepLimit = undefined;
    ordinaryWindowReductions = 0;
    ordinaryTotalReductions = 0;
    ordinaryPausedAtLimit = false;
    ordinaryPausedManually = false;
};

const resetStepping = () => {
    if (automaticStepTimer !== undefined) {
        clearTimeout(automaticStepTimer);
        automaticStepTimer = undefined;
    }
    steppingRequestId = undefined;
    steppingBasisStep = false;
    steppingColorize = false;
    steppingAutomatically = false;
    steppingKeyStep = false;
    steppingPausedAtLimit = false;
    steppingPausedManually = false;
    steppingDeferredOutput = "";
    steppingDeferredHtml = false;
    steppingReductions = 0;
    steppingProgressSequence = 0;
    steppingLastProgressAt = 0;
};

const postDeferredStepOutput = requestId => {
    if (steppingDeferredOutput === "") {
        return;
    }

    self.postMessage({
        type: "single-step-output",
        id: requestId,
        output: steppingDeferredOutput,
        html: steppingDeferredHtml,
    });
    steppingDeferredOutput = "";
    steppingDeferredHtml = false;
};

const evaluationResult = (
    result,
    reductions = 0,
    output = result.output,
) => ({
    success: Boolean(result.success),
    definition: Boolean(result.definition),
    recoverWorker: false,
    output: String(output ?? ""),
    error: String(result.error ?? ""),
    reductions,
    limitReached: Boolean(result.limitReached),
});

const nextOrdinarySlice = () => {
    const remaining = ordinaryStepLimit === undefined
        ? ordinaryEvaluationSliceReductions
        : ordinaryStepLimit - ordinaryWindowReductions;
    const limit = Math.min(
        ordinaryEvaluationSliceReductions, remaining);
    return {
        limit,
        checkAtLimit: ordinaryStepLimit !== undefined &&
            limit === remaining,
    };
};

const postOrdinaryResult = (module, requestId, result) => {
    const response = {
        type: "result",
        id: requestId,
        html: false,
        result,
    };
    if (result.success && result.definition) {
        response.setList = module.setList();
    }
    self.postMessage(response);
};

const scheduleOrdinaryContinuation = (module, requestId) => {
    ordinaryContinuationTimer = setTimeout(() => {
        ordinaryContinuationTimer = undefined;
        if (ordinaryRequestId !== requestId ||
            ordinaryPausedAtLimit || ordinaryPausedManually) {
            return;
        }

        busy = true;
        try {
            const slice = nextOrdinarySlice();
            const result = module.resumeLimitedEval(
                requestId, slice.limit, slice.checkAtLimit);
            handleOrdinarySliceResult(module, requestId, result);
        } catch (error) {
            resetOrdinaryEvaluation();
            self.postMessage({
                type: "fatal",
                id: requestId,
                error: errorMessage(error),
            });
        } finally {
            busy = false;
        }
    }, 0);
};

const handleOrdinarySliceResult = (module, requestId, result) => {
    const totalReductions = Number(result.reductions ?? 0);
    const reductionsThisSlice = Math.max(
        0, totalReductions - ordinaryTotalReductions);
    ordinaryTotalReductions = totalReductions;
    ordinaryWindowReductions += reductionsThisSlice;

    if (!result.success || result.definition || !result.limitReached) {
        resetOrdinaryEvaluation();
        postOrdinaryResult(module, requestId, result);
        return;
    }

    if (ordinaryStepLimit !== undefined &&
        ordinaryWindowReductions >= ordinaryStepLimit) {
        ordinaryPausedAtLimit = true;
        postOrdinaryResult(module, requestId, result);
        return;
    }

    scheduleOrdinaryContinuation(module, requestId);
};

const scheduleAutomaticStep = (module, requestId) => {
    automaticStepTimer = setTimeout(() => {
        automaticStepTimer = undefined;
        if (!steppingAutomatically ||
            steppingRequestId !== requestId) {
            return;
        }

        busy = true;
        try {
            const result = module.takeSingleStep(
                steppingBasisStep, steppingColorize, false);
            const resultOutput = String(result.output ?? "");
            if (result.success && result.limitReached) {
                steppingDeferredOutput = resultOutput;
                steppingDeferredHtml = steppingColorize;
            } else if (result.success && resultOutput !== "") {
                self.postMessage({
                    type: "single-step-output",
                    id: requestId,
                    output: resultOutput,
                    html: steppingColorize,
                });
            }

            if (result.success && result.reduced) {
                ++steppingReductions;
                const now = performance.now();
                if (now - steppingLastProgressAt >= 100) {
                    steppingLastProgressAt = now;
                    self.postMessage({
                        type: "eval-progress",
                        id: requestId,
                        sequence: ++steppingProgressSequence,
                        reductions: steppingReductions,
                    });
                }
            }

            if (!result.success || !result.reduced ||
                result.complete) {
                const reductions = steppingReductions;
                const response = {
                    type: "result",
                    id: requestId,
                    html: false,
                    result: evaluationResult(
                        result, reductions, ""),
                };
                if (result.success && result.limitReached) {
                    steppingAutomatically = false;
                    steppingPausedAtLimit = true;
                } else {
                    resetStepping();
                }
                self.postMessage(response);
                return;
            }

            scheduleAutomaticStep(module, requestId);
        } catch (error) {
            resetStepping();
            self.postMessage({
                type: "fatal",
                id: requestId,
                error: errorMessage(error),
            });
        } finally {
            busy = false;
        }
    }, automaticStepIntervalMs);
};

const findAmongResult = (output = "", error = "") => ({
    success: error === "",
    definition: false,
    recoverWorker: false,
    output,
    error,
    reductions: 0,
    limitReached: false,
});

const completedFindAmongResult = expressions => findAmongResult(
    expressions.length === 0
        ? "No match within search bounds\n"
        : expressions.map(expression => `?=${expression}\n`).join(""),
);

const terminateFindHelpers = (
    state,
    error = new FindAmongCancelled(),
) => {
    for (const helper of state.helpers) {
        helper.terminate(error);
    }
    state.helpers = [];
};

const remainingFindAmongBudget = state =>
    Math.max(0, state.deadline - performance.now());

const startFindAmongHelpers = async state => {
    const attempted = [];
    for (let index = 0;
        index < reportedFindAmongHelperCount(); ++index) {
        try {
            const helper = createFindHelper(index);
            attempted.push(helper);
            state.helpers.push(helper);
        } catch {
            break;
        }
    }
    if (attempted.length !== 0) {
        self.postMessage({
            type: "find-pool-mode",
            id: state.message.id,
            pooled: true,
            workers: attempted.length,
        });
    }
    const readiness = await Promise.allSettled(
        attempted.map(helper => helper.ready));
    const readyHelpers = [];
    for (let index = 0; index < attempted.length; ++index) {
        const helper = attempted[index];
        if (readiness[index].status === "fulfilled") {
            readyHelpers.push(helper);
        } else {
            helper.terminate(readiness[index].reason);
        }
    }
    state.helpers = readyHelpers;
};

const runFindAmongPool = async (module, message) => {
    const state = {
        message,
        helpers: [],
        cancelled: false,
        timedOut: false,
        deadline: 0,
        deadlineTimer: undefined,
        renderedMatches: [],
    };
    activeFindAmong = state;
    try {
        await startFindAmongHelpers(state);
        if (state.cancelled) {
            throw new FindAmongCancelled();
        }
        if (state.helpers.length === 0) {
            self.postMessage({
                type: "find-pool-mode",
                id: message.id,
                pooled: false,
                workers: 0,
            });
            return undefined;
        }

        self.postMessage({
            type: "find-pool-mode",
            id: message.id,
            pooled: true,
            workers: state.helpers.length,
        });
        const setList = module.setList();
        const restorations = await Promise.all(
            state.helpers.map(helper => helper.request(
                "find-helper-load",
                {source: setList},
            )));
        const failedRestore = restorations.find(
            result => !result.success);
        if (failedRestore !== undefined) {
            throw new Error(
                failedRestore.error ||
                "could not restore definitions in a Find helper");
        }
        if (state.cancelled) {
            throw new FindAmongCancelled();
        }

        state.deadline = performance.now() + findAmongWindowMs;
        state.deadlineTimer = setTimeout(() => {
            state.timedOut = true;
            terminateFindHelpers(
                state, new FindAmongTimedOut());
        }, findAmongWindowMs);

        const preparations = await Promise.all(
            state.helpers.map(helper => helper.request(
                "find-helper-prepare",
                {
                    source: String(message.source),
                    budget: remainingFindAmongBudget(state),
                },
            )));
        const failedPreparation = preparations.find(
            result => !result.success);
        if (failedPreparation !== undefined) {
            throw new Error(
                failedPreparation.error ||
                "could not prepare a restricted Find search");
        }
        const firstPreparation = preparations[0];
        if (preparations.some(result =>
            Boolean(result.allSizes) !==
                Boolean(firstPreparation.allSizes) ||
            Number(result.catalogSize) !==
                Number(firstPreparation.catalogSize))) {
            throw new Error(
                "Find helpers restored inconsistent definitions");
        }

        const printedMatches = new Set();
        if (preparations.some(result => result.timedOut) ||
            state.timedOut ||
            remainingFindAmongBudget(state) <= 0) {
            return completedFindAmongResult(state.renderedMatches);
        }
        if (preparations.some(result => !result.searchable)) {
            return completedFindAmongResult(state.renderedMatches);
        }

        const catalogSize = Number(firstPreparation.catalogSize);
        const allSizes = Boolean(firstPreparation.allSizes);
        for (let leafCount = 1;; ++leafCount) {
            if (state.cancelled) {
                throw new FindAmongCancelled();
            }
            const budget = remainingFindAmongBudget(state);
            if (state.timedOut || budget <= 0) {
                break;
            }
            const shardCount = leafCount < 3
                ? 1
                : Math.min(
                    state.helpers.length,
                    maximumFindAmongWork(
                        leafCount,
                        catalogSize,
                        state.helpers.length),
                );
            const shardResults = await Promise.all(
                state.helpers.slice(0, shardCount).map(
                    (helper, shardIndex) => helper.request(
                        "find-helper-size",
                        {
                            leafCount,
                            shardIndex,
                            shardCount,
                            budget,
                        },
                    ),
                ));
            const failedShard = shardResults.find(
                result => !result.success);
            if (failedShard !== undefined) {
                throw new Error(
                    failedShard.error ||
                    "a restricted Find shard failed");
            }
            if (shardResults.some(result => result.timedOut) ||
                state.timedOut ||
                remainingFindAmongBudget(state) <= 0) {
                break;
            }

            const sizeMatches = shardResults
                .flatMap(result => result.matches)
                .sort((left, right) => left.index - right.index);
            for (const match of sizeMatches) {
                if (printedMatches.has(match.expression)) {
                    continue;
                }
                printedMatches.add(match.expression);
                state.renderedMatches.push(match.expression);
            }
            if (!allSizes && sizeMatches.length !== 0) {
                break;
            }
        }

        return completedFindAmongResult(state.renderedMatches);
    } catch (error) {
        if (error instanceof FindAmongCancelled || state.cancelled) {
            return null;
        }
        if (error instanceof FindAmongTimedOut || state.timedOut) {
            return completedFindAmongResult(state.renderedMatches);
        }
        return findAmongResult("", errorMessage(error));
    } finally {
        if (state.deadlineTimer !== undefined) {
            clearTimeout(state.deadlineTimer);
        }
        terminateFindHelpers(state);
        if (activeFindAmong === state) {
            activeFindAmong = undefined;
        }
    }
};

const pauseFindAmong = requestId => {
    const state = activeFindAmong;
    if (state === undefined || state.message.id !== requestId ||
        state.cancelled) {
        return false;
    }
    state.cancelled = true;
    if (state.deadlineTimer !== undefined) {
        clearTimeout(state.deadlineTimer);
        state.deadlineTimer = undefined;
    }
    terminateFindHelpers(state);
    pausedFindAmongMessage = state.message;
    self.postMessage({
        type: "paused",
        id: requestId,
        reductions: 0,
    });
    return true;
};

const postFindAmongResult = (requestId, result) => {
    if (result === null) {
        return;
    }
    self.postMessage({
        type: "result",
        id: requestId,
        html: false,
        result,
    });
};

const handleFindHelperMessage = async message => {
    if (!findHelperRole || busy || modulePromise === undefined) {
        return;
    }
    busy = true;
    let result;
    try {
        const module = await modulePromise;
        if (message.type === "find-helper-load") {
            result = String(message.source) === ""
                ? {success: true, error: ""}
                : module.loadSetList(
                    String(message.source),
                    "Find helper definitions",
                );
        } else if (message.type === "find-helper-prepare") {
            result = module.prepareFindAmong(
                String(message.source), Number(message.budget));
        } else if (message.type === "find-helper-size") {
            const shard = module.findAmongPreparedSizeShard(
                Number(message.leafCount),
                Number(message.shardIndex),
                Number(message.shardCount),
                Number(message.budget),
            );
            result = {
                success: Boolean(shard.success),
                timedOut: Boolean(shard.timedOut),
                matches: takeFindAmongMatches(shard),
                error: String(shard.error ?? ""),
            };
        } else if (message.type === "find-helper-reset") {
            module.resetFindAmong();
            result = {success: true, error: ""};
        } else {
            return;
        }
    } catch (error) {
        result = {success: false, error: errorMessage(error)};
    } finally {
        busy = false;
    }
    self.postMessage({
        type: "find-helper-result",
        jobId: message.jobId,
        result,
    });
};

const evaluateFindAmong = async (module, message) => {
    const pooledResult = await runFindAmongPool(module, message);
    if (pooledResult !== undefined) {
        postFindAmongResult(message.id, pooledResult);
        return;
    }

    const serialResult = module.beginLimitedEval(
        String(message.source), message.id,
        ordinaryEvaluationSliceReductions, false);
    postOrdinaryResult(module, message.id, serialResult);
};

try {
    importScripts(assetUrl("combdsl.js"));
    modulePromise = createCombdslModule({
        locateFile: assetUrl,
    });
    modulePromise.then(
        module => self.postMessage(findHelperRole
            ? {type: "find-helper-ready"}
            : {
                type: "ready",
                setList: module.setList(),
            }),
        error => self.postMessage({
            type: findHelperRole ? "find-helper-fatal" : "fatal",
            error: errorMessage(error),
        }),
    );
} catch (error) {
    self.postMessage({
        type: findHelperRole ? "find-helper-fatal" : "fatal",
        error: errorMessage(error),
    });
}

self.addEventListener("message", async event => {
    const message = event.data;
    if (findHelperRole) {
        await handleFindHelperMessage(message);
        return;
    }

    if (message.type === "pause" &&
        pauseFindAmong(message.id)) {
        return;
    }

    if (message.type === "resume" &&
        pausedFindAmongMessage?.id === message.id) {
        const resumedMessage = pausedFindAmongMessage;
        pausedFindAmongMessage = undefined;
        const resumeWhenIdle = async () => {
            if (busy) {
                setTimeout(resumeWhenIdle, 0);
                return;
            }
            busy = true;
            try {
                const module = await modulePromise;
                self.postMessage({
                    type: "eval-started",
                    id: message.id,
                });
                await evaluateFindAmong(module, resumedMessage);
            } catch (error) {
                self.postMessage({
                    type: "fatal",
                    id: message.id,
                    error: errorMessage(error),
                });
            } finally {
                busy = false;
            }
        };
        void resumeWhenIdle();
        return;
    }

    if (busy || modulePromise === undefined) {
        return;
    }

    if (message.type === "inspect-definition" &&
        steppingRequestId === undefined &&
        ordinaryRequestId === undefined) {
        busy = true;
        try {
            const module = await modulePromise;
            self.postMessage({
                type: "definition-inspection-result",
                id: message.id,
                result: module.inspectDefinition(
                    String(message.source)),
            });
        } catch (error) {
            self.postMessage({
                type: "definition-inspection-result",
                id: message.id,
                result: {
                    success: false,
                    definition: false,
                    displayOnly: false,
                    showAll: false,
                    find: false,
                    replacement: "",
                    error: errorMessage(error),
                },
            });
        } finally {
            busy = false;
        }
        return;
    }

    if (message.type === "load" &&
        steppingRequestId === undefined &&
        ordinaryRequestId === undefined) {
        busy = true;
        try {
            const module = await modulePromise;
            const result = module.loadSetList(
                String(message.source), String(message.name));
            self.postMessage({
                type: "load-result",
                id: message.id,
                result,
                setList: module.setList(),
            });
        } catch (error) {
            self.postMessage({
                type: "load-result",
                id: message.id,
                result: {
                    success: false,
                    loaded: 0,
                    line: 0,
                    error: `${String(message.name)}: ${
                        errorMessage(error)}\n` +
                        "Errors are preventing any changes from being made",
                },
            });
        } finally {
            busy = false;
        }
        return;
    }

    if (message.type === "evaluate" &&
        steppingRequestId === undefined &&
        ordinaryRequestId === undefined) {
        busy = true;
        try {
            const module = await modulePromise;
            if (message.findAmong) {
                self.postMessage({
                    type: "eval-started",
                    id: message.id,
                });
                await evaluateFindAmong(module, message);
            } else if (message.keyStep) {
                const result = module.beginSingleStep(
                    String(message.source),
                    Boolean(message.basisStep),
                    false,
                    0);
                if (result.success && !result.complete) {
                    steppingRequestId = message.id;
                    steppingBasisStep = Boolean(message.basisStep);
                    steppingColorize = Boolean(message.colorize);
                    steppingKeyStep = true;
                    steppingPausedAtLimit = false;
                    steppingPausedManually = false;
                }
                const response = {
                    type: result.success && result.complete
                        ? "result"
                        : "step-ready",
                    id: message.id,
                    html: false,
                    result,
                };
                if (result.success && result.definition) {
                    response.setList = module.setList();
                }
                self.postMessage(response);
            } else {
                self.postMessage({
                    type: "eval-started",
                    id: message.id,
                });
                if (message.singleStep) {
                    const result = module.beginSingleStep(
                        String(message.source),
                        Boolean(message.basisStep),
                        Boolean(message.stepLimitEnabled),
                        Number(message.stepLimit));
                    if (result.success && !result.complete) {
                        steppingRequestId = message.id;
                        steppingBasisStep =
                            Boolean(message.basisStep);
                        steppingColorize =
                            Boolean(message.colorize);
                        steppingAutomatically = true;
                        steppingKeyStep = false;
                        steppingPausedAtLimit = false;
                        steppingPausedManually = false;
                        steppingReductions = 0;
                        steppingProgressSequence = 0;
                        steppingLastProgressAt =
                            performance.now();
                        scheduleAutomaticStep(
                            module, message.id);
                        return;
                    }

                    const response = {
                        type: "result",
                        id: message.id,
                        html: false,
                        result: evaluationResult(result),
                    };
                    if (result.success && result.definition) {
                        response.setList = module.setList();
                    }
                    self.postMessage(response);
                    return;
                }

                ordinaryRequestId = message.id;
                ordinaryStepLimit = message.stepLimitEnabled
                    ? Number(message.stepLimit)
                    : undefined;
                ordinaryWindowReductions = 0;
                ordinaryTotalReductions = 0;
                ordinaryPausedAtLimit = false;
                ordinaryPausedManually = false;
                const slice = nextOrdinarySlice();
                const result = module.beginLimitedEval(
                    String(message.source),
                    message.id,
                    slice.limit,
                    slice.checkAtLimit);
                handleOrdinarySliceResult(
                    module, message.id, result);
            }
        } catch (error) {
            if (ordinaryRequestId === message.id) {
                resetOrdinaryEvaluation();
            }
            if (steppingRequestId === message.id) {
                resetStepping();
            }
            self.postMessage({
                type: "fatal",
                id: message.id,
                error: errorMessage(error),
            });
        } finally {
            busy = false;
        }
        return;
    }

    if (message.type === "pause") {
        if (message.id === ordinaryRequestId &&
            !ordinaryPausedAtLimit &&
            !ordinaryPausedManually) {
            if (ordinaryContinuationTimer !== undefined) {
                clearTimeout(ordinaryContinuationTimer);
                ordinaryContinuationTimer = undefined;
            }
            ordinaryPausedManually = true;
            self.postMessage({
                type: "paused",
                id: message.id,
                reductions: ordinaryTotalReductions,
            });
            return;
        }

        if (message.id === steppingRequestId &&
            !steppingPausedAtLimit &&
            !steppingPausedManually) {
            if (automaticStepTimer !== undefined) {
                clearTimeout(automaticStepTimer);
                automaticStepTimer = undefined;
            }
            steppingAutomatically = false;
            steppingPausedManually = true;
            self.postMessage({
                type: "paused",
                id: message.id,
                reductions: steppingReductions,
            });
        }
        return;
    }

    if (message.type === "resume") {
        if (message.id === ordinaryRequestId) {
            if (ordinaryPausedManually) {
                ordinaryPausedManually = false;
                self.postMessage({
                    type: "eval-started",
                    id: message.id,
                });
                const module = await modulePromise;
                scheduleOrdinaryContinuation(module, message.id);
                return;
            }
            if (ordinaryPausedAtLimit) {
                ordinaryPausedAtLimit = false;
                ordinaryWindowReductions = 0;
                self.postMessage({
                    type: "eval-started",
                    id: message.id,
                });
                const module = await modulePromise;
                scheduleOrdinaryContinuation(module, message.id);
                return;
            }
        }

        if (message.id !== steppingRequestId) {
            return;
        }

        if (steppingPausedManually) {
            const module = await modulePromise;
            steppingPausedManually = false;
            if (steppingKeyStep) {
                self.postMessage({
                    type: "step-ready",
                    id: message.id,
                    result: {success: true},
                });
            } else {
                steppingAutomatically = true;
                steppingLastProgressAt = performance.now();
                self.postMessage({
                    type: "eval-started",
                    id: message.id,
                });
                scheduleAutomaticStep(module, message.id);
            }
            return;
        }

        if (!steppingPausedAtLimit) {
            return;
        }

        busy = true;
        try {
            const module = await modulePromise;
            if (!module.resumeSingleStep()) {
                throw new Error(
                    "no matching evaluation is ready to resume");
            }
            steppingPausedAtLimit = false;
            postDeferredStepOutput(message.id);
            steppingAutomatically = true;
            steppingLastProgressAt = performance.now();
            self.postMessage({
                type: "eval-started",
                id: message.id,
            });
            scheduleAutomaticStep(module, message.id);
        } catch (error) {
            resetStepping();
            self.postMessage({
                type: "fatal",
                id: message.id,
                error: errorMessage(error),
            });
        } finally {
            busy = false;
        }
        return;
    }

    if (message.type === "step" &&
        message.id === steppingRequestId &&
        !steppingPausedManually) {
        busy = true;
        try {
            const module = await modulePromise;
            const result = module.takeSingleStep(
                steppingBasisStep, steppingColorize, true);
            if (result.success && result.reduced) {
                ++steppingReductions;
            }
            if (!result.success || !result.reduced ||
                result.complete) {
                resetStepping();
            }
            self.postMessage({
                type: "step-result",
                id: message.id,
                result,
            });
        } catch (error) {
            resetStepping();
            self.postMessage({
                type: "fatal",
                id: message.id,
                error: errorMessage(error),
            });
        } finally {
            busy = false;
        }
    }
});
