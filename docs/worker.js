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
let steppingRequestId;
let steppingBasisStep = false;
let steppingColorize = false;
let steppingAutomatically = false;
let steppingReductions = 0;
let steppingProgressSequence = 0;
let steppingLastProgressAt = 0;
let automaticStepTimer;
const automaticStepIntervalMs = 16;

const errorMessage = error =>
    error instanceof Error ? error.message : String(error);

const resetStepping = () => {
    if (automaticStepTimer !== undefined) {
        clearTimeout(automaticStepTimer);
        automaticStepTimer = undefined;
    }
    steppingRequestId = undefined;
    steppingBasisStep = false;
    steppingColorize = false;
    steppingAutomatically = false;
    steppingReductions = 0;
    steppingProgressSequence = 0;
    steppingLastProgressAt = 0;
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
});

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
            if (result.success &&
                String(result.output) !== "") {
                self.postMessage({
                    type: "single-step-output",
                    id: requestId,
                    output: String(result.output),
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
                resetStepping();
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
        steppingRequestId === undefined) {
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
        steppingRequestId === undefined) {
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
        steppingRequestId === undefined) {
        busy = true;
        try {
            const module = await modulePromise;
            if (message.keyStep) {
                const result = module.beginSingleStep(
                    String(message.source));
                if (result.success && !result.complete) {
                    steppingRequestId = message.id;
                    steppingBasisStep = Boolean(message.basisStep);
                    steppingColorize = Boolean(message.colorize);
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
                        String(message.source));
                    if (result.success && !result.complete) {
                        steppingRequestId = message.id;
                        steppingBasisStep =
                            Boolean(message.basisStep);
                        steppingColorize =
                            Boolean(message.colorize);
                        steppingAutomatically = true;
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

                const result = module.parseEval(
                    String(message.source), message.id);
                const response = {
                    type: "result",
                    id: message.id,
                    html: false,
                    result,
                };
                if (result.success && result.definition) {
                    response.setList = module.setList();
                }
                self.postMessage(response);
            }
        } catch (error) {
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
        message.id === steppingRequestId) {
        busy = true;
        try {
            const module = await modulePromise;
            const result = module.takeSingleStep(
                steppingBasisStep, steppingColorize, true);
            if (!result.success || !result.reduced || result.complete) {
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
