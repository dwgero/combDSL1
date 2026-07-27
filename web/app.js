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

(() => {
    const evaluationWatchdog =
        globalThis.combdslEvaluationWatchdog;
    const form = document.querySelector("#evaluation-form");
    const source = document.querySelector("#source");
    const singleStep = document.querySelector("#single-step");
    const basisStep = document.querySelector("#basis-step");
    const keyStep = document.querySelector("#key-step");
    const colorize = document.querySelector("#colorize");
    const cancel = document.querySelector("#cancel");
    const save = document.querySelector("#save");
    const load = document.querySelector("#load");
    const loadFile = document.querySelector("#load-file");
    const nothingToSaveDialog = document.querySelector(
        "#nothing-to-save-dialog");
    const replacementDialog = document.querySelector(
        "#replacement-dialog");
    const replacementMessage = document.querySelector(
        "#replacement-message");
    const replacementReplace = document.querySelector(
        "#replacement-replace");
    const help = document.querySelector("#help");
    const helpDialog = document.querySelector("#help-dialog");
    const combinatorInfo = document.querySelector("#combinator-info");
    const combinatorInfoDialog = document.querySelector(
        "#combinator-info-dialog");
    const about = document.querySelector("#about");
    const aboutDialog = document.querySelector("#about-dialog");
    const status = document.querySelector("#status");
    const output = document.querySelector("#output");
    const user = "airings-pinker.3e";
    const domain = "icloud.com";

    let worker;
    let terminateWorker = () => {};
    let generation = 0;
    let nextRequestId = 0;
    let activeRequest;
    let replacementRequest;
    let loadRequest;
    let ready = false;
    let saveInProgress = false;
    let savedSetList = "";
    let saveDownloadUrl;
    let singleStepEnabled = false;
    let basisStepEnabled = false;
    let keyStepEnabled = false;
    let colorizeEnabled = false;

    const errorMessage = error =>
        error instanceof Error ? error.message : String(error);

    const updateControls = () => {
        const evaluating = activeRequest !== undefined;
        const busy = evaluating || saveInProgress ||
            loadRequest !== undefined;
        singleStep.disabled = !ready || busy;
        basisStep.disabled = !ready || busy;
        keyStep.disabled = !ready || busy;
        colorize.disabled = !ready || busy;
        cancel.disabled = !evaluating;
        const saveDisabled = !ready || busy;
        save.setAttribute("aria-disabled", String(saveDisabled));
        save.tabIndex = saveDisabled ? -1 : 0;
        if (saveDisabled || saveDownloadUrl === undefined) {
            save.removeAttribute("href");
        } else {
            save.href = saveDownloadUrl;
        }
        load.disabled = !ready || busy;
        loadFile.disabled = !ready || busy;
        source.readOnly = busy;
    };

    const updateModeButtons = () => {
        singleStep.setAttribute(
            "aria-pressed", String(singleStepEnabled));
        basisStep.setAttribute(
            "aria-pressed", String(basisStepEnabled));
        keyStep.setAttribute(
            "aria-pressed", String(keyStepEnabled));
        colorize.setAttribute(
            "aria-pressed", String(colorizeEnabled));
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

    const focusSource = () => {
        window.focus();
        source.focus({preventScroll: true});
        source.setSelectionRange(
            source.value.length, source.value.length);
    };

    const focusSourceAfterNextPaint = () => {
        afterNextPaint(focusSource);
    };

    window.addEventListener("pageshow", focusSourceAfterNextPaint);

    const clearEvaluationWatchdog = request => {
        if (request?.evaluationWatchdogTimer !== undefined) {
            clearTimeout(request.evaluationWatchdogTimer);
            request.evaluationWatchdogTimer = undefined;
        }
        if (request !== undefined) {
            request.evaluationWatchdogToken =
                (request.evaluationWatchdogToken ?? 0) + 1;
        }
    };

    const armEvaluationWatchdog = (request, waitMs) => {
        clearEvaluationWatchdog(request);
        if (request.keyStep) {
            return;
        }

        const expectedWorker = request.evaluationWorker;
        const expectedGeneration = request.evaluationGeneration;
        const token = request.evaluationWatchdogToken;
        const maximumTimerDelay = 2_147_483_647;
        request.evaluationWatchdogTimer = setTimeout(() => {
            if (activeRequest !== request ||
                worker !== expectedWorker ||
                generation !== expectedGeneration ||
                request.evaluationWatchdogToken !== token) {
                return;
            }
            request.evaluationWatchdogTimer = undefined;
            timeoutAndRestart(request);
        }, Math.min(waitMs, maximumTimerDelay));
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
        if (request.outputEntry === undefined) {
            request.outputEntry =
                beginEvaluationOutput(request.source);
        }
        request.outputEntry.append("\n", result);
        scrollToNewestOutput();
    };

    const clearCompletedSource = request => {
        if (source.value === request.source) {
            source.value = "";
        }
    };

    const updateSavedSetList = setList => {
        savedSetList = String(setList);
        if (saveDownloadUrl !== undefined) {
            const previousUrl = saveDownloadUrl;
            // WebKit may still be reading a recently downloaded Blob URL.
            setTimeout(() => URL.revokeObjectURL(previousUrl), 60000);
        }
        saveDownloadUrl = URL.createObjectURL(new Blob(
            [savedSetList],
            {type: "text/plain;charset=utf-8"},
        ));
        updateControls();
    };

    const beginRequestEvaluation = request => {
        if (activeRequest !== request) {
            return;
        }
        if (request.outputEntry === undefined) {
            request.outputEntry =
                beginEvaluationOutput(request.source);
        }
        status.textContent = request.keyStep
            ? "Preparing…"
            : "Evaluating…";
        updateControls();

        const evaluationWorker = worker;
        afterNextPaint(() => {
            if (activeRequest !== request ||
                worker !== evaluationWorker) {
                return;
            }
            if (!request.keyStep) {
                request.evaluationWorker = evaluationWorker;
                request.evaluationGeneration = generation;
                request.evaluationStarted = false;
                request.evaluationProgress =
                    evaluationWatchdog.createProgressState();
                armEvaluationWatchdog(
                    request, evaluationWatchdog.initialWaitMs);
            }
            evaluationWorker.postMessage({
                type: "evaluate",
                id: request.id,
                source: request.source,
                singleStep: request.singleStep,
                basisStep: request.basisStep,
                keyStep: request.keyStep,
                colorize: request.colorize,
            });
        });
    };

    const inspectRequestDefinition = request => {
        const inspectionWorker = worker;
        afterNextPaint(() => {
            if (activeRequest !== request ||
                worker !== inspectionWorker) {
                return;
            }
            inspectionWorker.postMessage({
                type: "inspect-definition",
                id: request.id,
                source: request.source,
            });
        });
    };

    const dismissReplacementDialog = () => {
        if (replacementRequest !== undefined) {
            replacementRequest.awaitingReplacement = false;
        }
        replacementRequest = undefined;
        if (replacementDialog.open) {
            replacementDialog.close("cancel");
        }
    };

    const saveSetListWithPicker = async () => {
        const setList = savedSetList;
        saveInProgress = true;
        status.textContent = "Saving set_list.cmb…";
        updateControls();

        try {
            const fileHandle = await window.showSaveFilePicker({
                suggestedName: "set_list.cmb",
                types: [{
                    description: "Combinator definitions",
                    accept: {"text/plain": [".cmb"]},
                }],
            });
            const writable = await fileHandle.createWritable();
            try {
                await writable.write(setList);
            } finally {
                await writable.close();
            }
            status.textContent = "Saved set_list.cmb";
        } catch (error) {
            status.textContent = "Ready";
            if (error?.name !== "AbortError") {
                appendOutput(errorMessage(error), "error");
            }
        } finally {
            saveInProgress = false;
            updateControls();
        }
    };

    const showStartupError = message => {
        dismissReplacementDialog();
        ready = false;
        activeRequest = undefined;
        loadRequest = undefined;
        status.textContent = "WebAssembly unavailable";
        appendOutput(message, "error");
        updateControls();
    };

    const startWorker = (setListToRestore = "") => {
        clearEvaluationWatchdog(activeRequest);
        dismissReplacementDialog();
        const currentGeneration = ++generation;
        let terminationExpected = false;
        let restoreRequestId;
        ready = false;
        activeRequest = undefined;
        loadRequest = undefined;
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
            if (message.type === "eval-started" &&
                message.id === activeRequest?.id &&
                !activeRequest.keyStep &&
                !activeRequest.evaluationStarted) {
                const request = activeRequest;
                request.evaluationStarted = true;
                request.evaluationProgress =
                    evaluationWatchdog.createProgressState();
                armEvaluationWatchdog(
                    request, evaluationWatchdog.initialWaitMs);
                return;
            }

            if (message.type === "eval-progress" &&
                message.id === activeRequest?.id &&
                !activeRequest.keyStep &&
                activeRequest.evaluationStarted) {
                const request = activeRequest;
                const waitMs = evaluationWatchdog.acceptProgress(
                    request.evaluationProgress, message);
                if (waitMs !== undefined) {
                    armEvaluationWatchdog(request, waitMs);
                }
                return;
            }

            if (message.type === "ready") {
                if (setListToRestore !== "") {
                    restoreRequestId = ++nextRequestId;
                    status.textContent = "Restoring definitions…";
                    currentWorker.postMessage({
                        type: "load",
                        id: restoreRequestId,
                        name: "saved definitions",
                        source: setListToRestore,
                    });
                    return;
                }
                updateSavedSetList(message.setList);
                ready = true;
                status.textContent = "Ready";
                updateControls();
                focusSourceAfterNextPaint();
                return;
            }

            if (message.type === "load-result" &&
                message.id === restoreRequestId) {
                restoreRequestId = undefined;
                if (!message.result.success) {
                    ready = false;
                    status.textContent = "Could not restore definitions";
                    appendOutput(
                        "Could not restore saved definitions after " +
                            `cancellation:\n${message.result.error}`,
                        "error",
                    );
                    updateControls();
                    return;
                }

                updateSavedSetList(message.setList);
                ready = true;
                status.textContent = "Ready";
                updateControls();
                focusSourceAfterNextPaint();
                return;
            }

            if (message.type === "definition-inspection-result" &&
                message.id === activeRequest?.id) {
                const request = activeRequest;
                if (!message.result.success) {
                    activeRequest = undefined;
                    status.textContent = "Ready";
                    completeEvaluationOutput(
                        request, message.result.error, "error");
                    updateControls();
                    return;
                }
                if (message.result.replacement !== "") {
                    request.awaitingReplacement = true;
                    replacementRequest = request;
                    replacementMessage.textContent =
                        `About to replace ${
                            message.result.replacement}`;
                    replacementDialog.returnValue = "";
                    status.textContent = "Waiting for confirmation…";
                    replacementDialog.showModal();
                    replacementReplace.focus({
                        preventScroll: true,
                    });
                    afterNextPaint(() => {
                        if (replacementDialog.open) {
                            replacementReplace.focus({
                                preventScroll: true,
                            });
                        }
                    });
                    updateControls();
                    return;
                }
                beginRequestEvaluation(request);
                return;
            }

            if (message.type === "load-result" &&
                message.id === loadRequest?.id) {
                const completedRequest = loadRequest;
                loadRequest = undefined;
                if (message.setList !== undefined) {
                    updateSavedSetList(message.setList);
                }
                if (message.result.success) {
                    status.textContent = `Loaded ${completedRequest.name}`;
                } else {
                    status.textContent = "Ready";
                    appendOutput(
                        message.result.error,
                        "error",
                    );
                }
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
                    focusSourceAfterNextPaint();
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
                        focusSourceAfterNextPaint();
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
                if (message.result.recoverWorker) {
                    cancelAndRestart(completedRequest);
                    return;
                }
                clearEvaluationWatchdog(completedRequest);
                activeRequest = undefined;
                status.textContent = "Ready";
                if (message.result.success) {
                    if (message.result.definition) {
                        updateSavedSetList(message.setList);
                    } else {
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
                if (failedRequest !== undefined) {
                    cancelAndRestart(failedRequest);
                    return;
                }
                dismissReplacementDialog();
                activeRequest = undefined;
                loadRequest = undefined;
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
            if (failedRequest !== undefined) {
                cancelAndRestart(failedRequest);
                return;
            }
            dismissReplacementDialog();
            activeRequest = undefined;
            loadRequest = undefined;
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

    const restartEvaluation = (request, message) => {
        if (activeRequest !== request) {
            return;
        }

        clearEvaluationWatchdog(request);
        terminateWorker();
        completeEvaluationOutput(request, message);
        startWorker(savedSetList);
    };

    const cancelAndRestart = request => {
        restartEvaluation(request, "[cancelled]");
    };

    const timeoutAndRestart = request => {
        restartEvaluation(
            request,
            evaluationWatchdog.timeoutMessage(
                request.evaluationProgress),
        );
    };

    form.addEventListener("submit", event => {
        event.preventDefault();
        if (!ready || activeRequest !== undefined ||
            loadRequest !== undefined) {
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
            awaitingReplacement: false,
            outputEntry: beginEvaluationOutput(startingExpression),
        };
        status.textContent = "Scanning…";
        updateControls();
        inspectRequestDefinition(activeRequest);
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
        focusSourceAfterNextPaint();
    });

    colorize.addEventListener("click", () => {
        colorizeEnabled = !colorizeEnabled;
        updateModeButtons();
    });

    save.addEventListener("click", event => {
        if (!ready || activeRequest !== undefined ||
            saveInProgress || loadRequest !== undefined) {
            event.preventDefault();
            return;
        }

        if (savedSetList === "") {
            event.preventDefault();
            status.textContent = "Ready";
            nothingToSaveDialog.showModal();
            nothingToSaveDialog.querySelector(
                "[data-dialog-initial-focus]")?.focus();
            return;
        }

        if (typeof window.showSaveFilePicker === "function") {
            event.preventDefault();
            void saveSetListWithPicker();
            return;
        }

        status.textContent = "Saved all definitions";
    });

    load.addEventListener("click", () => {
        if (!ready || activeRequest !== undefined ||
            loadRequest !== undefined) {
            return;
        }

        loadFile.value = "";
        loadFile.click();
    });

    loadFile.addEventListener("change", () => {
        const file = loadFile.files?.[0];
        if (file === undefined || !ready ||
            activeRequest !== undefined ||
            loadRequest !== undefined) {
            return;
        }

        const request = {
            id: ++nextRequestId,
            name: file.name,
            reader: new FileReader(),
        };
        loadRequest = request;
        status.textContent = `Reading ${request.name}…`;
        updateControls();

        request.reader.addEventListener("load", () => {
            if (loadRequest !== request) {
                return;
            }
            if (typeof request.reader.result !== "string") {
                loadRequest = undefined;
                status.textContent = "Ready";
                appendOutput(
                    `${request.name}: could not read the file as text`,
                    "error",
                );
                updateControls();
                return;
            }

            status.textContent = `Loading ${request.name}…`;
            worker.postMessage({
                type: "load",
                id: request.id,
                name: request.name,
                source: request.reader.result,
            });
        });

        request.reader.addEventListener("error", () => {
            if (loadRequest !== request) {
                return;
            }
            loadRequest = undefined;
            status.textContent = "Ready";
            appendOutput(
                `${request.name}: ${errorMessage(
                    request.reader.error ??
                    new Error("could not read the file"))}`,
                "error",
            );
            updateControls();
        });

        request.reader.readAsText(file);
    });

    cancel.addEventListener("click", () => {
        if (activeRequest === undefined) {
            return;
        }
        cancelAndRestart(activeRequest);
    });

    const configureDialog = (button, dialog) => {
        button.addEventListener("click", () => {
            if (replacementRequest !== undefined ||
                replacementDialog.open) {
                return;
            }
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

    nothingToSaveDialog.addEventListener("close", () => {
        save.focus();
    });

    replacementDialog.addEventListener("cancel", event => {
        event.preventDefault();
        replacementDialog.close("cancel");
    });

    replacementDialog.addEventListener("close", () => {
        const request = replacementRequest;
        replacementRequest = undefined;
        if (request === undefined ||
            activeRequest !== request ||
            !request.awaitingReplacement) {
            return;
        }

        request.awaitingReplacement = false;
        if (replacementDialog.returnValue === "replace") {
            beginRequestEvaluation(request);
            return;
        }

        completeEvaluationOutput(request, "[cancelled]");
        activeRequest = undefined;
        status.textContent = "Ready";
        updateControls();
        focusSourceAfterNextPaint();
    });

    window.addEventListener("pagehide", () => {
        clearEvaluationWatchdog(activeRequest);
        if (saveDownloadUrl !== undefined) {
            URL.revokeObjectURL(saveDownloadUrl);
        }
    }, {once: true});

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
            aboutDialog.open || replacementDialog.open ||
            event.target instanceof HTMLButtonElement) {
            return;
        }

        event.preventDefault();
        request.stepReady = false;
        request.stepPending = true;
        status.textContent = "Reducing…";
        worker.postMessage({type: "step", id: request.id});
    });

    const emailElement = document.getElementById("email-box");
    emailElement.innerHTML = `<a href="mailto:${user}@${domain}">${user}@${domain}</a>`;

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
