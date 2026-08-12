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

const errorMessage = error =>
    error instanceof Error ? error.message : String(error);

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

try {
    importScripts(assetUrl("combdsl.js"));
    modulePromise = createCombdslModule({
        locateFile: assetUrl,
    });
    modulePromise.then(
        module => self.postMessage({
            type: "ready",
            setList: module.setList(),
        }),
        error => self.postMessage({
            type: "fatal",
            error: errorMessage(error),
        }),
    );
} catch (error) {
    self.postMessage({
        type: "fatal",
        error: errorMessage(error),
    });
}

self.addEventListener("message", async event => {
    const message = event.data;
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
            if (message.keyStep) {
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
