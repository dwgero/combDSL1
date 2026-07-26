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
        ready = false;
        activeRequest = undefined;
        loadRequest = undefined;
        status.textContent = "WebAssembly unavailable";
        appendOutput(message, "error");
        updateControls();
    };

    const startWorker = () => {
        const currentGeneration = ++generation;
        let terminationExpected = false;
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
            if (message.type === "ready") {
                updateSavedSetList(message.setList);
                ready = true;
                status.textContent = "Ready";
                updateControls();
                focusSourceAfterNextPaint();
                return;
            }

            if (message.type === "load-result" &&
                message.id === loadRequest?.id) {
                const completedRequest = loadRequest;
                loadRequest = undefined;
                if (message.result.success) {
                    updateSavedSetList(message.setList);
                    status.textContent = `Loaded ${completedRequest.name}`;
                } else {
                    status.textContent = "Ready";
                    const line = message.result.line === 0
                        ? ""
                        : `line ${message.result.line}: `;
                    appendOutput(
                        `${completedRequest.name}: ${line}${
                            message.result.error}`,
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

        status.textContent = "Saved set_list.cmb";
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

    nothingToSaveDialog.addEventListener("close", () => {
        save.focus();
    });

    window.addEventListener("pagehide", () => {
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
