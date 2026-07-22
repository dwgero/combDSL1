"use strict";

(() => {
    const form = document.querySelector("#evaluation-form");
    const source = document.querySelector("#source");
    const singleStep = document.querySelector("#single-step");
    const keyStep = document.querySelector("#key-step");
    const cancel = document.querySelector("#cancel");
    const help = document.querySelector("#help");
    const helpDialog = document.querySelector("#help-dialog");
    const status = document.querySelector("#status");
    const output = document.querySelector("#output");

    let worker;
    let terminateWorker = () => {};
    let generation = 0;
    let nextRequestId = 0;
    let activeRequest;
    let ready = false;
    let singleStepEnabled = false;
    let keyStepEnabled = false;

    const errorMessage = error =>
        error instanceof Error ? error.message : String(error);

    const updateControls = () => {
        const evaluating = activeRequest !== undefined;
        singleStep.disabled = !ready || evaluating;
        keyStep.disabled = !ready || evaluating;
        cancel.disabled = !evaluating;
        source.readOnly = evaluating;
    };

    const updateModeButtons = () => {
        singleStep.setAttribute(
            "aria-pressed", String(singleStepEnabled));
        singleStep.textContent = `Single Step: ${
            singleStepEnabled ? "On" : "Off"}`;
        keyStep.setAttribute(
            "aria-pressed", String(keyStepEnabled));
        keyStep.textContent = `Key Step: ${
            keyStepEnabled ? "On" : "Off"}`;
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

    const completeEvaluationOutput = (request, text, kind = "output") => {
        const content = outputText(text);
        if (content === "") {
            return;
        }

        const result = document.createElement("span");
        result.textContent = content;
        result.dataset.kind = kind;
        request.outputEntry.append("\n", result);
        scrollToNewestOutput();
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
            worker = new Worker(new URL("./worker.js", document.baseURI));
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
                        request, message.result.output);
                    request.stepReady = true;
                    status.textContent = "Press a key for the next step";
                } else {
                    activeRequest = undefined;
                    status.textContent = "Normal form";
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
                    completeEvaluationOutput(
                        completedRequest, message.result.output);
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
            keyStep: keyStepEnabled,
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
                keyStep: submittedRequest.keyStep,
            });
        });
    });

    singleStep.addEventListener("click", () => {
        singleStepEnabled = !singleStepEnabled;
        if (singleStepEnabled) {
            keyStepEnabled = false;
        }
        updateModeButtons();
    });

    keyStep.addEventListener("click", () => {
        keyStepEnabled = !keyStepEnabled;
        if (keyStepEnabled) {
            singleStepEnabled = false;
        }
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

    help.addEventListener("click", () => {
        helpDialog.showModal();
    });

    helpDialog.addEventListener("click", event => {
        if (event.target !== helpDialog) {
            return;
        }

        const bounds = helpDialog.getBoundingClientRect();
        const inside = event.clientX >= bounds.left &&
            event.clientX <= bounds.right &&
            event.clientY >= bounds.top &&
            event.clientY <= bounds.bottom;
        if (!inside) {
            helpDialog.close();
        }
    });

    helpDialog.addEventListener("keydown", event => {
        if (event.key === "Escape") {
            event.preventDefault();
            helpDialog.close();
        }
    });

    helpDialog.addEventListener("close", () => {
        if (activeRequest?.keyStep) {
            source.focus();
        } else {
            help.focus();
        }
    });

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
            helpDialog.open ||
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
            "python3 -m http.server 8000 --directory build-browser/web\n\n" +
            "Then open http://localhost:8000/",
        );
    } else {
        startWorker();
    }
})();
