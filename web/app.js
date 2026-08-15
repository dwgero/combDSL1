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
    const compareWatchdogTimeoutMs =
        evaluationWatchdog.timeoutMs + 500;
    const isCompareCommand = value =>
        /^[ \t\n\r\f\v]*compare(?:[ \t\n\r\f\v]|$)/.test(
            String(value));
    const evaluationWatchdogTimeoutFor = request =>
        request?.compareCommand
            ? compareWatchdogTimeoutMs
            : evaluationWatchdog.timeoutMs;
    const inputHistoryTools = globalThis.combdslInputHistory;
    const inputHistoryStorage = (() => {
        try {
            return window.localStorage;
        } catch {
            return undefined;
        }
    })();
    const inputHistory = inputHistoryTools.create({
        storage: inputHistoryStorage,
    });
    const completeTopLevelCommand =
        inputHistoryTools.createCommandCompleter([
        "abstract",
        "compare",
        "define",
        "depends on",
        "depends-on",
        "dependson",
        "find",
        "inspect",
        "references",
        "remove",
        "revisions",
        "set",
        "show",
        "step limit",
        "used by",
        "used-by",
        "usedby",
    ], {appendSpaceToExact: true});
    const completeShowAll =
        inputHistoryTools.createCommandCompleter(["show all"]);
    const completeDependencyAll =
        inputHistoryTools.createCommandCompleter(
            [
                "depends on all",
                "depends-on all",
                "dependson all",
                "used by all",
                "used-by all",
                "usedby all",
            ],
            {appendSpaceToExact: true});
    const completeDependencyPath =
        inputHistoryTools.createCommandCompleter(
            [
                "used by path",
                "used-by path",
                "usedby path",
            ],
            {appendSpaceToExact: true});
    const completeDependencyPathBetween =
        inputHistoryTools.createCommandCompleter(
            [
                "used by path between",
                "used-by path between",
                "usedby path between",
            ],
            {appendSpaceToExact: true});
    const completeDependencyPathAnd = source => {
        const original = String(source);
        const tokenPattern = /[^ \t\n\r\f\v]+/g;
        const tokens = Array.from(
            original.matchAll(tokenPattern),
            match => ({text: match[0], index: match.index}),
        );
        if (tokens.length === 0) {
            return undefined;
        }

        const endsWithWhitespace = /[ \t\n\r\f\v]$/.test(original);
        const partial = endsWithWhitespace ? "" : tokens.at(-1).text;
        if (!"and".startsWith(partial)) {
            return undefined;
        }
        const prefixTokens = endsWithWhitespace
            ? tokens.map(token => token.text)
            : tokens.slice(0, -1).map(token => token.text);
        const compact =
            (prefixTokens[0] === "usedby" ||
                prefixTokens[0] === "used-by") &&
            prefixTokens[1] === "path";
        const twoWord =
            prefixTokens[0] === "used" &&
            prefixTokens[1] === "by" &&
            prefixTokens[2] === "path";
        const argument = compact ? 2 : twoWord ? 3 : undefined;
        if (argument === undefined) {
            return undefined;
        }
        const hasEndpoint =
            (prefixTokens.length === argument + 1 &&
                prefixTokens[argument] !== "between") ||
            (prefixTokens.length === argument + 2 &&
                prefixTokens[argument] === "between");
        if (!hasEndpoint) {
            return undefined;
        }

        const beforePartial = endsWithWhitespace
            ? original
            : original.slice(0, tokens.at(-1).index);
        return `${beforePartial}and `;
    };
    const completeDefinitionReferenceMode =
        inputHistoryTools.createCommandCompleter(
            [
                "define captured",
                "define live",
                "set captured",
                "set live",
            ],
            {appendSpaceToExact: true});
    const completeReferencesCommand =
        inputHistoryTools.createCommandCompleter([
            "references captured",
            "references live",
        ]);
    const completeStepLimitCommand =
        inputHistoryTools.createCommandCompleter([
            "step limit off",
        ]);
    const abstractCommandCompleters = [
        "abstract ?",
        "abstract steps ?",
        "abstract ministeps ?",
    ].map(phrase =>
        inputHistoryTools.createCommandCompleter([phrase]));
    const completeAbstractCommand = source => {
        for (const complete of abstractCommandCompleters) {
            const completed = complete(source);
            if (completed !== undefined) {
                return completed;
            }
        }
        return undefined;
    };
    const completeCompareCommand =
        inputHistoryTools.createCommandCompleter(["compare ?"]);
    const findCommandCompleters = [
        "find ?",
        "find all ?",
        "find 1 ?",
        "find all 1 ?",
        "find 2 ?",
        "find all 2 ?",
        "find 3 ?",
        "find all 3 ?",
        "find 4 ?",
        "find all 4 ?",
    ].map(phrase =>
        inputHistoryTools.createCommandCompleter([phrase]));
    const completeFindCommand = source => {
        for (const complete of findCommandCompleters) {
            const completed = complete(source);
            if (completed !== undefined) {
                return completed;
            }
        }
        return undefined;
    };
    const completeCommand = source =>
        completeTopLevelCommand(source) ??
            completeDependencyAll(source) ??
            completeDependencyPath(source) ??
            completeDependencyPathBetween(source) ??
            completeDependencyPathAnd(source) ??
            completeDefinitionReferenceMode(source) ??
            completeAbstractCommand(source) ??
            completeCompareCommand(source) ??
            completeShowAll(source) ??
            completeReferencesCommand(source) ??
            completeStepLimitCommand(source) ??
            completeFindCommand(source);
    const form = document.querySelector("#evaluation-form");
    const sourceBox = document.querySelector("#source-box");
    const sourceHistory = document.querySelector("#source-history");
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
    const stepLimitDialog = document.querySelector(
        "#step-limit-dialog");
    const stepLimitDialogTitle = document.querySelector(
        "#step-limit-title");
    const stepLimitDialogMessage = document.querySelector(
        "#step-limit-message");
    const stepLimitResume = document.querySelector(
        "#step-limit-resume");
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
    let stepLimitRequest;
    let loadRequest;
    let ready = false;
    let workerStarting = false;
    let submitWhenReady = false;
    let saveInProgress = false;
    let savedSetList = "";
    let saveDownloadUrl;
    let pendingOperateAndGetNext;
    let untouchedHistoryRecall = false;
    let historyRecallSelectionStart = 0;
    let historyRecallSelectionEnd = 0;
    let historyRecallSelectionDirection = "none";
    let singleStepEnabled = false;
    let basisStepEnabled = false;
    let keyStepEnabled = false;
    let colorizeEnabled = false;
    let stepLimit;
    let outputScrollScheduled = false;
    let sourceScrollScheduled = false;
    const blobDownloadsSupported =
        typeof URL.createObjectURL === "function" &&
        typeof URL.revokeObjectURL === "function";

    const errorMessage = error =>
        error instanceof Error ? error.message : String(error);

    const updateControls = () => {
        const evaluating = activeRequest !== undefined;
        const busy = evaluating || saveInProgress ||
            loadRequest !== undefined || submitWhenReady;
        singleStep.disabled = !ready || busy;
        basisStep.disabled = !ready || busy;
        keyStep.disabled = !ready || busy;
        colorize.disabled = !ready || busy;
        cancel.disabled = !evaluating || workerStarting ||
            activeRequest.pauseRequested ||
            activeRequest.awaitingPause;
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
        output.setAttribute(
            "aria-live",
            evaluating && activeRequest.singleStep &&
                !activeRequest.keyStep
                ? "off"
                : "polite");
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
        if (outputScrollScheduled) {
            return;
        }
        outputScrollScheduled = true;
        requestAnimationFrame(() => {
            outputScrollScheduled = false;
            output.scrollTop = output.scrollHeight;
        });
    };

    const scrollToNewestSource = () => {
        if (sourceScrollScheduled) {
            return;
        }
        sourceScrollScheduled = true;
        requestAnimationFrame(() => {
            sourceScrollScheduled = false;
            sourceBox.scrollTop = sourceBox.scrollHeight;
        });
    };

    const resizeSourceEditor = () => {
        source.style.height = "0";
        source.style.height = `${source.scrollHeight}px`;
        scrollToNewestSource();
    };

    const setSourceText = text => {
        source.value = text;
        source.setSelectionRange(text.length, text.length);
        resizeSourceEditor();
    };

    const clearUntouchedHistoryRecall = () => {
        untouchedHistoryRecall = false;
    };

    const rememberUntouchedHistoryRecall = () => {
        untouchedHistoryRecall = inputHistory.hasCurrent();
        historyRecallSelectionStart = source.selectionStart;
        historyRecallSelectionEnd = source.selectionEnd;
        historyRecallSelectionDirection =
            source.selectionDirection ?? "none";
    };

    const historyRecallSelectionChanged = () => {
        if (untouchedHistoryRecall &&
            (source.selectionStart !== historyRecallSelectionStart ||
                source.selectionEnd !== historyRecallSelectionEnd ||
                (source.selectionDirection ?? "none") !==
                    historyRecallSelectionDirection)) {
            clearUntouchedHistoryRecall();
        }
    };

    const redNoticePattern =
        /\[(?:cancelled|step limit|timed out[^\]\r\n]*)\]/g;

    const appendTextWithRedNotices = (parent, text) => {
        const content = String(text);
        let end = 0;
        for (const match of content.matchAll(redNoticePattern)) {
            parent.append(content.slice(end, match.index));
            const notice = document.createElement("span");
            notice.textContent = match[0];
            notice.dataset.kind = "error";
            // Source history is outside the Results <pre>, whose stylesheet
            // normally supplies this color for error and notice spans.
            notice.style.color = "#c33";
            parent.append(notice);
            end = match.index + match[0].length;
        }
        parent.append(content.slice(end));
    };

    const appendSourceHistoryEntry = entry => {
        const historyEntry =
            document.createElement("div");
        appendTextWithRedNotices(historyEntry, entry);
        sourceHistory.append(historyEntry);
        scrollToNewestSource();
    };

    const appendSourceHistory = (
        sourceText,
        outcome = "",
    ) => {
        inputHistory.record(sourceText, outcome);
        renderSourceHistory();
    };

    const renderSourceHistory = () => {
        sourceHistory.textContent = "";
        inputHistory.values().forEach(appendSourceHistoryEntry);
    };

    renderSourceHistory();

    window.addEventListener("storage", event => {
        if (inputHistoryStorage === undefined ||
            event.storageArea !== inputHistoryStorage ||
            !inputHistory.handlesStorageKey(event.key)) {
            return;
        }

        const update = inputHistory.synchronizeStorage();
        if (!update.changed) {
            return;
        }

        renderSourceHistory();
        if (update.currentRemoved) {
            clearUntouchedHistoryRecall();
        }
    });

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
        if (request.keyStep || request.awaitingPause ||
            document.hidden) {
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

    const rearmVisibleEvaluationWatchdog = () => {
        const request = activeRequest;
        if (document.hidden ||
            request?.evaluationWorker !== worker ||
            request.evaluationGeneration !== generation ||
            request.keyStep || request.awaitingPause) {
            return;
        }
        armEvaluationWatchdog(
            request, evaluationWatchdogTimeoutFor(request));
    };

    window.addEventListener("pageshow", () => {
        focusSourceAfterNextPaint();
        rearmVisibleEvaluationWatchdog();
    });

    document.addEventListener("visibilitychange", () => {
        if (document.hidden) {
            clearEvaluationWatchdog(activeRequest);
            return;
        }
        rearmVisibleEvaluationWatchdog();
    });

    const createOutputEntry = () => {
        if (output.childNodes.length !== 0) {
            output.append(
                output.lastElementChild?.dataset.compactAfter === "true"
                    ? "\n"
                    : "\n\n");
        }

        const entry = document.createElement("span");
        output.append(entry);
        return entry;
    };

    const ensureBlankLineBeforeOutputEntry = entry => {
        const separator = entry.previousSibling;
        if (separator?.nodeType === Node.TEXT_NODE &&
            separator.textContent === "\n") {
            separator.textContent = "\n\n";
        }
    };

    const appendOutput = (text, kind = "output") => {
        const entry = createOutputEntry();
        const content = document.createElement("span");
        appendTextWithRedNotices(content, outputText(text));
        content.dataset.kind = kind;
        entry.append(content);
        if (kind === "error") {
            entry.dataset.compactAfter = "true";
        }
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
            // color_step_html escapes expression text and emits only its fixed
            // color markup.
            result.innerHTML = content;
        } else {
            appendTextWithRedNotices(result, content);
        }
        result.dataset.kind = kind;
        if (request.outputEntry === undefined) {
            request.outputEntry =
                beginEvaluationOutput(request.source);
        }
        request.outputEntry.append("\n", result);
        if (kind === "error" || kind === "notice") {
            request.outputEntry.dataset.compactAfter = "true";
        }
        scrollToNewestOutput();
    };

    const streamSingleStepOutput = (
        request,
        text,
        html,
    ) => {
        const content = outputText(String(text));
        if (content === "") {
            return;
        }
        if (html) {
            completeEvaluationOutput(
                request, content, "output", true);
            return;
        }

        if (request.outputEntry === undefined) {
            request.outputEntry =
                beginEvaluationOutput(request.source);
        }
        if (request.streamedPlainOutput === undefined) {
            const result = document.createElement("span");
            result.dataset.kind = "output";
            request.streamedPlainOutput =
                document.createTextNode(content);
            result.append(request.streamedPlainOutput);
            request.outputEntry.append("\n", result);
        } else {
            request.streamedPlainOutput.appendData(
                `\n${content}`);
        }
        scrollToNewestOutput();
    };

    const updateEvaluationProgress = (
        request,
        reductions,
        {exact = false, final = false} = {},
    ) => {
        const displayExactCount = exact || final;
        const completedMilestone = displayExactCount
            ? evaluationWatchdog.finalDisplayedStepCount(reductions)
            : evaluationWatchdog.displayedStepCount(reductions);
        const displayedMilestone = request.displayedSteps;
        if (completedMilestone === 0 ||
            completedMilestone < displayedMilestone ||
            (!displayExactCount &&
                completedMilestone === displayedMilestone)) {
            return;
        }

        if (request.outputEntry === undefined) {
            request.outputEntry =
                beginEvaluationOutput(request.source);
        }
        if (request.progressEntry === undefined) {
            request.progressEntry = document.createElement("span");
            request.progressEntry.dataset.kind = "progress";
            request.outputEntry.append("\n", request.progressEntry);
        }
        request.displayedSteps = completedMilestone;
        request.progressEntry.textContent = exact && !final
            ? `[${completedMilestone} steps so far]`
            : evaluationWatchdog.stepCountMessage(
                  reductions, final);
        scrollToNewestOutput();
    };

    const completeSuccessfulSource = (request, outcome = "") => {
        appendSourceHistory(request.source, outcome);
        if (source.value === request.source) {
            const nextSource = request.operateAndGetNext === undefined
                ? ""
                : inputHistory.resumeOperateAndGetNext(
                    request.operateAndGetNext) ?? "";
            clearUntouchedHistoryRecall();
            setSourceText(nextSource);
        }
    };

    const updateSavedSetList = setList => {
        savedSetList = String(setList);
        if (saveDownloadUrl !== undefined &&
            blobDownloadsSupported) {
            const previousUrl = saveDownloadUrl;
            // WebKit may still be reading a recently downloaded Blob URL.
            setTimeout(() => URL.revokeObjectURL(previousUrl), 60000);
        }
        saveDownloadUrl = blobDownloadsSupported
            ? URL.createObjectURL(new Blob(
                [savedSetList],
                {type: "text/plain;charset=utf-8"},
            ))
            : "data:text/plain;charset=utf-8," +
                encodeURIComponent(savedSetList);
        updateControls();
    };

    const completeWorkerStartup = (
        setList,
        requestToResume,
    ) => {
        updateSavedSetList(setList);
        workerStarting = false;
        ready = true;
        status.textContent = "Ready";
        if (requestToResume !== undefined &&
            activeRequest === requestToResume) {
            beginRequestEvaluation(requestToResume);
            return;
        }
        if (submitWhenReady) {
            submitWhenReady = false;
            form.requestSubmit();
            return;
        }
        updateControls();
        focusSourceAfterNextPaint();
    };

    const beginRequestEvaluation = request => {
        if (activeRequest !== request) {
            return;
        }
        if (request.pauseRequested) {
            request.resumeBeforeEvaluation = true;
            pauseEvaluation(request, "manual");
            return;
        }
        if (request.outputEntry === undefined) {
            request.outputEntry =
                beginEvaluationOutput(request.source);
        }
        status.textContent = request.findCommand
            ? "Searching…"
            : request.keyStep
                ? "Preparing…"
                : "Evaluating…";
        updateControls();

        const evaluationWorker = worker;
        afterNextPaint(() => {
            if (activeRequest !== request ||
                worker !== evaluationWorker) {
                return;
            }
            if (request.pauseRequested) {
                request.resumeBeforeEvaluation = true;
                pauseEvaluation(request, "manual");
                return;
            }
            if (!request.keyStep && !request.findCommand) {
                request.evaluationWorker = evaluationWorker;
                request.evaluationGeneration = generation;
                request.evaluationStarted = false;
                request.evaluationProgress =
                    evaluationWatchdog.createProgressState();
                armEvaluationWatchdog(
                    request, evaluationWatchdogTimeoutFor(request));
            }
            evaluationWorker.postMessage({
                type: "evaluate",
                id: request.id,
                source: request.source,
                singleStep: request.findCommand
                    ? false
                    : request.singleStep,
                basisStep: request.basisStep,
                keyStep: request.findCommand
                    ? false
                    : request.keyStep,
                colorize: request.colorize,
                stepLimitEnabled:
                    !request.keyStep &&
                    request.stepLimit !== undefined,
                stepLimit: request.keyStep
                    ? 0
                    : request.stepLimit ?? 0,
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

    const dismissStepLimitDialog = () => {
        if (stepLimitRequest !== undefined) {
            stepLimitRequest.awaitingPause = false;
        }
        stepLimitRequest = undefined;
        if (stepLimitDialog.open) {
            stepLimitDialog.close("cancel");
        }
    };

    const pauseEvaluation = (request, reason) => {
        if (activeRequest !== request) {
            return;
        }

        clearEvaluationWatchdog(request);
        request.evaluationStarted = false;
        request.stepReady = false;
        request.stepPending = false;
        request.pauseRequested = false;
        request.awaitingPause = true;
        request.pauseReason = reason;
        stepLimitRequest = request;
        const stepLimitReached = reason === "step-limit";
        stepLimitDialogTitle.textContent = stepLimitReached
            ? "Step limit reached"
            : "Paused";
        stepLimitDialogMessage.textContent = stepLimitReached
            ? "Step limit reached after " + request.stepLimit + " " +
                (request.stepLimit === 1 ? "step." : "steps.")
            : "";
        stepLimitDialogMessage.hidden = !stepLimitReached;
        stepLimitDialog.returnValue = "";
        status.textContent = stepLimitReached
            ? "Step limit reached"
            : "Paused";
        stepLimitDialog.showModal();
        stepLimitResume.focus({preventScroll: true});
        afterNextPaint(() => {
            if (stepLimitDialog.open) {
                stepLimitResume.focus({preventScroll: true});
            }
        });
        updateControls();
    };

    const pauseAtStepLimit = request => {
        pauseEvaluation(request, "step-limit");
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
        dismissStepLimitDialog();
        workerStarting = false;
        submitWhenReady = false;
        ready = false;
        activeRequest = undefined;
        loadRequest = undefined;
        status.textContent = "WebAssembly unavailable";
        appendOutput(message, "error");
        updateControls();
    };

    const startWorker = (
        setListToRestore = "",
        requestToResume,
    ) => {
        clearEvaluationWatchdog(activeRequest);
        dismissReplacementDialog();
        dismissStepLimitDialog();
        const currentGeneration = ++generation;
        let currentWorker;
        let terminationExpected = false;
        let restoreRequestId;
        workerStarting = true;
        ready = false;
        activeRequest = requestToResume;
        loadRequest = undefined;
        status.textContent = "Loading WebAssembly…";
        updateControls();

        try {
            const workerUrl = new URL("./worker.js", document.baseURI);
            workerUrl.searchParams.set("v", Date.now().toString());
            currentWorker = new Worker(workerUrl);
            worker = currentWorker;
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
            if (message.type === "paused" &&
                message.id === activeRequest?.id &&
                activeRequest.pauseRequested) {
                const request = activeRequest;
                if (!request.singleStep && !request.keyStep &&
                    !request.findCommand &&
                    Number.isFinite(Number(message.reductions))) {
                    updateEvaluationProgress(
                        request,
                        Number(message.reductions),
                        {exact: true});
                }
                pauseEvaluation(request, "manual");
                return;
            }

            if (message.type === "eval-started" &&
                message.id === activeRequest?.id &&
                !activeRequest.keyStep &&
                !activeRequest.findCommand &&
                !activeRequest.evaluationStarted) {
                const request = activeRequest;
                request.evaluationStarted = true;
                request.evaluationProgress ??=
                    evaluationWatchdog.createProgressState();
                armEvaluationWatchdog(
                    request, evaluationWatchdogTimeoutFor(request));
                return;
            }

            if (message.type === "eval-progress" &&
                message.id === activeRequest?.id &&
                !activeRequest.keyStep &&
                activeRequest.evaluationStarted) {
                const request = activeRequest;
                if (evaluationWatchdog.acceptProgress(
                    request.evaluationProgress, message)) {
                    armEvaluationWatchdog(
                        request, evaluationWatchdogTimeoutFor(request));
                    if (!request.singleStep &&
                        !request.keyStep) {
                        updateEvaluationProgress(
                            request,
                            request.evaluationProgress.reductions);
                    }
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
                completeWorkerStartup(
                    message.setList, requestToResume);
                return;
            }

            if (message.type === "load-result" &&
                message.id === restoreRequestId) {
                restoreRequestId = undefined;
                if (!message.result.success) {
                    workerStarting = false;
                    submitWhenReady = false;
                    ready = false;
                    status.textContent = "Could not restore definitions";
                    appendOutput(
                        "Could not restore saved definitions after " +
                            `restarting the worker:\n${message.result.error}`,
                        "error",
                    );
                    updateControls();
                    return;
                }

                completeWorkerStartup(
                    message.setList, requestToResume);
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
                if (message.result.stepLimitCommand) {
                    stepLimit = message.result.stepLimitEnabled
                        ? Number(message.result.stepLimit)
                        : undefined;
                    activeRequest = undefined;
                    status.textContent = "Ready";
                    request.outputEntry.dataset.compactAfter = "true";
                    completeSuccessfulSource(request);
                    updateControls();
                    return;
                }
                request.displayOnly =
                    Boolean(message.result.displayOnly);
                request.showAll = Boolean(message.result.showAll);
                request.findCommand = Boolean(message.result.find);
                if (request.displayOnly) {
                    request.singleStep = false;
                    request.keyStep = false;
                    request.basisStep = false;
                    request.colorize = false;
                }
                if (!message.result.definition &&
                    !request.displayOnly) {
                    ensureBlankLineBeforeOutputEntry(
                        request.outputEntry);
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
                    if (!request.pauseRequested &&
                        !request.awaitingPause) {
                        request.stepReady = true;
                        status.textContent =
                            "Press a key for the next step";
                        focusSourceAfterNextPaint();
                    }
                } else {
                    activeRequest = undefined;
                    status.textContent = "Ready";
                    completeEvaluationOutput(
                        request, message.result.error, "error");
                }
                updateControls();
                return;
            }

            if (message.type === "single-step-output" &&
                message.id === activeRequest?.id &&
                activeRequest.singleStep &&
                !activeRequest.keyStep) {
                streamSingleStepOutput(
                    activeRequest,
                    message.output,
                    Boolean(message.html));
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
                        completeSuccessfulSource(request);
                        activeRequest = undefined;
                        status.textContent = "Normal form reached";
                    } else {
                        request.stepReady = true;
                        status.textContent =
                            "Press a key for the next step";
                        focusSourceAfterNextPaint();
                    }
                } else {
                    completeSuccessfulSource(request);
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
                    failedEvaluationAndRestart(completedRequest);
                    return;
                }
                clearEvaluationWatchdog(completedRequest);
                if (message.result.success &&
                    message.result.limitReached) {
                    if (!completedRequest.singleStep &&
                        !completedRequest.keyStep &&
                        !completedRequest.findCommand) {
                        updateEvaluationProgress(
                            completedRequest,
                            message.result.reductions,
                            {exact: true});
                    }
                    pauseAtStepLimit(completedRequest);
                    return;
                }
                if (message.result.success &&
                    !completedRequest.singleStep &&
                    !completedRequest.keyStep &&
                    !completedRequest.findCommand) {
                    updateEvaluationProgress(
                        completedRequest,
                        message.result.reductions,
                        {exact: true, final: true});
                }
                if (message.result.success) {
                    if (message.result.definition) {
                        updateSavedSetList(message.setList);
                    }
                    if (!message.result.definition ||
                        completedRequest.displayOnly) {
                        const nothingToShow =
                            completedRequest.showAll &&
                            outputText(message.result.output) ===
                                "Nothing to show";
                        const noFindMatch =
                            completedRequest.findCommand &&
                            outputText(message.result.output) ===
                                "No match within search bounds";
                        completeEvaluationOutput(
                            completedRequest,
                            message.result.output,
                            nothingToShow || noFindMatch
                                ? "notice"
                                : "output",
                            Boolean(message.html));
                        if (completedRequest.showAll &&
                            !nothingToShow) {
                            completeEvaluationOutput(
                                completedRequest,
                                "[show end]",
                                "notice");
                        }
                        if (completedRequest.displayOnly) {
                            completedRequest.outputEntry.dataset.compactAfter =
                                "true";
                        }
                    } else {
                        completedRequest.outputEntry.dataset.compactAfter =
                            "true";
                    }
                    completeSuccessfulSource(completedRequest);
                } else {
                    completeEvaluationOutput(
                        completedRequest, message.result.error, "error");
                }
                activeRequest = undefined;
                status.textContent = "Ready";
                updateControls();
                return;
            }

            if (message.type === "fatal") {
                const failedRequest = activeRequest;
                if (failedRequest !== undefined) {
                    failedEvaluationAndRestart(failedRequest);
                    return;
                }
                dismissReplacementDialog();
                dismissStepLimitDialog();
                workerStarting = false;
                submitWhenReady = false;
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
                failedEvaluationAndRestart(failedRequest);
                return;
            }
            dismissReplacementDialog();
            dismissStepLimitDialog();
            workerStarting = false;
            submitWhenReady = false;
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

    const restartEvaluation = (
        request,
        message,
        historyOutcome,
    ) => {
        if (activeRequest !== request) {
            return;
        }

        clearEvaluationWatchdog(request);
        terminateWorker();
        appendSourceHistory(
            request.source, historyOutcome);
        completeEvaluationOutput(request, message, "error");
        startWorker(savedSetList);
    };

    const cancelAndRestart = request => {
        restartEvaluation(
            request, "[cancelled]", "cancelled");
    };

    const timeoutAndRestart = request => {
        restartEvaluation(
            request,
            evaluationWatchdog.timeoutMessage(
                request.evaluationProgress),
            "timed out",
        );
    };

    const failedEvaluationAndRestart = request => {
        if (!request.keyStep &&
            request.evaluationProgress !== undefined) {
            timeoutAndRestart(request);
            return;
        }
        cancelAndRestart(request);
    };

    form.addEventListener("submit", event => {
        event.preventDefault();
        if (activeRequest !== undefined || loadRequest !== undefined) {
            return;
        }
        if (!ready) {
            if (workerStarting) {
                submitWhenReady = true;
                updateControls();
            }
            return;
        }
        const operateAndGetNext = pendingOperateAndGetNext;
        pendingOperateAndGetNext = undefined;

        const startingExpression = source.value;
        inputHistory.resetNavigation();
        clearUntouchedHistoryRecall();
        activeRequest = {
            id: ++nextRequestId,
            source: startingExpression,
            compareCommand: isCompareCommand(startingExpression),
            singleStep: singleStepEnabled,
            basisStep: basisStepEnabled,
            keyStep: keyStepEnabled,
            colorize: colorizeEnabled,
            stepLimit,
            stepReady: false,
            stepPending: false,
            displayedSteps: 0,
            awaitingReplacement: false,
            pauseRequested: false,
            awaitingPause: false,
            pauseReason: undefined,
            resumeBeforeEvaluation: false,
            restartOnResume: false,
            operateAndGetNext,
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
        if (activeRequest === undefined ||
            activeRequest.pauseRequested ||
            activeRequest.awaitingPause) {
            return;
        }
        activeRequest.pauseRequested = true;
        activeRequest.stepReady = false;
        status.textContent = "Pausing…";
        worker.postMessage({
            type: "pause",
            id: activeRequest.id,
        });
        if (activeRequest.findCommand) {
            terminateWorker();
            activeRequest.restartOnResume = true;
            pauseEvaluation(activeRequest, "manual");
        }
        updateControls();
    });

    const configureDialog = (button, dialog) => {
        const closeButton = dialog.querySelector(
            ".dialog-controls button[type=\"submit\"]");

        button.addEventListener("click", () => {
            if (replacementRequest !== undefined ||
                replacementDialog.open ||
                stepLimitRequest !== undefined ||
                stepLimitDialog.open) {
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
            const dismissKey = event.key === "Escape" ||
                (event.key === "Enter" && !event.isComposing);
            if (!dismissKey || !dialog.contains(document.activeElement)) {
                return;
            }

            event.preventDefault();
            closeButton?.click();
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

        appendSourceHistory(request.source, "cancelled");
        completeEvaluationOutput(
            request, "[cancelled]", "error");
        activeRequest = undefined;
        status.textContent = "Ready";
        updateControls();
        focusSourceAfterNextPaint();
    });

    stepLimitDialog.addEventListener("cancel", event => {
        event.preventDefault();
        stepLimitDialog.close("cancel");
    });

    stepLimitDialog.addEventListener("close", () => {
        const request = stepLimitRequest;
        stepLimitRequest = undefined;
        if (request === undefined ||
            activeRequest !== request ||
            !request.awaitingPause) {
            return;
        }

        request.awaitingPause = false;
        if (stepLimitDialog.returnValue === "resume") {
            status.textContent = "Resuming…";
            if (request.resumeBeforeEvaluation) {
                request.resumeBeforeEvaluation = false;
                beginRequestEvaluation(request);
                return;
            }
            if (request.restartOnResume) {
                request.restartOnResume = false;
                startWorker(savedSetList, request);
                return;
            }
            worker.postMessage({type: "resume", id: request.id});
            updateControls();
            return;
        }

        cancelAndRestart(request);
    });

    window.addEventListener("pagehide", () => {
        clearEvaluationWatchdog(activeRequest);
        dismissStepLimitDialog();
        if (saveDownloadUrl !== undefined &&
            blobDownloadsSupported) {
            URL.revokeObjectURL(saveDownloadUrl);
        }
    }, {once: true});

    source.addEventListener("keydown", event => {
        if (event.ctrlKey && event.key.toLowerCase() === "o" &&
            !event.isComposing && !event.shiftKey &&
            !event.metaKey && !event.altKey && !source.readOnly &&
            ready && activeRequest === undefined &&
            loadRequest === undefined) {
            event.preventDefault();
            pendingOperateAndGetNext =
                inputHistory.prepareOperateAndGetNext();
            form.requestSubmit();
            return;
        }

        const previousHistory =
            (!event.ctrlKey && event.key === "ArrowUp") ||
            (event.ctrlKey && event.key.toLowerCase() === "p");
        const nextHistory =
            (!event.ctrlKey && event.key === "ArrowDown") ||
            (event.ctrlKey && event.key.toLowerCase() === "n");
        if (!event.isComposing && !event.shiftKey &&
            !event.metaKey && !event.altKey && !source.readOnly &&
            (previousHistory || nextHistory)) {
            const synchronized = inputHistory.synchronizeStorage();
            if (synchronized.changed) {
                renderSourceHistory();
            }
            if (synchronized.currentRemoved) {
                clearUntouchedHistoryRecall();
            }

            event.preventDefault();
            const recalled = previousHistory
                ? inputHistory.previous(source.value)
                : inputHistory.next();
            if (recalled !== undefined) {
                setSourceText(recalled);
                rememberUntouchedHistoryRecall();
            }
            return;
        }

        historyRecallSelectionChanged();
        const removeHistory = event.ctrlKey &&
            event.key.toLowerCase() === "d" &&
            untouchedHistoryRecall;
        if (!event.isComposing && !event.shiftKey &&
            !event.metaKey && !event.altKey && !source.readOnly &&
            removeHistory) {
            const synchronized = inputHistory.synchronizeStorage();
            if (synchronized.changed) {
                renderSourceHistory();
            }
            if (synchronized.currentRemoved) {
                clearUntouchedHistoryRecall();
                return;
            }

            const removed = inputHistory.removeCurrent();
            clearUntouchedHistoryRecall();
            renderSourceHistory();
            if (removed !== undefined) {
                event.preventDefault();
                setSourceText(removed.nextSource);
                rememberUntouchedHistoryRecall();
            }
            return;
        }

        if (event.key === "Tab" && !event.isComposing &&
            !event.shiftKey && !event.ctrlKey && !event.metaKey &&
            !event.altKey && !source.readOnly &&
            source.selectionStart === source.value.length &&
            source.selectionEnd === source.value.length) {
            const completed = completeCommand(source.value);
            if (completed !== undefined) {
                event.preventDefault();
                inputHistory.resetNavigation();
                clearUntouchedHistoryRecall();
                setSourceText(completed);
            }
            return;
        }

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

    source.addEventListener("input", () => {
        inputHistory.resetNavigation();
        clearUntouchedHistoryRecall();
        resizeSourceEditor();
    });
    source.addEventListener(
        "selectionchange", historyRecallSelectionChanged);
    source.addEventListener("keyup", historyRecallSelectionChanged);
    source.addEventListener("pointerup", historyRecallSelectionChanged);
    sourceBox.addEventListener("click", event => {
        const selection = window.getSelection?.();
        if (event.target !== source &&
            selection?.isCollapsed !== false) {
            focusSource();
        }
    });
    window.addEventListener("resize", resizeSourceEditor);

    document.addEventListener("keydown", event => {
        const request = activeRequest;
        if (event.defaultPrevented || event.isComposing || event.repeat ||
            !ready || !request?.keyStep || !request.stepReady ||
            request.stepPending || event.ctrlKey || event.metaKey ||
            event.altKey || event.key === "Tab" ||
            event.key === "Shift" ||
            helpDialog.open || combinatorInfoDialog.open ||
            aboutDialog.open || replacementDialog.open ||
            stepLimitDialog.open ||
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
    resizeSourceEditor();

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
