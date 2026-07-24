/*
 * C++ combinator DSL
 * Copyright (C) 2026  David W. Gero
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

"use strict";

(() => {
    const form = document.querySelector("#evaluation-form");
    const source = document.querySelector("#source");
    const singleStep = document.querySelector("#single-step");
    const basisStep = document.querySelector("#basis-step");
    const keyStep = document.querySelector("#key-step");
    const colorize = document.querySelector("#colorize");
    const cancel = document.querySelector("#cancel");
    const help = document.querySelector("#help");
    const helpDialog = document.querySelector("#help-dialog");
    const combinatorInfo = document.querySelector("#combinator-info");
    const combinatorInfoDialog = document.querySelector(
        "#combinator-info-dialog");
    const about = document.querySelector("#about");
    const aboutDialog = document.querySelector("#about-dialog");
    const status = document.querySelector("#status");
    const output = document.querySelector("#output");

    let worker;
    let terminateWorker = () => {};
    let generation = 0;
    let nextRequestId = 0;
    let activeRequest;
    let ready = false;
    let singleStepEnabled = false;
    let basisStepEnabled = false;
    let keyStepEnabled = false;
    let colorizeEnabled = false;

    const errorMessage = error =>
        error instanceof Error ? error.message : String(error);

    const updateControls = () => {
        const evaluating = activeRequest !== undefined;
        singleStep.disabled = !ready || evaluating;
        basisStep.disabled = !ready || evaluating;
        keyStep.disabled = !ready || evaluating;
        colorize.disabled = !ready || evaluating;
        cancel.disabled = !evaluating;
        source.readOnly = evaluating;
    };

    const updateModeButtons = () => {
        singleStep.setAttribute(
            "aria-pressed", String(singleStepEnabled));
        singleStep.textContent = `Single Step: ${
            singleStepEnabled ? "On" : "Off"}`;
        basisStep.setAttribute(
            "aria-pressed", String(basisStepEnabled));
        basisStep.textContent = `Basis Step: ${
            basisStepEnabled ? "On" : "Off"}`;
        keyStep.setAttribute(
            "aria-pressed", String(keyStepEnabled));
        keyStep.textContent = `Key Step: ${
            keyStepEnabled ? "On" : "Off"}`;
        colorize.setAttribute(
            "aria-pressed", String(colorizeEnabled));
        colorize.textContent = `Colorize: ${
            colorizeEnabled ? "On" : "Off"}`;
    };

    const outputText = text => text.endsWith("\n")
        ? text.slice(0, -1)
        : text;

    const scrollToNewestOutput = () => {
        output.scrollTop = output.scrollHeight;
    };

    const afterNextPaint = callback => {
        requestAnimationFrame(() => requestAnimationFrame(callback));
    };

    const createOutputEntry = () => {
        if (output.childNodes.length !== 0) {
            output.append("\n\n");
        }

        const entry = document.createElement("span");
        output.append(entry);
        return entry;
    };

    const appendOutput = (text, kind = "output") => {
        const entry = createOutputEntry();
        const content = document.createElement("span");
        content.textContent = outputText(text);
        content.dataset.kind = kind;
        entry.append(content);
        scrollToNewestOutput();
    };

    const beginEvaluationOutput = startingExpression => {
        const entry = createOutputEntry();
        entry.textContent = startingExpression;
        scrollToNewestOutput();
        return entry;
    };

    const completeEvaluationOutput = (
        request,
        text,
        kind = "output",
        html = false,
    ) => {
        const content = outputText(text);
        if (content === "") {
            return;
        }

        const result = document.createElement("span");
        if (html) {
            // color_step escapes expression text and emits only its fixed
            // color markup.
            result.innerHTML = content;
        } else {
            result.textContent = content;
        }
        result.dataset.kind = kind;
        request.outputEntry.append("\n", result);
        scrollToNewestOutput();
    };

    const clearCompletedSource = request => {
        if (source.value === request.source) {
            source.value = "";
        }
    };

    const showStartupError = message => {
        ready = false;
        activeRequest = undefined;
        status.textContent = "WebAssembly unavailable";
        appendOutput(message, "error");
        updateControls();
    };

    const startWorker = () => {
        const currentGeneration = ++generation;
        let terminationExpected = false;
        ready = false;
        activeRequest = undefined;
        status.textContent = "Loading WebAssembly…";
        updateControls();

        try {
            const workerUrl = new URL("./worker.js", document.baseURI);
            workerUrl.searchParams.set("v", Date.now().toString());
            worker = new Worker(workerUrl);
            const currentWorker = worker;
            terminateWorker = () => {
                terminationExpected = true;
                currentWorker.terminate();
            };
        } catch (error) {
            showStartupError(errorMessage(error));
            return;
        }

        worker.addEventListener("message", event => {
            if (currentGeneration !== generation || terminationExpected) {
                return;
            }

            const message = event.data;
            if (message.type === "ready") {
                ready = true;
                status.textContent = "Ready";
                updateControls();
                return;
            }

            if (message.type === "step-ready" &&
                message.id === activeRequest?.id &&
                activeRequest.keyStep) {
                const request = activeRequest;
                if (message.result.success) {
                    request.stepReady = true;
                    status.textContent = "Press a key for the next step";
                } else {
                    activeRequest = undefined;
                    status.textContent = "Ready";
                    completeEvaluationOutput(
                        request, message.result.error, "error");
                }
                updateControls();
                return;
            }

            if (message.type === "step-result" &&
                message.id === activeRequest?.id &&
                activeRequest.keyStep) {
                const request = activeRequest;
                request.stepPending = false;

                if (!message.result.success) {
                    activeRequest = undefined;
                    status.textContent = "Ready";
                    completeEvaluationOutput(
                        request, message.result.error, "error");
                } else if (message.result.reduced) {
                    completeEvaluationOutput(
                        request,
                        message.result.output,
                        "output",
                        request.colorize);
                    if (message.result.complete) {
                        clearCompletedSource(request);
                        activeRequest = undefined;
                        status.textContent = "Normal form reached";
                    } else {
                        request.stepReady = true;
                        status.textContent =
                            "Press a key for the next step";
                    }
                } else {
                    clearCompletedSource(request);
                    activeRequest = undefined;
                    status.textContent = "Normal form reached";
                }
                updateControls();
                return;
            }

            if (message.type === "result" &&
                message.id === activeRequest?.id) {
                const completedRequest = activeRequest;
                activeRequest = undefined;
                status.textContent = "Ready";
                if (message.result.success) {
                    if (!message.result.definition) {
                        completeEvaluationOutput(
                            completedRequest,
                            message.result.output,
                            "output",
                            completedRequest.singleStep &&
                                completedRequest.colorize);
                    }
                    clearCompletedSource(completedRequest);
                } else {
                    completeEvaluationOutput(
                        completedRequest, message.result.error, "error");
                }
                updateControls();
                return;
            }

            if (message.type === "fatal") {
                const failedRequest = activeRequest;
                activeRequest = undefined;
                ready = false;
                status.textContent = "WebAssembly stopped";
                if (failedRequest === undefined) {
                    appendOutput(message.error, "error");
                } else {
                    completeEvaluationOutput(
                        failedRequest, message.error, "error");
                }
                updateControls();
            }
        });

        worker.addEventListener("error", event => {
            if (terminationExpected) {
                event.preventDefault();
                return;
            }
            if (currentGeneration !== generation) {
                return;
            }
            event.preventDefault();
            const failedRequest = activeRequest;
            activeRequest = undefined;
            ready = false;
            status.textContent = "WebAssembly stopped";
            const message = event.message || "Web Worker failed";
            if (failedRequest === undefined) {
                appendOutput(message, "error");
            } else {
                completeEvaluationOutput(failedRequest, message, "error");
            }
            updateControls();
        });
    };

    form.addEventListener("submit", event => {
        event.preventDefault();
        if (!ready || activeRequest !== undefined) {
            return;
        }

        const startingExpression = source.value;
        activeRequest = {
            id: ++nextRequestId,
            source: startingExpression,
            singleStep: singleStepEnabled,
            basisStep: basisStepEnabled,
            keyStep: keyStepEnabled,
            colorize: colorizeEnabled,
            stepReady: false,
            stepPending: false,
            outputEntry: beginEvaluationOutput(startingExpression),
        };
        status.textContent = keyStepEnabled ? "Preparing…" : "Evaluating…";
        updateControls();
        const submittedRequest = activeRequest;
        const evaluationWorker = worker;
        afterNextPaint(() => {
            if (activeRequest !== submittedRequest ||
                worker !== evaluationWorker) {
                return;
            }
            evaluationWorker.postMessage({
                type: "evaluate",
                id: submittedRequest.id,
                source: submittedRequest.source,
                singleStep: submittedRequest.singleStep,
                basisStep: submittedRequest.basisStep,
                keyStep: submittedRequest.keyStep,
                colorize: submittedRequest.colorize,
            });
        });
    });

    singleStep.addEventListener("click", () => {
        singleStepEnabled = !singleStepEnabled;
        if (singleStepEnabled) {
            keyStepEnabled = false;
        }
        updateModeButtons();
        updateControls();
    });

    basisStep.addEventListener("click", () => {
        basisStepEnabled = !basisStepEnabled;
        updateModeButtons();
    });

    keyStep.addEventListener("click", () => {
        keyStepEnabled = !keyStepEnabled;
        if (keyStepEnabled) {
            singleStepEnabled = false;
        }
        updateModeButtons();
        updateControls();
    });

    colorize.addEventListener("click", () => {
        colorizeEnabled = !colorizeEnabled;
        updateModeButtons();
    });

    cancel.addEventListener("click", () => {
        if (activeRequest === undefined) {
            return;
        }

        const cancelledRequest = activeRequest;
        terminateWorker();
        completeEvaluationOutput(cancelledRequest, "[cancelled]");
        startWorker();
    });

    const configureDialog = (button, dialog) => {
        button.addEventListener("click", () => {
            dialog.showModal();
            button.setAttribute("aria-expanded", "true");
            dialog.querySelector("[data-dialog-initial-focus]")?.focus();
        });

        dialog.addEventListener("click", event => {
            if (event.target !== dialog) {
                return;
            }

            const bounds = dialog.getBoundingClientRect();
            const inside = event.clientX >= bounds.left &&
                event.clientX <= bounds.right &&
                event.clientY >= bounds.top &&
                event.clientY <= bounds.bottom;
            if (!inside) {
                dialog.close();
            }
        });

        dialog.addEventListener("keydown", event => {
            if (event.key === "Escape") {
                event.preventDefault();
                dialog.close();
            }
        });

        dialog.addEventListener("close", () => {
            button.setAttribute("aria-expanded", "false");
            if (activeRequest?.keyStep) {
                source.focus();
            } else {
                button.focus();
            }
        });
    };

    configureDialog(help, helpDialog);
    configureDialog(combinatorInfo, combinatorInfoDialog);
    configureDialog(about, aboutDialog);

    source.addEventListener("keydown", event => {
        if (event.key === "Enter" && !event.isComposing) {
            if (activeRequest?.keyStep) {
                if (!activeRequest.stepReady) {
                    event.preventDefault();
                }
                return;
            }
            event.preventDefault();
            form.requestSubmit();
        }
    });

    document.addEventListener("keydown", event => {
        const request = activeRequest;
        if (event.defaultPrevented || event.isComposing || event.repeat ||
            !ready || !request?.keyStep || !request.stepReady ||
            request.stepPending || event.ctrlKey || event.metaKey ||
            event.altKey || event.key === "Tab" ||
            event.key === "Shift" ||
            helpDialog.open || combinatorInfoDialog.open ||
            aboutDialog.open ||
            event.target instanceof HTMLButtonElement) {
            return;
        }

        event.preventDefault();
        request.stepReady = false;
        request.stepPending = true;
        status.textContent = "Reducing…";
        worker.postMessage({type: "step", id: request.id});
    });

    updateModeButtons();

    if (window.location.protocol === "file:") {
        showStartupError(
            "WebAssembly cannot run from file://.\n\n" +
            "Serve the generated directory over HTTP, for example:\n\n" +
            "python3 -m http.server 8000 --directory docs\n\n" +
            "Then open http://localhost:8000/",
        );
    } else {
        startWorker();
    }
})();
