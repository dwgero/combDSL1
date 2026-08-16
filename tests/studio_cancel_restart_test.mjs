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
import {readFileSync} from "node:fs";
import test from "node:test";
import vm from "node:vm";

const sourceUrl = new URL("../web/app.js", import.meta.url);
const indexUrl = new URL("../web/index.html", import.meta.url);
const indexSource = readFileSync(indexUrl, "utf8");
const embeddedStyle = indexSource.match(/<style>([\s\S]*?)<\/style>/)?.[1];
assert.ok(embeddedStyle, "Studio index must contain its embedded stylesheet");

const cssDeclarationsFor = selector => {
    const declarations = new Map();
    for (const rule of embeddedStyle.matchAll(/([^{}]+)\{([^{}]*)\}/g)) {
        const selectors = rule[1].split(",").map(item => item.trim());
        if (!selectors.includes(selector)) {
            continue;
        }
        for (const item of rule[2].split(";")) {
            const separator = item.indexOf(":");
            if (separator === -1) {
                continue;
            }
            declarations.set(
                item.slice(0, separator).trim(),
                item.slice(separator + 1).trim(),
            );
        }
    }
    return declarations;
};

const dialogMarkup = id => {
    const markup = indexSource.match(new RegExp(
        `<dialog\\b[^>]*\\bid="${id}"[^>]*>[\\s\\S]*?<\\/dialog>`,
    ))?.[0];
    assert.ok(markup, `missing #${id}`);
    return markup;
};

const mergedCssDeclarationsFor = (...selectors) => {
    const declarations = new Map();
    for (const selector of selectors) {
        for (const [property, value] of cssDeclarationsFor(selector)) {
            declarations.set(property, value);
        }
    }
    return declarations;
};

const assertNoVisibleDividerEdge = (declarations, edge, description) => {
    const explicitlyInvisible = value => value !== undefined &&
        /(?:^|\s)(?:0(?:\.0+)?(?:px|rem|em)?|none|hidden|transparent)(?:\s|$)/
            .test(value);
    const border = declarations.get(`border-${edge}`) ??
        declarations.get("border");
    const borderStyle = declarations.get(`border-${edge}-style`) ??
        declarations.get("border-style");
    const borderWidth = declarations.get(`border-${edge}-width`) ??
        declarations.get("border-width");
    const borderColor = declarations.get(`border-${edge}-color`) ??
        declarations.get("border-color");
    const invisibleBorder = border === undefined
        ? borderStyle === undefined || explicitlyInvisible(borderStyle) ||
            explicitlyInvisible(borderWidth) ||
            explicitlyInvisible(borderColor)
        : explicitlyInvisible(border);
    assert.ok(
        invisibleBorder,
        `${description} must not draw a ${edge} border`,
    );
    assert.ok(
        [undefined, "none"].includes(declarations.get("box-shadow")),
        `${description} must not draw a separator with box-shadow`,
    );
    assert.ok(
        declarations.get("outline") === undefined ||
            explicitlyInvisible(declarations.get("outline")),
        `${description} must not draw a separator with an outline`,
    );
};

const evaluationWatchdogUrl = new URL(
    "../web/evaluation_watchdog.js", import.meta.url);
const evaluationWatchdogContext = vm.createContext({});
new vm.Script(readFileSync(evaluationWatchdogUrl, "utf8"), {
    filename: evaluationWatchdogUrl.pathname,
}).runInContext(evaluationWatchdogContext);
const realEvaluationWatchdog =
    evaluationWatchdogContext.combdslEvaluationWatchdog;
const inputHistoryUrl = new URL(
    "../web/input_history.js", import.meta.url);
const inputHistoryContext = vm.createContext({});
new vm.Script(readFileSync(inputHistoryUrl, "utf8"), {
    filename: inputHistoryUrl.pathname,
}).runInContext(inputHistoryContext);
const realInputHistoryTools =
    inputHistoryContext.combdslInputHistory;

const childElements = root => root.childNodes.flatMap(child => [
    ...(child instanceof FakeElement ? [child] : []),
    ...(child instanceof FakeElement ? childElements(child) : []),
]);

const assertRedNotice = (root, expected) => {
    const notice = childElements(root).find(
        element => element.textContent === expected &&
            element.dataset.kind === "error" &&
            element.style.color === "#c33",
    );
    assert.ok(notice, `missing marked notice ${expected}`);
};

class FakeEventTarget {
    constructor() {
        this.listeners = new Map();
    }

    addEventListener(type, listener) {
        const listeners = this.listeners.get(type) ?? [];
        listeners.push(listener);
        this.listeners.set(type, listeners);
    }

    dispatch(type, properties = {}) {
        const event = {
            type,
            target: this,
            defaultPrevented: false,
            preventDefault() {
                this.defaultPrevented = true;
            },
            ...properties,
        };
        for (const listener of this.listeners.get(type) ?? []) {
            listener(event);
        }
        return event;
    }
}

class FakeTextNode {
    constructor(text) {
        this.nodeType = 3;
        this.textContent = String(text);
        this.parentNode = null;
    }

    appendData(text) {
        this.textContent += String(text);
    }

    get previousSibling() {
        if (this.parentNode === null) {
            return null;
        }
        const index = this.parentNode.childNodes.indexOf(this);
        return index <= 0 ? null : this.parentNode.childNodes[index - 1];
    }
}

class FakeElement extends FakeEventTarget {
    constructor(tagName = "div") {
        super();
        this.tagName = tagName.toUpperCase();
        this.childNodes = [];
        this.dataset = {};
        this.style = {};
        this.attributes = new Map();
        this.disabled = false;
        this.readOnly = false;
        this.open = false;
        this.returnValue = "";
        this.value = "";
        this.selectionStart = 0;
        this.selectionEnd = 0;
        this.scrollHeight = 1;
        this.scrollTop = 0;
        this.tabIndex = 0;
        this.parentNode = null;
        this.ownerDocument = null;
        this._textContent = "";
        this.focusCount = 0;
    }

    append(...children) {
        if (this.childNodes.length === 0 && this._textContent !== "") {
            const existingText = new FakeTextNode(this._textContent);
            existingText.parentNode = this;
            this.childNodes.push(existingText);
            this._textContent = "";
        }
        for (const child of children) {
            const node = typeof child === "string"
                ? new FakeTextNode(child)
                : child;
            node.parentNode = this;
            this.childNodes.push(node);
        }
    }

    get textContent() {
        if (this.childNodes.length !== 0) {
            return this.childNodes
                .map(child => child.textContent)
                .join("");
        }
        return this._textContent;
    }

    set textContent(value) {
        this._textContent = String(value);
        this.childNodes = [];
    }

    get lastElementChild() {
        return [...this.childNodes].reverse().find(
            child => child instanceof FakeElement) ?? null;
    }

    get previousSibling() {
        if (this.parentNode === null) {
            return null;
        }
        const index = this.parentNode.childNodes.indexOf(this);
        return index <= 0 ? null : this.parentNode.childNodes[index - 1];
    }

    setAttribute(name, value) {
        this.attributes.set(name, String(value));
    }

    removeAttribute(name) {
        this.attributes.delete(name);
    }

    remove() {
        if (this.parentNode === null) {
            return;
        }
        const index = this.parentNode.childNodes.indexOf(this);
        if (index !== -1) {
            this.parentNode.childNodes.splice(index, 1);
        }
        this.parentNode = null;
    }

    querySelector() {
        return null;
    }

    setSelectionRange(start, end) {
        this.selectionStart = start;
        this.selectionEnd = end;
    }

    focus() {
        ++this.focusCount;
        if (this.ownerDocument !== null) {
            this.ownerDocument.activeElement = this;
        }
    }

    contains(node) {
        for (let current = node ?? null;
            current !== null;
            current = current.parentNode) {
            if (current === this) {
                return true;
            }
        }
        return false;
    }

    click() {
        this.dispatch("click");
    }

    showModal() {
        this.open = true;
    }

    close(returnValue = "") {
        this.open = false;
        this.returnValue = returnValue;
        this.dispatch("close");
    }

    getBoundingClientRect() {
        return {left: 0, right: 0, top: 0, bottom: 0};
    }
}

class FakeButtonElement extends FakeElement {
    constructor() {
        super("button");
    }
}

class FakeFormElement extends FakeElement {
    constructor() {
        super("form");
    }

    requestSubmit() {
        this.dispatch("submit");
    }
}

class FakeWorker extends FakeEventTarget {
    static instances = [];

    constructor(url) {
        super();
        this.url = String(url);
        this.messages = [];
        this.terminated = false;
        FakeWorker.instances.push(this);
    }

    postMessage(message) {
        this.messages.push(message);
    }

    terminate() {
        this.terminated = true;
    }

    send(message) {
        this.dispatch("message", {data: message});
    }
}

const createMemoryStorage = () => {
    const values = new Map();
    return Object.freeze({
        get length() {
            return values.size;
        },
        clear() {
            values.clear();
        },
        getItem(key) {
            return values.get(String(key)) ?? null;
        },
        key(index) {
            return [...values.keys()][index] ?? null;
        },
        removeItem(key) {
            values.delete(String(key));
        },
        setItem(key, value) {
            values.set(String(key), String(value));
        },
    });
};

const createHarness = ({
    historyValues = [],
    inputHistoryTools: suppliedInputHistoryTools,
    storage = createMemoryStorage(),
    watchdog,
} = {}) => {
    FakeWorker.instances = [];
    const animationFrames = [];
    const scheduledTimeouts = [];
    const clearedTimeouts = [];
    const elements = new Map();
    const buttons = new Set([
        "single-step",
        "basis-step",
        "key-step",
        "colorize",
        "cancel",
        "save",
        "load",
        "replacement-replace",
        "step-limit-cancel",
        "step-limit-resume",
        "help",
        "combinator-info",
        "about",
    ]);
    const form = new FakeFormElement();
    elements.set("evaluation-form", form);
    for (const id of [
        "source-box",
        "source-history",
        "source",
        "load-file",
        "nothing-to-save-dialog",
        "replacement-dialog",
        "replacement-message",
        "step-limit-dialog",
        "step-limit-title",
        "step-limit-message",
        "help-dialog",
        "combinator-info-dialog",
        "about-dialog",
        "status",
        "output",
        "email-box",
    ]) {
        elements.set(id, new FakeElement());
    }
    for (const id of buttons) {
        elements.set(id, new FakeButtonElement());
    }
    elements.get("cancel").textContent = "Pause";
    elements.get("step-limit-title").textContent = "Step limit reached";
    elements.get("step-limit-cancel").textContent = "Cancel";
    elements.get("step-limit-resume").textContent = "Resume";

    const informationalDialogParts = new Map();
    for (const [dialogId, titleId] of [
        ["help-dialog", "help-title"],
        ["combinator-info-dialog", "combinator-info-title"],
        ["about-dialog", "about-title"],
    ]) {
        const dialog = elements.get(dialogId);
        const scroll = new FakeElement("div");
        const title = new FakeElement("h2");
        const content = new FakeElement("a");
        const controls = new FakeFormElement();
        const close = new FakeButtonElement();
        elements.set(titleId, title);
        scroll.append(title, content);
        controls.append(close);
        dialog.append(scroll, controls);
        dialog.querySelector = selector => {
            if (selector === "[data-dialog-initial-focus]") {
                return title;
            }
            if (selector === ".dialog-controls button[type=\"submit\"]") {
                return close;
            }
            return null;
        };
        close.addEventListener("click", () => {
            const event = controls.dispatch("submit", {submitter: close});
            if (!event.defaultPrevented && dialog.open) {
                dialog.close();
            }
        });
        informationalDialogParts.set(dialogId, {
            close,
            content,
            controls,
            scroll,
            title,
        });
    }

    const document = new FakeEventTarget();
    document.baseURI = "https://example.test/combdsl/index.html";
    document.hidden = false;
    document.activeElement = null;
    document.querySelector = selector => elements.get(selector.slice(1));
    document.getElementById = id => elements.get(id);
    document.createElement = tagName => new FakeElement(tagName);
    document.createTextNode = text => new FakeTextNode(text);
    const attachToDocument = element => {
        element.ownerDocument = document;
        for (const child of element.childNodes) {
            if (child instanceof FakeElement) {
                attachToDocument(child);
            }
        }
    };
    for (const element of elements.values()) {
        attachToDocument(element);
    }

    const window = new FakeEventTarget();
    window.localStorage = storage;
    window.location = {protocol: "https:"};
    window.focus = () => {};
    const selection = {isCollapsed: true};
    window.getSelection = () => selection;

    const visibleHistory = [...historyValues];
    const inputHistory = {
        record(source, outcome = "") {
            const entry = outcome === ""
                ? source
                : `${source} [${outcome}]`;
            visibleHistory.push(entry);
            return entry;
        },
        values: () => visibleHistory,
        resetNavigation: () => {},
        previous: () => undefined,
        next: () => undefined,
        hasCurrent: () => false,
        removeCurrent: () => undefined,
        handlesStorageKey: () => false,
        synchronizeStorage: () => ({
            changed: false,
            currentRemoved: false,
        }),
        prepareOperateAndGetNext: () => undefined,
        resumeOperateAndGetNext: () => undefined,
    };
    const inputHistoryTools = suppliedInputHistoryTools ?? {
        create: () => inputHistory,
        createCommandCompleter: () => () => undefined,
    };
    const evaluationWatchdog = watchdog ?? {
        timeoutMs: 1000,
        createProgressState: () => ({sequence: 0, reductions: 0}),
        acceptProgress: () => true,
        displayedStepCount: () => 0,
        finalDisplayedStepCount: () => 0,
        stepCountMessage: () => "",
        timeoutMessage: () => "[timed out after more than 0 steps]",
    };
    class HarnessURL extends URL {
        static createObjectURL() {
            return "blob:test";
        }

        static revokeObjectURL() {}
    }

    const context = vm.createContext({
        Blob,
        clearTimeout: id => {
            clearedTimeouts.push(id);
        },
        console,
        document,
        encodeURIComponent,
        Error,
        FileReader: class {},
        globalThis: undefined,
        HTMLButtonElement: FakeButtonElement,
        Node: {TEXT_NODE: 3},
        requestAnimationFrame: callback => {
            animationFrames.push(callback);
            return animationFrames.length;
        },
        setTimeout: (callback, delay) => {
            const id = scheduledTimeouts.length + 1;
            scheduledTimeouts.push({callback, delay, id});
            return id;
        },
        URL: HarnessURL,
        window,
        Worker: FakeWorker,
    });
    context.globalThis = context;
    context.combdslEvaluationWatchdog = evaluationWatchdog;
    context.combdslInputHistory = inputHistoryTools;

    new vm.Script(readFileSync(sourceUrl, "utf8"), {
        filename: sourceUrl.pathname,
    }).runInContext(context);

    const stepLimitDialog = elements.get("step-limit-dialog");
    elements.get("step-limit-cancel").addEventListener("click", () => {
        if (stepLimitDialog.open) {
            stepLimitDialog.close("cancel");
        }
    });
    elements.get("step-limit-resume").addEventListener("click", () => {
        if (stepLimitDialog.open) {
            stepLimitDialog.close("resume");
        }
    });

    return {
        clearedTimeouts,
        dialogParts: id => informationalDialogParts.get(id),
        document,
        element: id => elements.get(id),
        flushAnimationFrames() {
            while (animationFrames.length !== 0) {
                const frame = animationFrames.shift();
                frame();
            }
        },
        scheduledTimeouts,
        pressEnter() {
            elements.get("source").dispatch("keydown", {
                key: "Enter",
                isComposing: false,
            });
        },
        pressStepKey(key = "x") {
            document.dispatch("keydown", {
                altKey: false,
                ctrlKey: false,
                isComposing: false,
                key,
                metaKey: false,
                repeat: false,
                shiftKey: false,
                target: elements.get("source"),
            });
        },
        dispatchStorage({
            key,
            newValue = key === null
                ? null
                : storage.getItem(key),
            oldValue = null,
            storageArea = storage,
        }) {
            return window.dispatch("storage", {
                key,
                newValue,
                oldValue,
                storageArea,
            });
        },
        storage,
        selection,
        workers: FakeWorker.instances,
    };
};

const createPopulatedHistoryTools = entries => {
    const history = realInputHistoryTools.create();
    for (const [source, outcome = ""] of entries) {
        history.record(source, outcome);
    }
    return Object.freeze({
        create: () => history,
        createCommandCompleter:
            realInputHistoryTools.createCommandCompleter,
    });
};

const storageKeys = storage =>
    Array.from({length: storage.length}, (_, index) => storage.key(index));

const assertSameNodes = (actual, expected, message) => {
    assert.equal(actual.length, expected.length, message);
    assert.ok(actual.every((node, index) => node === expected[index]),
        message);
};

const dispatchSourceKey = (source, key, {ctrlKey = false} = {}) =>
    source.dispatch("keydown", {
        key,
        ctrlKey,
        isComposing: false,
        shiftKey: false,
        metaKey: false,
        altKey: false,
    });

const recordAndGetStorageKey = (
    history,
    storage,
    source,
    outcome = "",
) => {
    const before = new Set(storageKeys(storage));
    history.record(source, outcome);
    const added = storageKeys(storage).filter(key => !before.has(key));
    assert.equal(added.length, 1,
        "recording one entry must add one immutable storage key");
    return added[0];
};

const createCapturedHistoryTools = capture => Object.freeze({
    ...realInputHistoryTools,
    create(options) {
        const history = realInputHistoryTools.create(options);
        capture(history);
        return history;
    },
});

const beginLimitedEvaluation = (harness, limit, expression) => {
    const source = harness.element("source");
    const worker = harness.workers[0];

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();

    source.value = `step limit ${limit}`;
    harness.pressEnter();
    harness.flushAnimationFrames();
    const limitInspection = worker.messages.filter(
        message => message.type === "inspect-definition").at(-1);
    worker.send({
        type: "definition-inspection-result",
        id: limitInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
            stepLimitCommand: true,
            stepLimitEnabled: true,
            stepLimit: limit,
        },
    });

    source.value = expression;
    harness.pressEnter();
    harness.flushAnimationFrames();
    const expressionInspection = worker.messages.filter(
        message => message.type === "inspect-definition").at(-1);
    worker.send({
        type: "definition-inspection-result",
        id: expressionInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();

    return {
        worker,
        requestId: expressionInspection.id,
        outputEntry: harness.element("output").lastElementChild,
    };
};

const beginPausableEvaluation = (
    harness,
    {expression = "BKM(BKM)", steppingMode} = {},
) => {
    const source = harness.element("source");
    const worker = harness.workers[0];
    const pause = harness.element("cancel");

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();
    assert.equal(pause.textContent, "Pause");
    assert.equal(pause.disabled, true);
    if (steppingMode !== undefined) {
        harness.element(steppingMode).click();
    }

    source.value = expression;
    harness.pressEnter();
    harness.flushAnimationFrames();
    const inspection = worker.messages.filter(
        message => message.type === "inspect-definition").at(-1);
    worker.send({
        type: "definition-inspection-result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    const evaluation = worker.messages.filter(
        message => message.type === "evaluate").at(-1);
    assert.equal(evaluation.source, expression);
    assert.equal(
        evaluation.singleStep,
        steppingMode === "single-step",
    );
    assert.equal(evaluation.keyStep, steppingMode === "key-step");
    if (steppingMode === "key-step") {
        worker.send({
            type: "step-ready",
            id: inspection.id,
            result: {success: true},
        });
    } else {
        worker.send({type: "eval-started", id: inspection.id});
    }
    assert.equal(pause.disabled, false);

    return {
        evaluation,
        outputEntry: harness.element("output").lastElementChild,
        requestId: inspection.id,
        source,
        worker,
    };
};

const beginFindEvaluation = (
    harness,
    {
        expression = "find ?xy = x(yx)",
        setList = "set userBird = 1 I\n",
    } = {},
) => {
    const source = harness.element("source");
    const worker = harness.workers[0];

    worker.send({type: "ready", setList});
    harness.flushAnimationFrames();
    source.value = expression;
    harness.pressEnter();
    harness.flushAnimationFrames();
    const inspection = worker.messages.filter(
        message => message.type === "inspect-definition").at(-1);
    worker.send({
        type: "definition-inspection-result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: true,
            showAll: false,
            find: true,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    const evaluation = worker.messages.filter(
        message => message.type === "evaluate").at(-1);
    assert.equal(evaluation.source, expression);
    assert.equal(evaluation.singleStep, false);
    assert.equal(evaluation.keyStep, false);

    return {
        expression,
        outputEntry: harness.element("output").lastElementChild,
        requestId: inspection.id,
        setList,
        worker,
    };
};

const requestPause = (harness, worker, requestId) => {
    const dialog = harness.element("step-limit-dialog");
    const pause = harness.element("cancel");
    pause.click();
    const pauseRequest = worker.messages.at(-1);
    assert.equal(pauseRequest.type, "pause");
    assert.equal(pauseRequest.id, requestId);
    assert.equal(worker.terminated, false);
    assert.equal(dialog.open, false,
        "the dialog must wait for the worker's paused acknowledgement");
    assert.equal(pause.disabled, true,
        "Pause must be disabled while its acknowledgement is pending");

    worker.send({type: "paused", id: requestId});
    assert.equal(dialog.open, true);
    assert.equal(harness.element("step-limit-title").textContent, "Paused");
    assert.equal(harness.element("step-limit-cancel").textContent, "Cancel");
    assert.equal(harness.element("step-limit-resume").textContent, "Resume");
    assert.equal(pause.disabled, true,
        "Pause must remain disabled while the evaluation is paused");
    return dialog;
};

const pauseAndCancel = (harness, worker, requestId) => {
    requestPause(harness, worker, requestId);
    harness.element("step-limit-cancel").click();
};

test("labels the active-evaluation control Pause", () => {
    assert.match(
        indexSource,
        /<button id="cancel"[^>]*>\s*Pause\s*<\/button>/,
    );
    assert.match(
        indexSource,
        /<button id="step-limit-cancel"[^>]*>\s*Cancel\s*<\/button>/,
    );
    assert.match(
        indexSource,
        /<button id="step-limit-resume"[^>]*>\s*Resume\s*<\/button>/,
    );
});

test("Help explains zero-reduction completion in both step modes", () => {
    const helpText = dialogMarkup("help-dialog")
        .replace(/<[^>]+>/g, " ")
        .replace(/\s+/g, " ");
    assert.match(
        helpText,
        /Single Step .* no available reduction, Studio still prints its canonical normal form as the result\./,
    );
    assert.match(
        helpText,
        /Key Step .* no available reduction, Studio prints its canonical normal form and completes without waiting for a keypress\./,
    );
});

test("keeps informational dialog Close buttons below the sole scroller", () => {
    const dialogIds = [
        "help-dialog",
        "combinator-info-dialog",
        "about-dialog",
    ];
    for (const id of dialogIds) {
        const closedDeclarations = cssDeclarationsFor(`#${id}`);
        assert.match(
            closedDeclarations.get("max-height") ?? "",
            /^calc\(100dvh - 2rem\)$/,
            `#${id} must remain bounded by the viewport`,
        );
        assert.equal(
            closedDeclarations.get("overflow"),
            "hidden",
            `#${id} itself must not scroll its footer`,
        );

        const openDeclarations = cssDeclarationsFor(`#${id}[open]`);
        assert.equal(openDeclarations.get("display"), "grid");
        assert.match(
            openDeclarations.get("grid-template-rows") ?? "",
            /^minmax\(0,\s*1fr\)\s+auto$/,
            `#${id} must reserve its second row for Close`,
        );

        const markup = dialogMarkup(id);
        const scrollStart = markup.search(
            /<[^>]+class="[^"]*\bdialog-scroll\b[^"]*"[^>]*>/,
        );
        const controlsStart = markup.search(
            /<form\b[^>]*class="[^"]*\bdialog-controls\b[^"]*"[^>]*>/,
        );
        assert.ok(scrollStart !== -1, `#${id} needs a content scroller`);
        assert.ok(controlsStart > scrollStart,
            `#${id} Close controls must follow the content scroller`);
        assert.match(
            markup.slice(controlsStart),
            /<button\b[^>]*type="submit"[^>]*>\s*Close\s*<\/button>/,
        );
    }

    const scrollDeclarations = cssDeclarationsFor(".dialog-scroll");
    assert.equal(scrollDeclarations.get("grid-row"), "1");
    assert.equal(scrollDeclarations.get("box-sizing"), "border-box");
    assert.equal(scrollDeclarations.get("width"), "100%");
    assert.equal(scrollDeclarations.get("min-width"), "0");
    assert.equal(scrollDeclarations.get("min-height"), "0");
    assert.equal(scrollDeclarations.get("overflow-y"), "auto");

    const footerDeclarations = mergedCssDeclarationsFor(
        ".dialog-controls",
        ".dialog-scroll + .dialog-controls",
    );
    assert.equal(footerDeclarations.get("grid-row"), "2");
    assert.equal(footerDeclarations.get("overflow"), "visible");
    assert.doesNotMatch(
        footerDeclarations.get("overflow-y") ?? "visible",
        /^(?:auto|scroll)$/,
        "the Close footer must not become another vertical scroller",
    );
    assertNoVisibleDividerEdge(
        scrollDeclarations,
        "bottom",
        "the scrolling content",
    );
    assertNoVisibleDividerEdge(
        footerDeclarations,
        "top",
        "the Close footer",
    );
});

test("focuses every informational dialog heading for keyboard scrolling", () => {
    for (const [dialogId, titleId, description] of [
        ["help-dialog", "help-title", "Help"],
        ["combinator-info-dialog", "combinator-info-title", "Bird Info"],
        ["about-dialog", "about-title", "About"],
    ]) {
        const markup = dialogMarkup(dialogId);
        const heading = markup.match(
            new RegExp(`<h2\\b[^>]*\\bid="${titleId}"[^>]*>`),
        )?.[0];
        assert.ok(heading, `${description} must retain its labelled heading`);
        assert.match(heading, /\btabindex="-1"/);
        assert.match(heading, /\bdata-dialog-initial-focus(?:\s|>)/);

        const scrollStart = markup.search(
            /<[^>]+class="[^"]*\bdialog-scroll\b[^"]*"[^>]*>/,
        );
        const headingStart = markup.indexOf(heading);
        const controlsStart = markup.search(
            /<form\b[^>]*class="[^"]*\bdialog-controls\b[^"]*"[^>]*>/,
        );
        assert.ok(scrollStart < headingStart && headingStart < controlsStart,
            `${description}'s focused heading must be inside the scrolling row`);
        const close = markup.slice(controlsStart).match(
            /<button\b[^>]*>\s*Close\s*<\/button>/,
        )?.[0];
        assert.ok(close, `${description} must retain its Close button`);
        assert.doesNotMatch(close, /\bautofocus\b/);
    }
});

test("focuses each informational heading when its dialog opens", () => {
    const harness = createHarness();
    for (const [buttonId, dialogId] of [
        ["help", "help-dialog"],
        ["combinator-info", "combinator-info-dialog"],
        ["about", "about-dialog"],
    ]) {
        const dialog = harness.element(dialogId);
        const title = harness.dialogParts(dialogId).title;

        harness.element(buttonId).click();

        assert.equal(dialog.open, true);
        assert.equal(title.focusCount, 1);
        dialog.close();
    }
});

test("Enter and Escape close informational dialogs from every dialog area", () => {
    const harness = createHarness();
    for (const [buttonId, dialogId] of [
        ["help", "help-dialog"],
        ["combinator-info", "combinator-info-dialog"],
        ["about", "about-dialog"],
    ]) {
        const button = harness.element(buttonId);
        const dialog = harness.element(dialogId);
        const parts = harness.dialogParts(dialogId);
        let closeCount = 0;
        let submitCount = 0;
        dialog.addEventListener("close", () => {
            ++closeCount;
        });
        parts.controls.addEventListener("submit", () => {
            ++submitCount;
        });

        for (const key of ["Enter", "Escape"]) {
            for (const [area, target] of [
                ["heading", parts.title],
                ["scrolling content", parts.content],
                ["scrolling row", parts.scroll],
                ["Close controls", parts.controls],
                ["Close button", parts.close],
            ]) {
                button.click();
                assert.equal(dialog.open, true);
                const closesBefore = closeCount;
                const submitsBefore = submitCount;
                target.focus();

                const event = dialog.dispatch("keydown", {
                    altKey: false,
                    ctrlKey: false,
                    isComposing: false,
                    key,
                    metaKey: false,
                    repeat: false,
                    shiftKey: false,
                    target,
                });
                if (!event.defaultPrevented && target === parts.close) {
                    parts.close.click();
                }

                assert.equal(event.defaultPrevented, true,
                    `${key} in ${dialogId}'s ${area} must suppress its default`);
                assert.equal(dialog.open, false,
                    `${key} in ${dialogId}'s ${area} must close the dialog`);
                assert.equal(closeCount, closesBefore + 1,
                    `${key} in ${dialogId}'s ${area} must close exactly once`);
                assert.equal(submitCount, submitsBefore + 1,
                    `${key} in ${dialogId}'s ${area} must submit Close exactly once`);
            }
        }
    }
});

test("other keys and focus outside do not close informational dialogs", () => {
    const harness = createHarness();
    for (const [buttonId, dialogId] of [
        ["help", "help-dialog"],
        ["combinator-info", "combinator-info-dialog"],
        ["about", "about-dialog"],
    ]) {
        const button = harness.element(buttonId);
        const dialog = harness.element(dialogId);
        const parts = harness.dialogParts(dialogId);
        button.click();
        parts.content.focus();

        for (const key of ["ArrowDown", "PageDown", "Tab", " "]) {
            const event = dialog.dispatch("keydown", {
                key,
                target: parts.content,
            });
            assert.equal(event.defaultPrevented, false,
                `${key} must keep its ordinary dialog behavior`);
            assert.equal(dialog.open, true,
                `${key} must not close ${dialogId}`);
        }

        for (const key of ["Enter", "Escape"]) {
            harness.element("source").focus();
            const event = harness.document.dispatch("keydown", {
                altKey: false,
                ctrlKey: false,
                isComposing: false,
                key,
                metaKey: false,
                repeat: false,
                shiftKey: false,
                target: harness.element("source"),
            });
            assert.equal(event.defaultPrevented, false,
                `${key} outside ${dialogId} must remain untouched`);
            assert.equal(dialog.open, true,
                `${key} outside ${dialogId} must not close it`);
        }

        dialog.close();
    }
});

test("completes revisions as a typed Studio command", () => {
    const harness = createHarness({
        inputHistoryTools: createPopulatedHistoryTools([]),
    });
    const source = harness.element("source");
    source.value = "rev";
    source.setSelectionRange(3, 3);

    const event = source.dispatch("keydown", {
        key: "Tab",
        isComposing: false,
        shiftKey: false,
        ctrlKey: false,
        metaKey: false,
        altKey: false,
    });

    assert.equal(event.defaultPrevented, true);
    assert.equal(source.value, "revisions ");
    assert.deepEqual(
        [source.selectionStart, source.selectionEnd],
        [10, 10],
    );
});

test("completes inspect as a typed Studio command", () => {
    const harness = createHarness({
        inputHistoryTools: createPopulatedHistoryTools([]),
    });
    const source = harness.element("source");
    source.value = "ins";
    source.setSelectionRange(3, 3);

    const event = dispatchSourceKey(source, "Tab");

    assert.equal(event.defaultPrevented, true);
    assert.equal(source.value, "inspect ");
    assert.deepEqual(
        [source.selectionStart, source.selectionEnd],
        [8, 8],
    );
});

test("completes compare and its required question mark in Studio", () => {
    const harness = createHarness({
        inputHistoryTools: createPopulatedHistoryTools([]),
    });
    const source = harness.element("source");
    source.value = "com";
    source.setSelectionRange(3, 3);

    const commandEvent = dispatchSourceKey(source, "Tab");

    assert.equal(commandEvent.defaultPrevented, true);
    assert.equal(source.value, "compare ");
    assert.deepEqual(
        [source.selectionStart, source.selectionEnd],
        [8, 8],
    );

    const questionEvent = dispatchSourceKey(source, "Tab");

    assert.equal(questionEvent.defaultPrevented, true);
    assert.equal(source.value, "compare ?");
    assert.deepEqual(
        [source.selectionStart, source.selectionEnd],
        [9, 9],
    );
});

test("completes abstract ministeps as a typed Studio command", () => {
    const harness = createHarness({
        inputHistoryTools: createPopulatedHistoryTools([]),
    });
    const source = harness.element("source");
    source.value = "abstract m";
    source.setSelectionRange(10, 10);

    const event = source.dispatch("keydown", {
        key: "Tab",
        isComposing: false,
        shiftKey: false,
        ctrlKey: false,
        metaKey: false,
        altKey: false,
    });

    assert.equal(event.defaultPrevented, true);
    assert.equal(source.value, "abstract ministeps ?");
    assert.deepEqual(
        [source.selectionStart, source.selectionEnd],
        [20, 20],
    );
});

test("completes restricted Find catalog keywords in Studio", () => {
    for (const [sourceText, completed] of [
        ["find am", "find among "],
        ["find all am", "find all among "],
    ]) {
        const harness = createHarness({
            inputHistoryTools: createPopulatedHistoryTools([]),
        });
        const source = harness.element("source");
        source.value = sourceText;
        source.setSelectionRange(sourceText.length, sourceText.length);

        const event = dispatchSourceKey(source, "Tab");

        assert.equal(event.defaultPrevented, true);
        assert.equal(source.value, completed);
        assert.deepEqual(
            [source.selectionStart, source.selectionEnd],
            [completed.length, completed.length],
        );
    }

    const harness = createHarness({
        inputHistoryTools: createPopulatedHistoryTools([]),
    });
    const source = harness.element("source");
    source.value = "find a";
    source.setSelectionRange(6, 6);
    const ambiguous = dispatchSourceKey(source, "Tab");
    assert.equal(ambiguous.defaultPrevented, false);
    assert.equal(source.value, "find a");
});

test("completes all forms of transitive dependency commands", () => {
    for (const [sourceText, completed] of [
        ["dependson a", "dependson all "],
        ["depends-on a", "depends-on all "],
        ["depends on a", "depends on all "],
        ["usedby a", "usedby all "],
        ["used-by a", "used-by all "],
        ["used by a", "used by all "],
    ]) {
        const harness = createHarness({
            inputHistoryTools: createPopulatedHistoryTools([]),
        });
        const source = harness.element("source");
        source.value = sourceText;
        source.setSelectionRange(sourceText.length, sourceText.length);

        const event = dispatchSourceKey(source, "Tab");

        assert.equal(event.defaultPrevented, true);
        assert.equal(source.value, completed);
        assert.deepEqual(
            [source.selectionStart, source.selectionEnd],
            [completed.length, completed.length],
        );
    }
});

test("completes every Studio dependency-path alias", () => {
    for (const [sourceText, completed] of [
        ["usedby p", "usedby path "],
        ["used-by p", "used-by path "],
        ["used by p", "used by path "],
        ["usedby path b", "usedby path between "],
        ["used-by path b", "used-by path between "],
        ["used by path b", "used by path between "],
        ["usedby path A a", "usedby path A and "],
        [
            "used-by path between A a",
            "used-by path between A and ",
        ],
        ["used by path A ", "used by path A and "],
    ]) {
        const harness = createHarness({
            inputHistoryTools: createPopulatedHistoryTools([]),
        });
        const source = harness.element("source");
        source.value = sourceText;
        source.setSelectionRange(sourceText.length, sourceText.length);

        const event = dispatchSourceKey(source, "Tab");

        assert.equal(event.defaultPrevented, true);
        assert.equal(source.value, completed);
        assert.deepEqual(
            [source.selectionStart, source.selectionEnd],
            [completed.length, completed.length],
        );
    }
});

test("renders zero-reduction Single Step and Key Step immediately", () => {
    for (const [steppingMode, expression, basisStep, colorize] of [
        ["single-step", "Cstar xy", false, false],
        ["key-step", "C*xy", true, true],
    ]) {
        const harness = createHarness();
        const source = harness.element("source");
        const worker = harness.workers[0];

        worker.send({type: "ready", setList: ""});
        harness.flushAnimationFrames();
        harness.element(steppingMode).click();
        if (basisStep) {
            harness.element("basis-step").click();
        }
        if (colorize) {
            harness.element("colorize").click();
        }

        source.value = expression;
        harness.pressEnter();
        harness.flushAnimationFrames();
        const inspection = worker.messages.find(
            message => message.type === "inspect-definition");
        worker.send({
            type: "definition-inspection-result",
            id: inspection.id,
            result: {
                success: true,
                definition: false,
                displayOnly: false,
                showAll: false,
                find: false,
                replacement: "",
            },
        });
        harness.flushAnimationFrames();

        const evaluation = worker.messages.find(
            message => message.type === "evaluate");
        assert.equal(evaluation.source, expression);
        assert.equal(
            evaluation.singleStep,
            steppingMode === "single-step",
        );
        assert.equal(evaluation.keyStep, steppingMode === "key-step");
        assert.equal(evaluation.basisStep, basisStep);
        assert.equal(evaluation.colorize, colorize);
        if (steppingMode === "single-step") {
            worker.send({type: "eval-started", id: inspection.id});
        }
        assert.equal(
            worker.messages.some(message => message.type === "step"),
            false,
        );

        worker.send({
            type: "result",
            id: inspection.id,
            html: false,
            result: {
                success: true,
                definition: false,
                recoverWorker: false,
                output: `${expression}\n`,
                error: "",
                reductions: 0,
                limitReached: false,
            },
        });

        const output = harness.element("output");
        assert.equal(output.lastElementChild.textContent,
            `${expression}\n${expression}`);
        assert.equal(
            output.childNodes.filter(
                child => child instanceof FakeElement).length,
            1,
            "a normal form must finish its existing Results entry",
        );
        assert.equal(source.value, "");
        assert.equal(
            harness.element("source-history").textContent,
            expression,
        );
        assert.equal(harness.element("status").textContent, "Ready");
        assert.equal(
            worker.messages.some(message => message.type === "step"),
            false,
        );
    }
});

for (const [description, steppingMode] of [
    ["ordinary evaluation", undefined],
    ["automatic Single Step", "single-step"],
]) {
    test(`pauses and resumes the same ${description}`, () => {
        const harness = createHarness();
        const {outputEntry, requestId, source, worker} =
            beginPausableEvaluation(harness, {steppingMode});

        requestPause(harness, worker, requestId);
        assert.strictEqual(
            harness.element("output").lastElementChild,
            outputEntry,
        );
        harness.element("step-limit-resume").click();

        const resume = worker.messages.at(-1);
        assert.equal(resume.type, "resume");
        assert.equal(resume.id, requestId);
        assert.equal(worker.terminated, false);
        assert.equal(harness.element("step-limit-dialog").open, false);
        assert.strictEqual(
            harness.element("output").lastElementChild,
            outputEntry,
        );
        assert.equal(
            childElements(outputEntry).some(
                element => element.dataset.kind === "error"),
            false,
            "Resume must not add a cancellation notice",
        );

        worker.send({type: "eval-started", id: requestId});
        worker.send({
            type: "result",
            id: requestId,
            result: {
                success: true,
                definition: false,
                recoverWorker: false,
                output: "normal form\n",
                error: "",
                reductions: 42,
                limitReached: false,
            },
        });
        assert.strictEqual(
            harness.element("output").lastElementChild,
            outputEntry,
            "Resume must finish the preserved Results entry",
        );
        assert.equal(outputEntry.textContent, "BKM(BKM)\nnormal form");
        assert.equal(source.readOnly, false);
    });
}

test("Pause dialog Cancel cancels the preserved evaluation", () => {
    const harness = createHarness();
    const {outputEntry, requestId, worker} = beginPausableEvaluation(harness);

    pauseAndCancel(harness, worker, requestId);

    assert.equal(worker.terminated, true);
    assert.equal(harness.workers.length, 2);
    assert.strictEqual(harness.element("output").lastElementChild, outputEntry);
    assert.equal(outputEntry.textContent, "BKM(BKM)\n[cancelled]");
    assert.equal(
        outputEntry.textContent.split("[cancelled]").length - 1,
        1,
        "Cancel must add the cancellation text exactly once",
    );
    assertRedNotice(outputEntry, "[cancelled]");
});

test("pauses and resumes Key Step while it awaits a key", () => {
    const harness = createHarness();
    const {outputEntry, requestId, worker} = beginPausableEvaluation(
        harness,
        {expression: "IIx", steppingMode: "key-step"},
    );

    requestPause(harness, worker, requestId);
    harness.element("step-limit-resume").click();
    const resume = worker.messages.at(-1);
    assert.equal(resume.type, "resume");
    assert.equal(resume.id, requestId);
    assert.equal(worker.terminated, false);

    worker.send({
        type: "step-ready",
        id: requestId,
        result: {success: true},
    });
    harness.pressStepKey();
    const step = worker.messages.at(-1);
    assert.equal(step.type, "step");
    assert.equal(step.id, requestId);
    worker.send({
        type: "step-result",
        id: requestId,
        result: {
            success: true,
            reduced: true,
            complete: true,
            definition: false,
            output: "x\n",
            error: "",
            limitReached: false,
        },
    });

    assert.strictEqual(harness.element("output").lastElementChild, outputEntry);
    assert.equal(outputEntry.textContent, "IIx\nx");
});

test("keeps selected history text while blank clicks focus input", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const sourceBox = harness.element("source-box");
    const sourceHistory = harness.element("source-history");

    source.value = "draft";
    source.setSelectionRange(0, 0);
    harness.selection.isCollapsed = false;
    sourceBox.dispatch("click", {target: sourceHistory});
    assert.deepEqual(
        [source.selectionStart, source.selectionEnd],
        [0, 0],
    );

    harness.selection.isCollapsed = true;
    sourceBox.dispatch("click", {target: sourceHistory});
    assert.deepEqual(
        [source.selectionStart, source.selectionEnd],
        [source.value.length, source.value.length],
    );
});

test("Ctrl-D removes an untouched recalled history item", () => {
    const harness = createHarness({
        inputHistoryTools: createPopulatedHistoryTools([
            ["A"],
            ["B", "cancelled"],
            ["C"],
        ]),
    });
    const source = harness.element("source");
    const displayedHistory = harness.element("source-history");
    source.value = "draft";

    source.dispatch("keydown", {
        key: "ArrowUp",
        ctrlKey: false,
        isComposing: false,
        shiftKey: false,
        metaKey: false,
        altKey: false,
    });
    source.dispatch("keydown", {
        key: "ArrowUp",
        ctrlKey: false,
        isComposing: false,
        shiftKey: false,
        metaKey: false,
        altKey: false,
    });
    assert.equal(source.value, "B");
    source.dispatch("selectionchange");

    const removal = source.dispatch("keydown", {
        key: "d",
        ctrlKey: true,
        isComposing: false,
        shiftKey: false,
        metaKey: false,
        altKey: false,
    });

    assert.equal(removal.defaultPrevented, true);
    assert.equal(source.value, "C");
    assert.deepEqual(
        [source.selectionStart, source.selectionEnd],
        [1, 1],
    );
    assert.deepEqual(
        displayedHistory.childNodes.map(row => row.textContent),
        ["A", "C"],
    );

    const secondRemoval = source.dispatch("keydown", {
        key: "d",
        ctrlKey: true,
        isComposing: false,
        shiftKey: false,
        metaKey: false,
        altKey: false,
    });
    assert.equal(secondRemoval.defaultPrevented, true,
        "the untouched replacement entry must remain removable");
    assert.equal(source.value, "draft");
    assert.deepEqual(
        displayedHistory.childNodes.map(row => row.textContent),
        ["A"],
    );
});

for (const [description, key, ctrlKey] of [
    ["Backspace", "Backspace", false],
    ["Ctrl-H", "h", true],
]) {
    test(`${description} edits rather than removes a recalled history item`,
        () => {
            const harness = createHarness({
                inputHistoryTools: createPopulatedHistoryTools([
                    ["ABC"],
                ]),
            });
            const source = harness.element("source");
            const displayedHistory = harness.element("source-history");
            const originalRow = displayedHistory.childNodes[0];
            source.value = "draft";

            source.dispatch("keydown", {
                key: "ArrowUp",
                ctrlKey: false,
                isComposing: false,
                shiftKey: false,
                metaKey: false,
                altKey: false,
            });
            assert.equal(source.value, "ABC");

            const editing = source.dispatch("keydown", {
                key,
                ctrlKey,
                isComposing: false,
                shiftKey: false,
                metaKey: false,
                altKey: false,
            });

            assert.equal(editing.defaultPrevented, false,
                `${description} must retain its native editing behavior`);
            assert.deepEqual(
                displayedHistory.childNodes.map(row => row.textContent),
                ["ABC"],
            );
            assert.strictEqual(displayedHistory.childNodes[0], originalRow);
        });
}

test("Ctrl-D stays in editing mode after the caret moves away and back", () => {
    const harness = createHarness({
        inputHistoryTools: createPopulatedHistoryTools([
            ["A"],
            ["ABC"],
        ]),
    });
    const source = harness.element("source");
    const displayedHistory = harness.element("source-history");
    const originalRow = displayedHistory.childNodes[1];
    source.value = "draft";

    source.dispatch("keydown", {
        key: "ArrowUp",
        ctrlKey: false,
        isComposing: false,
        shiftKey: false,
        metaKey: false,
        altKey: false,
    });
    assert.equal(source.value, "ABC");

    source.setSelectionRange(2, 2);
    source.dispatch("selectionchange");
    source.setSelectionRange(3, 3);
    source.dispatch("selectionchange");

    const editing = source.dispatch("keydown", {
        key: "d",
        ctrlKey: true,
        isComposing: false,
        shiftKey: false,
        metaKey: false,
        altKey: false,
    });

    assert.equal(editing.defaultPrevented, false,
        "Ctrl-D must retain forward-delete behavior after cursor movement");
    assert.deepEqual(
        displayedHistory.childNodes.map(row => row.textContent),
        ["A", "ABC"],
    );
    assert.strictEqual(displayedHistory.childNodes[1], originalRow);

    source.dispatch("keydown", {
        key: "ArrowUp",
        ctrlKey: false,
        isComposing: false,
        shiftKey: false,
        metaKey: false,
        altKey: false,
    });
    assert.equal(source.value, "A");

    const removal = source.dispatch("keydown", {
        key: "d",
        ctrlKey: true,
        isComposing: false,
        shiftKey: false,
        metaKey: false,
        altKey: false,
    });
    assert.equal(removal.defaultPrevented, true,
        "recalling another item must re-arm Ctrl-D history removal");
    assert.deepEqual(
        displayedHistory.childNodes.map(row => row.textContent),
        ["ABC"],
    );
    assert.notStrictEqual(displayedHistory.childNodes[0], originalRow,
        "a successful removal rerenders the merged history snapshot");
});

test("history-removal keys retain their normal behavior on the live draft", () => {
    const harness = createHarness({
        inputHistoryTools: createPopulatedHistoryTools([["A"]]),
    });
    const source = harness.element("source");
    const displayedHistory = harness.element("source-history");
    source.value = "draft";

    for (const [key, ctrlKey] of [
        ["Backspace", false],
        ["h", true],
        ["d", true],
    ]) {
        const event = source.dispatch("keydown", {
            key,
            ctrlKey,
            isComposing: false,
            shiftKey: false,
            metaKey: false,
            altKey: false,
        });
        assert.equal(event.defaultPrevented, false);
        assert.equal(source.value, "draft");
    }
    assert.deepEqual(
        displayedHistory.childNodes.map(row => row.textContent),
        ["A"],
    );
});

test("synchronizes concurrent history changes between Studio tabs", () => {
    const storage = createMemoryStorage();
    const otherHistory = realInputHistoryTools.create({storage});
    let studioHistory;
    const harness = createHarness({
        storage,
        inputHistoryTools: createCapturedHistoryTools(history => {
            studioHistory = history;
        }),
    });
    const displayedHistory = harness.element("source-history");

    const studioKey = recordAndGetStorageKey(
        studioHistory, storage, "Ix");
    const otherKey = recordAndGetStorageKey(
        otherHistory, storage, "YI", "cancelled");
    assert.notEqual(studioKey, otherKey,
        "independent tabs must store additions under distinct keys");

    const unrelatedStorage = createMemoryStorage();
    harness.dispatchStorage({
        key: otherKey,
        storageArea: unrelatedStorage,
    });
    storage.setItem("unrelated", "value");
    harness.dispatchStorage({key: "unrelated"});
    assert.equal(displayedHistory.textContent, "",
        "unrelated storage events must not change the visible history");

    harness.dispatchStorage({key: otherKey});
    assert.deepEqual(
        displayedHistory.childNodes.map(row => row.textContent),
        [...studioHistory.values()],
    );
    assert.deepEqual(new Set(studioHistory.values()), new Set([
        "Ix",
        "YI [cancelled]",
    ]));
    assertRedNotice(displayedHistory, "[cancelled]");

    const synchronizedRows = [...displayedHistory.childNodes];
    harness.dispatchStorage({key: otherKey});
    assertSameNodes(displayedHistory.childNodes, synchronizedRows,
        "an already-applied storage event must not rerender history");

    const staleHistory = realInputHistoryTools.create({storage});
    assert.equal(otherHistory.previous(""), "YI");
    const keysBeforeRemoval = new Set(storageKeys(storage));
    assert.ok(otherHistory.removeCurrent());
    const removedKeys = [...keysBeforeRemoval].filter(
        key => !storageKeys(storage).includes(key));
    assert.deepEqual(removedKeys, [otherKey]);
    harness.dispatchStorage({key: otherKey, newValue: null});
    assert.deepEqual([...studioHistory.values()], ["Ix"]);
    assert.equal(displayedHistory.textContent, "Ix");

    const staleKey = recordAndGetStorageKey(
        staleHistory, storage, "Kx");
    harness.dispatchStorage({key: staleKey});
    assert.deepEqual(new Set(studioHistory.values()), new Set([
        "Ix",
        "Kx",
    ]));
    assert.equal(studioHistory.values().includes("YI [cancelled]"), false,
        "a stale tab must not resurrect a remotely removed entry");

    storage.clear();
    harness.dispatchStorage({key: null, newValue: null});
    assert.deepEqual([...studioHistory.values()], []);
    assert.equal(displayedHistory.textContent, "");
});

test("preserves recalled text and draft across remote history changes", () => {
    const storage = createMemoryStorage();
    const seedHistory = realInputHistoryTools.create({storage});
    recordAndGetStorageKey(seedHistory, storage, "A");
    const selectedKey = recordAndGetStorageKey(
        seedHistory, storage, "B");
    let studioHistory;
    const harness = createHarness({
        storage,
        inputHistoryTools: createCapturedHistoryTools(history => {
            studioHistory = history;
        }),
    });
    const otherHistory = realInputHistoryTools.create({storage});
    const source = harness.element("source");
    const displayedHistory = harness.element("source-history");
    source.value = "draft";

    dispatchSourceKey(source, "ArrowUp");
    assert.equal(source.value, "B");
    const recalledSelection = [
        source.selectionStart,
        source.selectionEnd,
    ];

    const addedKey = recordAndGetStorageKey(
        otherHistory, storage, "C");
    harness.dispatchStorage({key: addedKey});
    assert.equal(source.value, "B");
    assert.deepEqual([
        source.selectionStart,
        source.selectionEnd,
    ], recalledSelection);
    assert.deepEqual([...studioHistory.values()], ["A", "B", "C"]);

    dispatchSourceKey(source, "ArrowDown");
    assert.equal(source.value, "C",
        "the remote addition must become the next newer entry");
    dispatchSourceKey(source, "ArrowUp");
    assert.equal(source.value, "B");

    assert.equal(otherHistory.previous(""), "C");
    assert.equal(otherHistory.previous(""), "B");
    assert.ok(otherHistory.removeCurrent());
    harness.dispatchStorage({key: selectedKey, newValue: null});
    assert.deepEqual(
        displayedHistory.childNodes.map(row => row.textContent),
        ["A", "C"],
    );
    assert.equal(source.value, "B",
        "removing the recalled entry remotely must preserve its editor text");
    assert.deepEqual([
        source.selectionStart,
        source.selectionEnd,
    ], recalledSelection);

    const forwardDelete = dispatchSourceKey(
        source, "d", {ctrlKey: true});
    assert.equal(forwardDelete.defaultPrevented, false,
        "Ctrl-D must not delete a neighboring item after remote removal");
    assert.deepEqual([...studioHistory.values()], ["A", "C"]);

    dispatchSourceKey(source, "ArrowUp");
    assert.equal(source.value, "A",
        "Up must continue from the removed entry's ordering anchor");
    dispatchSourceKey(source, "ArrowDown");
    assert.equal(source.value, "C");
    dispatchSourceKey(source, "ArrowDown");
    assert.equal(source.value, "draft",
        "the original live draft must survive remote changes");
});

test("history navigation renders a remote add before its storage event", () => {
    const storage = createMemoryStorage();
    const otherHistory = realInputHistoryTools.create({storage});
    let studioHistory;
    const harness = createHarness({
        storage,
        inputHistoryTools: createCapturedHistoryTools(history => {
            studioHistory = history;
        }),
    });
    const source = harness.element("source");
    const displayedHistory = harness.element("source-history");
    source.value = "draft";
    const remoteKey = recordAndGetStorageKey(
        otherHistory, storage, "Remote");

    dispatchSourceKey(source, "ArrowUp");
    assert.equal(source.value, "Remote");
    assert.deepEqual([...studioHistory.values()], ["Remote"]);
    assert.equal(displayedHistory.textContent, "Remote");

    const renderedRow = displayedHistory.childNodes[0];
    harness.dispatchStorage({key: remoteKey});
    assert.strictEqual(displayedHistory.childNodes[0], renderedRow,
        "the late event must not rerender an already-imported entry");
});

test("local duplicate record renders history imported before its event", () => {
    const storage = createMemoryStorage();
    const otherHistory = realInputHistoryTools.create({storage});
    let studioHistory;
    const harness = createHarness({
        storage,
        inputHistoryTools: createCapturedHistoryTools(history => {
            studioHistory = history;
        }),
    });
    const source = harness.element("source");
    const displayedHistory = harness.element("source-history");
    const worker = harness.workers[0];
    const remoteKey = recordAndGetStorageKey(
        otherHistory, storage, "Ix");
    const storedKeys = storageKeys(storage);

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();
    source.value = "Ix";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const inspection = worker.messages.find(
        message => message.type === "inspect-definition");
    worker.send({
        type: "definition-inspection-result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    worker.send({
        type: "result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "x\n",
            error: "",
            reductions: 1,
            limitReached: false,
        },
    });

    assert.deepEqual(
        displayedHistory.childNodes.map(row => row.textContent),
        [...studioHistory.values()],
    );
    assert.deepEqual([...studioHistory.values()], ["Ix"]);
    assert.deepEqual(storageKeys(storage), storedKeys,
        "the adjacent duplicate must not add another storage entry");
    const renderedRows = [...displayedHistory.childNodes];
    harness.dispatchStorage({key: remoteKey});
    assertSameNodes(displayedHistory.childNodes, renderedRows,
        "the late event must not rerender an already-imported entry");
});

test("local removal rerenders history imported before a late storage event", () => {
    const storage = createMemoryStorage();
    const seedHistory = realInputHistoryTools.create({storage});
    recordAndGetStorageKey(seedHistory, storage, "A");
    recordAndGetStorageKey(seedHistory, storage, "B");
    let studioHistory;
    const harness = createHarness({
        storage,
        inputHistoryTools: createCapturedHistoryTools(history => {
            studioHistory = history;
        }),
    });
    const otherHistory = realInputHistoryTools.create({storage});
    const source = harness.element("source");
    const displayedHistory = harness.element("source-history");
    source.value = "draft";

    dispatchSourceKey(source, "ArrowUp");
    assert.equal(source.value, "B");
    const remoteKey = recordAndGetStorageKey(
        otherHistory, storage, "C");

    const removal = dispatchSourceKey(source, "d", {ctrlKey: true});
    assert.equal(removal.defaultPrevented, true);
    assert.equal(source.value, "C");
    assert.deepEqual([...studioHistory.values()], ["A", "C"]);
    assert.deepEqual(
        displayedHistory.childNodes.map(row => row.textContent),
        ["A", "C"],
    );

    const renderedRows = [...displayedHistory.childNodes];
    harness.dispatchStorage({key: remoteKey});
    assertSameNodes(displayedHistory.childNodes, renderedRows,
        "the late event must not rerender an already-imported entry");
});

test("Ctrl-D safely discovers remote removal before its storage event", () => {
    const storage = createMemoryStorage();
    const seedHistory = realInputHistoryTools.create({storage});
    recordAndGetStorageKey(seedHistory, storage, "A");
    const removedKey = recordAndGetStorageKey(
        seedHistory, storage, "B");
    let studioHistory;
    const harness = createHarness({
        storage,
        inputHistoryTools: createCapturedHistoryTools(history => {
            studioHistory = history;
        }),
    });
    const otherHistory = realInputHistoryTools.create({storage});
    const source = harness.element("source");
    const displayedHistory = harness.element("source-history");
    source.value = "draft";

    dispatchSourceKey(source, "ArrowUp");
    assert.equal(source.value, "B");
    assert.equal(otherHistory.previous(""), "B");
    assert.ok(otherHistory.removeCurrent());

    const forwardDelete = dispatchSourceKey(
        source, "d", {ctrlKey: true});
    assert.equal(forwardDelete.defaultPrevented, false);
    assert.equal(source.value, "B");
    assert.deepEqual([...studioHistory.values()], ["A"]);
    assert.equal(displayedHistory.textContent, "A");

    const renderedRow = displayedHistory.childNodes[0];
    harness.dispatchStorage({key: removedKey, newValue: null});
    assert.strictEqual(displayedHistory.childNodes[0], renderedRow,
        "the late event must not rerender an already-imported removal");
});

test("does not display an adjacent duplicate history entry", () => {
    const harness = createHarness({
        inputHistoryTools: createPopulatedHistoryTools([["Ix"]]),
    });
    const source = harness.element("source");
    const displayedHistory = harness.element("source-history");
    const worker = harness.workers[0];

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();
    source.value = "Ix";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const inspection = worker.messages.find(
        message => message.type === "inspect-definition");
    worker.send({
        type: "definition-inspection-result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    worker.send({
        type: "result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "x\n",
            error: "",
            reductions: 1,
            limitReached: false,
        },
    });

    assert.equal(displayedHistory.childNodes.length, 1);
    assert.equal(displayedHistory.textContent, "Ix");
});

test("replays a submission after cancellation and definition restore", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const firstWorker = harness.workers[0];
    const savedSetList = "set userBird = 1 I\n";

    assert.ok(firstWorker);
    firstWorker.send({type: "ready", setList: savedSetList});
    harness.flushAnimationFrames();

    source.value = "MM";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const firstInspection = firstWorker.messages.find(
        message => message.type === "inspect-definition");
    assert.equal(firstInspection?.source, "MM");

    firstWorker.send({
        type: "definition-inspection-result",
        id: firstInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    assert.equal(
        firstWorker.messages.filter(
            message => message.type === "evaluate").length,
        1,
    );
    firstWorker.send({type: "eval-started", id: firstInspection.id});

    pauseAndCancel(harness, firstWorker, firstInspection.id);
    assert.equal(firstWorker.terminated, true);
    assert.equal(harness.workers.length, 2);
    assertRedNotice(harness.element("output"), "[cancelled]");
    assertRedNotice(harness.element("source-history"), "[cancelled]");
    const replacementWorker = harness.workers[1];

    source.value = "show B";
    harness.pressEnter();
    harness.flushAnimationFrames();
    assert.deepEqual(replacementWorker.messages, []);

    replacementWorker.send({type: "ready", setList: ""});
    const restore = replacementWorker.messages.find(
        message => message.type === "load");
    assert.equal(restore?.source, savedSetList);
    assert.equal(
        replacementWorker.messages.filter(
            message => message.type === "inspect-definition").length,
        0,
    );

    replacementWorker.send({
        type: "load-result",
        id: restore.id,
        setList: savedSetList,
        result: {success: true},
    });
    assert.equal(
        replacementWorker.messages.filter(
            message => message.type === "inspect-definition").length,
        0,
    );

    harness.flushAnimationFrames();
    harness.flushAnimationFrames();
    const inspections = replacementWorker.messages.filter(
        message => message.type === "inspect-definition");
    assert.equal(inspections.length, 1);
    assert.equal(inspections[0].source, "show B");
});

test("marks detailed timeout notices red in results and history", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const firstWorker = harness.workers[0];

    firstWorker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();

    source.value = "MM";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const inspection = firstWorker.messages.find(
        message => message.type === "inspect-definition");
    firstWorker.send({
        type: "definition-inspection-result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            replacement: "",
        },
    });
    firstWorker.send({type: "eval-started", id: inspection.id});
    firstWorker.send({
        type: "result",
        id: inspection.id,
        result: {recoverWorker: true},
    });

    assert.equal(firstWorker.terminated, true);
    assertRedNotice(
        harness.element("output"),
        "[timed out after more than 0 steps]",
    );
    assertRedNotice(
        harness.element("source-history"),
        "[timed out]",
    );
});

test("routes abstract trace modes as display-only with modes enabled", () => {
    for (const command of [
        "abstract steps ?xy = x(yx)",
        "abstract ministeps ?xy = y(xy)",
    ]) {
        for (const steppingMode of ["single-step", "key-step"]) {
            const harness = createHarness();
            const source = harness.element("source");
            const worker = harness.workers[0];

            worker.send({type: "ready", setList: ""});
            harness.flushAnimationFrames();
            harness.element(steppingMode).click();
            harness.element("basis-step").click();
            harness.element("colorize").click();

            source.value = command;
            harness.pressEnter();
            harness.flushAnimationFrames();
            const inspection = worker.messages.find(
                message => message.type === "inspect-definition");
            worker.send({
                type: "definition-inspection-result",
                id: inspection.id,
                result: {
                    success: true,
                    definition: false,
                    displayOnly: true,
                    showAll: false,
                    find: false,
                    replacement: "",
                },
            });
            harness.flushAnimationFrames();

            const evaluation = worker.messages.find(
                message => message.type === "evaluate");
            assert.equal(evaluation.source, command);
            assert.equal(evaluation.singleStep, false);
            assert.equal(evaluation.keyStep, false);
            assert.equal(evaluation.basisStep, false);
            assert.equal(evaluation.colorize, false);
        }
    }
});

test("routes revisions as compact display-only output", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const worker = harness.workers[0];
    const command = "revisions StudioBird";
    const revisions =
        "StudioBird@1 arity:1 I [captured]\n" +
        "StudioBird@2 arity:1 K [live] [current]\n";

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();
    harness.element("single-step").click();
    harness.element("basis-step").click();
    harness.element("colorize").click();

    source.value = command;
    harness.pressEnter();
    harness.flushAnimationFrames();
    const inspection = worker.messages.find(
        message => message.type === "inspect-definition");
    worker.send({
        type: "definition-inspection-result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: true,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();

    const evaluation = worker.messages.find(
        message => message.type === "evaluate");
    assert.equal(evaluation.source, command);
    assert.equal(evaluation.singleStep, false);
    assert.equal(evaluation.keyStep, false);
    assert.equal(evaluation.basisStep, false);
    assert.equal(evaluation.colorize, false);

    worker.send({
        type: "result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: revisions,
            error: "",
            reductions: 0,
        },
    });

    const outputEntry = harness.element("output").lastElementChild;
    assert.equal(
        outputEntry.textContent,
        `${command}\n${revisions.trimEnd()}`,
    );
    assert.equal(outputEntry.dataset.compactAfter, "true");
    assert.equal(outputEntry.textContent.includes("[show end]"), false);
    assert.equal(harness.element("source-history").textContent, command);
});

test("routes inspect as compact display-only output", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const worker = harness.workers[0];
    const command = "inspect S(Kx)(Iy)z";
    const report =
        "free symbols: x y z\n" +
        "references:\n" +
        "  S [fundamental]\n" +
        "  K [fundamental]\n" +
        "  I [fundamental]\n" +
        "next reduction: S(Kx)(Iy)z [S at root]\n";

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();
    harness.element("single-step").click();
    harness.element("basis-step").click();
    harness.element("key-step").click();
    harness.element("colorize").click();

    source.value = command;
    harness.pressEnter();
    harness.flushAnimationFrames();
    const inspection = worker.messages.find(
        message => message.type === "inspect-definition");
    assert.equal(inspection.source, command);
    worker.send({
        type: "definition-inspection-result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: true,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();

    const evaluation = worker.messages.find(
        message => message.type === "evaluate");
    assert.equal(evaluation.source, command);
    assert.equal(evaluation.singleStep, false);
    assert.equal(evaluation.keyStep, false);
    assert.equal(evaluation.basisStep, false);
    assert.equal(evaluation.colorize, false);

    worker.send({
        type: "result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: report,
            error: "",
            reductions: 0,
        },
    });

    const outputEntry = harness.element("output").lastElementChild;
    assert.equal(
        outputEntry.textContent,
        `${command}\n${report.trimEnd()}`,
    );
    assert.equal(outputEntry.dataset.compactAfter, "true");
    assert.equal(outputEntry.textContent.includes("canonical:"), false);
    assert.equal(outputEntry.textContent.includes("tree:"), false);
    assert.equal(outputEntry.textContent.includes("[show end]"), false);
    assert.equal(harness.element("source-history").textContent, command);
});

test("routes compare result forms as compact display-only output", () => {
    const cases = [
        ["compare ?x I = SKK", "both reduce to: x\n"],
        [
            "compare ?xy K = KI",
            "left reduces to: x\nright reduces to: y\n",
        ],
        ["compare ?x YI = I", "inconclusive\n"],
    ];

    for (const [command, report] of cases) {
        const harness = createHarness();
        const source = harness.element("source");
        const worker = harness.workers[0];

        worker.send({type: "ready", setList: ""});
        harness.flushAnimationFrames();
        harness.element("single-step").click();
        harness.element("basis-step").click();
        harness.element("key-step").click();
        harness.element("colorize").click();

        source.value = command;
        harness.pressEnter();
        harness.flushAnimationFrames();
        const inspection = worker.messages.find(
            message => message.type === "inspect-definition");
        assert.equal(inspection.source, command);
        worker.send({
            type: "definition-inspection-result",
            id: inspection.id,
            result: {
                success: true,
                definition: false,
                displayOnly: true,
                showAll: false,
                find: false,
                replacement: "",
            },
        });
        harness.flushAnimationFrames();

        const evaluation = worker.messages.find(
            message => message.type === "evaluate");
        assert.equal(evaluation.source, command);
        assert.equal(evaluation.singleStep, false);
        assert.equal(evaluation.keyStep, false);
        assert.equal(evaluation.basisStep, false);
        assert.equal(evaluation.colorize, false);

        worker.send({
            type: "result",
            id: inspection.id,
            result: {
                success: true,
                definition: false,
                recoverWorker: false,
                output: report,
                error: "",
                reductions: 0,
            },
        });

        const outputEntry = harness.element("output").lastElementChild;
        assert.equal(
            outputEntry.textContent,
            `${command}\n${report.trimEnd()}`,
        );
        assert.equal(outputEntry.dataset.compactAfter, "true");
        assert.equal(
            outputEntry.textContent.includes("[show end]"), false);
        assert.equal(
            harness.element("source-history").textContent, command);
    }
});

test("gives compare a longer watchdog window on start and rearm", () => {
    for (const [command, displayOnly, expectedDelay] of [
        ["Ix", false, 1000],
        ["compare ?x I = SKK", true, 1500],
    ]) {
        const harness = createHarness({
            watchdog: realEvaluationWatchdog,
        });
        const source = harness.element("source");
        const worker = harness.workers[0];

        worker.send({type: "ready", setList: ""});
        harness.flushAnimationFrames();
        source.value = command;
        harness.pressEnter();
        harness.flushAnimationFrames();

        const inspection = worker.messages.find(
            message => message.type === "inspect-definition");
        worker.send({
            type: "definition-inspection-result",
            id: inspection.id,
            result: {
                success: true,
                definition: false,
                displayOnly,
                showAll: false,
                find: false,
                replacement: "",
            },
        });
        harness.flushAnimationFrames();

        const initialTimer = harness.scheduledTimeouts.at(-1);
        assert.equal(initialTimer.delay, expectedDelay);

        worker.send({type: "eval-started", id: inspection.id});
        const rearmedTimer = harness.scheduledTimeouts.at(-1);
        assert.notEqual(rearmedTimer.id, initialTimer.id);
        assert.equal(rearmedTimer.delay, expectedDelay);
        assert.equal(
            harness.clearedTimeouts.includes(initialTimer.id), true);
    }
});

test("routes literal-backslash inspect without escape-only canonical output",
    () => {
        const harness = createHarness();
        const source = harness.element("source");
        const worker = harness.workers[0];
        const command = "inspect \\";
        const report =
            "free symbols: none\n" +
            "references: none\n" +
            "next reduction: none [normal form]\n";

        worker.send({type: "ready", setList: ""});
        harness.flushAnimationFrames();

        source.value = command;
        harness.pressEnter();
        harness.flushAnimationFrames();
        const inspection = worker.messages.find(
            message => message.type === "inspect-definition");
        assert.equal(inspection.source, command);
        worker.send({
            type: "definition-inspection-result",
            id: inspection.id,
            result: {
                success: true,
                definition: false,
                displayOnly: true,
                showAll: false,
                find: false,
                replacement: "",
            },
        });
        harness.flushAnimationFrames();

        const evaluation = worker.messages.find(
            message => message.type === "evaluate");
        assert.equal(evaluation.source, command);
        worker.send({
            type: "result",
            id: inspection.id,
            result: {
                success: true,
                definition: false,
                recoverWorker: false,
                output: report,
                error: "",
                reductions: 0,
            },
        });

        const outputEntry = harness.element("output").lastElementChild;
        assert.equal(
            outputEntry.textContent,
            `${command}\n${report.trimEnd()}`,
        );
        assert.equal(
            outputEntry.textContent.includes("canonical:"), false);
        assert.equal(outputEntry.dataset.compactAfter, "true");
    });

test("routes captured and live definitions as silent commands", () => {
    const cases = [
        ["define captured StudioCaptured xy = x(yx)", "single-step"],
        ["set live StudioLive = 1 StudioCaptured", "key-step"],
    ];

    for (const [command, steppingMode] of cases) {
        const harness = createHarness();
        const source = harness.element("source");
        const worker = harness.workers[0];

        worker.send({type: "ready", setList: ""});
        harness.flushAnimationFrames();
        harness.element(steppingMode).click();

        source.value = command;
        harness.pressEnter();
        harness.flushAnimationFrames();
        const inspection = worker.messages.find(
            message => message.type === "inspect-definition");
        worker.send({
            type: "definition-inspection-result",
            id: inspection.id,
            result: {
                success: true,
                definition: true,
                displayOnly: false,
                showAll: false,
                find: false,
                replacement: "",
            },
        });
        harness.flushAnimationFrames();

        const evaluation = worker.messages.find(
            message => message.type === "evaluate");
        assert.equal(evaluation.source, command);
        assert.equal(
            evaluation.singleStep,
            steppingMode === "single-step",
        );
        assert.equal(evaluation.keyStep, steppingMode === "key-step");
        assert.equal(evaluation.basisStep, false);
        assert.equal(evaluation.colorize, false);

        worker.send({
            type: "result",
            id: inspection.id,
            setList: command,
            result: {
                success: true,
                definition: true,
                recoverWorker: false,
                output: "",
                error: "",
                reductions: 0,
            },
        });

        assert.equal(harness.element("output").textContent, command);
        assert.equal(
            childElements(harness.element("output")).some(
                element => element.dataset.kind !== undefined),
            false,
            "definition modifier should not append result output",
        );
        assert.equal(
            harness.element("save").attributes.get("aria-disabled"),
            "false",
        );
    }
});

test("step-mode evaluation errors stay in the submitted Results entry", () => {
    for (const steppingMode of ["single-step", "key-step"]) {
        const harness = createHarness();
        const source = harness.element("source");
        const worker = harness.workers[0];
        const expression = "ErrorBird x";
        const error = "evaluation failed";

        worker.send({type: "ready", setList: ""});
        harness.flushAnimationFrames();
        harness.element(steppingMode).click();
        source.value = expression;
        harness.pressEnter();
        harness.flushAnimationFrames();
        const inspection = worker.messages.find(
            message => message.type === "inspect-definition");
        worker.send({
            type: "definition-inspection-result",
            id: inspection.id,
            result: {
                success: true,
                definition: false,
                displayOnly: false,
                showAll: false,
                find: false,
                replacement: "",
            },
        });
        harness.flushAnimationFrames();

        if (steppingMode === "key-step") {
            worker.send({
                type: "step-ready",
                id: inspection.id,
                result: {success: false, error},
            });
        } else {
            worker.send({type: "eval-started", id: inspection.id});
            worker.send({
                type: "result",
                id: inspection.id,
                result: {
                    success: false,
                    definition: false,
                    recoverWorker: false,
                    output: "",
                    error,
                    reductions: 0,
                    limitReached: false,
                },
            });
        }

        const output = harness.element("output");
        assert.equal(output.lastElementChild.textContent,
            `${expression}\n${error}`);
        assert.ok(
            childElements(output.lastElementChild).some(
                element => element.textContent === error &&
                    element.dataset.kind === "error"),
            "evaluation failure must retain error styling",
        );
        assert.equal(
            output.childNodes.filter(
                child => child instanceof FakeElement).length,
            1,
        );
        assert.equal(source.value, expression);
        assert.equal(harness.element("source-history").textContent, "");
        assert.equal(harness.element("status").textContent, "Ready");
        assert.equal(
            worker.messages.some(message => message.type === "step"),
            false,
        );
    }
});

test("routes every dependency alias as display-only", () => {
    const cases = [
        ["dependson A", "A is directly depended on by: B C\n"],
        ["depends-on A", "A is directly depended on by: B C\n"],
        ["depends on A", "A is directly depended on by: B C\n"],
        ["usedby A", "A directly uses: B C\n"],
        ["used-by A", "A directly uses: B C\n"],
        ["used by A", "A directly uses: B C\n"],
        [
            "dependson all A",
            "A is directly depended on by: B C\n" +
                "A is indirectly depended on by: D E\n",
        ],
        [
            "depends-on all A",
            "A is directly depended on by: B C\n" +
                "A is indirectly depended on by: D E\n",
        ],
        [
            "depends on all A",
            "A is directly depended on by: B C\n" +
                "A is indirectly depended on by: D E\n",
        ],
        [
            "usedby all A",
            "A directly uses: B C\nA indirectly uses: D E\n",
        ],
        [
            "used-by all A",
            "A directly uses: B C\nA indirectly uses: D E\n",
        ],
        [
            "used by all A",
            "A directly uses: B C\nA indirectly uses: D E\n",
        ],
        [
            "usedby path A B",
            "A uses B via:\n" +
                "  A -> C@2  [captured]\n" +
                "  C@2 -> B  [captured]\n",
        ],
        [
            "used-by path between A and B",
            "A uses B via:\n" +
                "  A -> C@2  [live] [name removed]\n" +
                "  C@2 -> B  [captured]\n",
        ],
        [
            "used by path A and B",
            "A uses B via:\n" +
                "  A -> B  [pre-defined]\n",
        ],
    ];

    for (const [command, commandOutput] of cases) {
        const harness = createHarness();
        const source = harness.element("source");
        const worker = harness.workers[0];

        worker.send({type: "ready", setList: ""});
        harness.flushAnimationFrames();
        harness.element("single-step").click();
        harness.element("basis-step").click();
        harness.element("colorize").click();

        source.value = command;
        harness.pressEnter();
        harness.flushAnimationFrames();
        const inspection = worker.messages.find(
            message => message.type === "inspect-definition");
        worker.send({
            type: "definition-inspection-result",
            id: inspection.id,
            result: {
                success: true,
                definition: false,
                displayOnly: true,
                showAll: false,
                find: false,
                replacement: "",
            },
        });
        harness.flushAnimationFrames();

        const evaluation = worker.messages.find(
            message => message.type === "evaluate");
        assert.equal(evaluation.source, command);
        assert.equal(evaluation.singleStep, false);
        assert.equal(evaluation.keyStep, false);
        assert.equal(evaluation.basisStep, false);
        assert.equal(evaluation.colorize, false);

        worker.send({
            type: "result",
            id: inspection.id,
            result: {
                success: true,
                definition: false,
                recoverWorker: false,
                output: commandOutput,
                error: "",
                reductions: 0,
            },
        });
        assert.equal(
            harness.element("output").textContent,
            `${command}\n${commandOutput.trimEnd()}`);
    }
});

test("routes typed references commands as silent saved definitions", () => {
    for (const command of [
        "references captured",
        "references live",
    ]) {
        const harness = createHarness();
        const source = harness.element("source");
        const worker = harness.workers[0];
        const updatedSetList = `${command}\n`;

        worker.send({type: "ready", setList: ""});
        harness.flushAnimationFrames();

        source.value = command;
        harness.pressEnter();
        harness.flushAnimationFrames();
        const inspection = worker.messages.find(
            message => message.type === "inspect-definition");
        worker.send({
            type: "definition-inspection-result",
            id: inspection.id,
            result: {
                success: true,
                definition: true,
                displayOnly: false,
                showAll: false,
                find: false,
                replacement: "",
            },
        });
        harness.flushAnimationFrames();

        const evaluation = worker.messages.find(
            message => message.type === "evaluate");
        assert.equal(evaluation.source, command);
        assert.equal(evaluation.singleStep, false);
        assert.equal(evaluation.keyStep, false);
        assert.equal(evaluation.basisStep, false);
        assert.equal(evaluation.colorize, false);

        worker.send({
            type: "result",
            id: inspection.id,
            setList: updatedSetList,
            result: {
                success: true,
                definition: true,
                recoverWorker: false,
                output: "",
                error: "",
                reductions: 0,
            },
        });
        assert.equal(harness.element("status").textContent, "Ready");
        assert.equal(harness.element("source-history").textContent, command);
        assert.equal(harness.element("output").textContent, command);
        assert.equal(
            childElements(harness.element("output")).some(
                element => element.dataset.kind !== undefined),
            false,
            "references should not append result output",
        );

        const save = harness.element("save");
        assert.equal(save.attributes.get("aria-disabled"), "false");
        save.click();
        assert.equal(
            harness.element("status").textContent,
            "Saved all definitions",
        );

        source.value = "MM";
        harness.pressEnter();
        harness.flushAnimationFrames();
        const pendingInspection = worker.messages.filter(
            message => message.type === "inspect-definition").at(-1);
        worker.send({
            type: "definition-inspection-result",
            id: pendingInspection.id,
            result: {
                success: true,
                definition: false,
                displayOnly: false,
                showAll: false,
                find: false,
                replacement: "",
            },
        });
        harness.flushAnimationFrames();
        worker.send({type: "eval-started", id: pendingInspection.id});
        pauseAndCancel(harness, worker, pendingInspection.id);

        assert.equal(worker.terminated, true);
        assert.equal(harness.workers.length, 2);
        const replacementWorker = harness.workers[1];
        replacementWorker.send({type: "ready", setList: ""});
        const restore = replacementWorker.messages.find(
            message => message.type === "load");
        assert.equal(restore?.source, updatedSetList);
    }
});

test("applies and disables a typed step limit for later evaluations", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const worker = harness.workers[0];

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();

    source.value = "step limit 2";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const limitInspection = worker.messages.find(
        message => message.type === "inspect-definition");
    worker.send({
        type: "definition-inspection-result",
        id: limitInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
            stepLimitCommand: true,
            stepLimitEnabled: true,
            stepLimit: 2,
        },
    });

    assert.equal(
        worker.messages.some(message => message.type === "evaluate"),
        false,
    );
    assert.equal(harness.element("status").textContent, "Ready");
    assert.equal(
        harness.element("source-history").textContent,
        "step limit 2",
    );

    source.value = "IIIx";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const expressionInspection = worker.messages.filter(
        message => message.type === "inspect-definition").at(-1);
    worker.send({
        type: "definition-inspection-result",
        id: expressionInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    const limitedEvaluation = worker.messages.filter(
        message => message.type === "evaluate").at(-1);
    assert.equal(limitedEvaluation.stepLimitEnabled, true);
    assert.equal(limitedEvaluation.stepLimit, 2);
    const output = harness.element("output");
    const expressionOutputEntry = output.lastElementChild;
    const outputBeforeStepLimit = output.textContent;

    worker.send({
        type: "result",
        id: expressionInspection.id,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "Ix\n",
            error: "",
            reductions: 2,
            limitReached: true,
        },
    });
    assert.equal(
        harness.element("status").textContent,
        "Step limit reached",
    );
    assert.equal(
        output.textContent,
        outputBeforeStepLimit,
        "reaching the step limit must not change Results",
    );
    assert.strictEqual(
        output.lastElementChild,
        expressionOutputEntry,
        "the paused evaluation must retain its Results entry",
    );
    const stepLimitDialog = harness.element("step-limit-dialog");
    const stepLimitResume = harness.element("step-limit-resume");
    assert.equal(stepLimitDialog.open, true);
    assert.equal(
        harness.element("step-limit-message").textContent,
        "Step limit reached after 2 steps.",
    );
    assert.equal(stepLimitResume.focusCount, 1);
    assert.equal(source.readOnly, true);
    assert.equal(
        harness.element("source-history").textContent,
        "step limit 2",
        "the expression must not enter history before it finishes",
    );

    harness.flushAnimationFrames();
    assert.equal(stepLimitResume.focusCount, 2);
    stepLimitResume.click();
    assert.equal(worker.messages.at(-1).type, "resume");
    assert.equal(worker.messages.at(-1).id, expressionInspection.id);
    assert.equal(worker.terminated, false);
    assert.equal(source.readOnly, true);
    assert.equal(harness.element("status").textContent, "Resuming…");
    assert.equal(output.textContent, outputBeforeStepLimit);
    assert.strictEqual(output.lastElementChild, expressionOutputEntry);

    worker.send({type: "eval-started", id: expressionInspection.id});
    worker.send({
        type: "result",
        id: expressionInspection.id,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "x\n",
            error: "",
            reductions: 3,
            limitReached: false,
        },
    });
    assert.equal(harness.element("status").textContent, "Ready");
    assert.equal(source.readOnly, false);
    assert.equal(source.value, "");
    assert.strictEqual(
        output.lastElementChild,
        expressionOutputEntry,
        "the resumed result must finish in the original Results entry",
    );
    assert.equal(
        expressionOutputEntry.textContent,
        "IIIx\nx",
        "the original entry must receive only the final result",
    );
    assert.equal(
        harness.element("source-history").textContent,
        "step limit 2IIIx",
    );
    assert.equal(
        harness.element("source-history").textContent.includes(
            "[step limit]"),
        false,
    );

    source.value = "step limit off";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const offInspection = worker.messages.filter(
        message => message.type === "inspect-definition").at(-1);
    worker.send({
        type: "definition-inspection-result",
        id: offInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
            stepLimitCommand: true,
            stepLimitEnabled: false,
            stepLimit: 0,
        },
    });

    source.value = "Ix";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const unlimitedInspection = worker.messages.filter(
        message => message.type === "inspect-definition").at(-1);
    worker.send({
        type: "definition-inspection-result",
        id: unlimitedInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    const unlimitedEvaluation = worker.messages.filter(
        message => message.type === "evaluate").at(-1);
    assert.equal(unlimitedEvaluation.stepLimitEnabled, false);
    assert.equal(unlimitedEvaluation.stepLimit, 0);
});

test("overwrites running step counts and refreshes the exact count at a limit", () => {
    const harness = createHarness({watchdog: realEvaluationWatchdog});
    const {worker, requestId, outputEntry} =
        beginLimitedEvaluation(harness, 2537, "BKM(BKM)");

    worker.send({type: "eval-started", id: requestId});
    worker.send({
        type: "eval-progress",
        id: requestId,
        sequence: 1,
        reductions: 1000,
    });
    let progressEntries = childElements(outputEntry).filter(
        element => element.dataset.kind === "progress");
    assert.equal(progressEntries.length, 1);
    const progressEntry = progressEntries[0];
    assert.equal(progressEntry.textContent, "[1000 steps so far]");

    worker.send({
        type: "eval-progress",
        id: requestId,
        sequence: 2,
        reductions: 2000,
    });
    progressEntries = childElements(outputEntry).filter(
        element => element.dataset.kind === "progress");
    assert.equal(progressEntries.length, 1);
    assert.strictEqual(progressEntries[0], progressEntry);
    assert.equal(progressEntry.textContent, "[2000 steps so far]");
    assert.equal(
        outputEntry.textContent.includes("[1000 steps so far]"),
        false,
        "the 2000-step count must overwrite the 1000-step count",
    );

    const stepLimitDialog = harness.element("step-limit-dialog");
    const showModal = stepLimitDialog.showModal.bind(stepLimitDialog);
    let progressAtDialogOpen;
    stepLimitDialog.showModal = () => {
        progressAtDialogOpen = progressEntry.textContent;
        showModal();
    };
    worker.send({
        type: "result",
        id: requestId,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "partial result must remain hidden\n",
            error: "",
            reductions: 2537,
            limitReached: true,
        },
    });

    assert.equal(stepLimitDialog.open, true);
    assert.equal(progressAtDialogOpen, "[2537 steps so far]");
    assert.strictEqual(
        childElements(outputEntry).find(
            element => element.dataset.kind === "progress"),
        progressEntry,
    );
    assert.equal(progressEntry.textContent, "[2537 steps so far]");
    assert.equal(
        outputEntry.textContent,
        "BKM(BKM)\n[2537 steps so far]",
    );
});

test("does not add a progress line below one thousand steps", () => {
    const harness = createHarness({watchdog: realEvaluationWatchdog});
    const {worker, requestId, outputEntry} =
        beginLimitedEvaluation(harness, 999, "IIx");

    worker.send({type: "eval-started", id: requestId});
    worker.send({
        type: "eval-progress",
        id: requestId,
        sequence: 1,
        reductions: 999,
    });
    assert.equal(
        childElements(outputEntry).some(
            element => element.dataset.kind === "progress"),
        false,
    );

    worker.send({
        type: "result",
        id: requestId,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "Ix\n",
            error: "",
            reductions: 999,
            limitReached: true,
        },
    });

    assert.equal(harness.element("step-limit-dialog").open, true);
    assert.equal(
        childElements(outputEntry).some(
            element => element.dataset.kind === "progress"),
        false,
    );
    assert.equal(outputEntry.textContent, "IIx");
});

test("ignores the configured step limit during Key Step", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const worker = harness.workers[0];

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();

    source.value = "step limit 1";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const limitInspection = worker.messages.find(
        message => message.type === "inspect-definition");
    worker.send({
        type: "definition-inspection-result",
        id: limitInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
            stepLimitCommand: true,
            stepLimitEnabled: true,
            stepLimit: 1,
        },
    });

    harness.element("key-step").click();
    source.value = "IIx";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const expressionInspection = worker.messages.filter(
        message => message.type === "inspect-definition").at(-1);
    worker.send({
        type: "definition-inspection-result",
        id: expressionInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    const evaluation = worker.messages.filter(
        message => message.type === "evaluate").at(-1);
    assert.equal(evaluation.keyStep, true);
    assert.equal(evaluation.stepLimitEnabled, false);
    assert.equal(evaluation.stepLimit, 0);

    worker.send({
        type: "step-ready",
        id: expressionInspection.id,
        result: {success: true},
    });
    worker.send({
        type: "step-result",
        id: expressionInspection.id,
        result: {
            success: true,
            reduced: true,
            complete: false,
            definition: false,
            output: "Ix\n",
            error: "",
            limitReached: false,
        },
    });

    assert.equal(harness.element("step-limit-dialog").open, false);
    assert.equal(
        harness.element("output").lastElementChild.textContent,
        "IIx\nIx",
    );
});

test("keeps the step limit when cancellation replaces the worker", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const firstWorker = harness.workers[0];

    firstWorker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();

    source.value = "step limit 7";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const limitInspection = firstWorker.messages.find(
        message => message.type === "inspect-definition");
    firstWorker.send({
        type: "definition-inspection-result",
        id: limitInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
            stepLimitCommand: true,
            stepLimitEnabled: true,
            stepLimit: 7,
        },
    });

    source.value = "MM";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const loopingInspection = firstWorker.messages.filter(
        message => message.type === "inspect-definition").at(-1);
    firstWorker.send({
        type: "definition-inspection-result",
        id: loopingInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    const output = harness.element("output");
    const expressionOutputEntry = output.lastElementChild;
    const outputBeforeStepLimit = output.textContent;
    firstWorker.send({
        type: "result",
        id: loopingInspection.id,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "MM\n",
            error: "",
            reductions: 7,
            limitReached: true,
        },
    });
    const stepLimitDialog = harness.element("step-limit-dialog");
    assert.equal(stepLimitDialog.open, true);
    assert.equal(
        output.textContent,
        outputBeforeStepLimit,
        "reaching the step limit must not change Results",
    );
    assert.strictEqual(output.lastElementChild, expressionOutputEntry);
    harness.element("step-limit-cancel").click();

    assert.equal(firstWorker.terminated, true);
    assert.strictEqual(output.lastElementChild, expressionOutputEntry);
    assert.equal(
        expressionOutputEntry.textContent,
        "MM\n[cancelled]",
        "Cancel alone must append [cancelled] to the paused entry",
    );
    assertRedNotice(output, "[cancelled]");
    assertRedNotice(harness.element("source-history"), "[cancelled]");
    const replacementWorker = harness.workers[1];
    replacementWorker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();

    source.value = "Ix";
    harness.pressEnter();
    harness.flushAnimationFrames();
    const replacementInspection = replacementWorker.messages.find(
        message => message.type === "inspect-definition");
    replacementWorker.send({
        type: "definition-inspection-result",
        id: replacementInspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: false,
            showAll: false,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();

    const evaluation = replacementWorker.messages.find(
        message => message.type === "evaluate");
    assert.equal(evaluation.stepLimitEnabled, true);
    assert.equal(evaluation.stepLimit, 7);
});

test("routes find as an unstepped cancellable search", () => {
    for (const command of [
        "find ?xy = x(yx)",
        "find among AKIS ?xy = x(yx)",
    ]) {
        const harness = createHarness();
        const source = harness.element("source");
        const worker = harness.workers[0];

        worker.send({type: "ready", setList: ""});
        harness.flushAnimationFrames();
        harness.element("single-step").click();
        harness.element("basis-step").click();
        harness.element("colorize").click();

        source.value = command;
        harness.pressEnter();
        harness.flushAnimationFrames();
        const inspection = worker.messages.find(
            message => message.type === "inspect-definition");
        worker.send({
            type: "definition-inspection-result",
            id: inspection.id,
            result: {
                success: true,
                definition: false,
                displayOnly: true,
                showAll: false,
                find: true,
                replacement: "",
            },
        });
        assert.equal(harness.element("status").textContent, "Searching…");
        harness.flushAnimationFrames();

        const evaluation = worker.messages.find(
            message => message.type === "evaluate");
        assert.equal(evaluation.source, command);
        assert.equal(evaluation.singleStep, false);
        assert.equal(evaluation.keyStep, false);
        assert.equal(evaluation.basisStep, false);
        assert.equal(evaluation.colorize, false);
        assert.equal(harness.element("cancel").disabled, false);
        assert.equal(harness.scheduledTimeouts.length, 0,
            "Find uses its own search deadline, not the evaluation watchdog");
    }
});

test("Pause restarts and resumes Find in its original Results entry", () => {
    const harness = createHarness();
    const {expression, outputEntry, requestId, setList, worker} =
        beginFindEvaluation(harness);
    const outputBeforePause = harness.element("output").textContent;

    harness.element("cancel").click();

    const pause = worker.messages.at(-1);
    assert.equal(pause.type, "pause");
    assert.equal(pause.id, requestId);
    assert.equal(worker.terminated, true,
        "Find cannot cooperate, so Pause must stop its worker immediately");
    assert.equal(harness.workers.length, 1,
        "the replacement worker must wait until Resume");
    assert.equal(harness.element("step-limit-dialog").open, true);
    assert.equal(harness.element("step-limit-title").textContent, "Paused");
    assert.strictEqual(harness.element("output").lastElementChild, outputEntry);
    assert.equal(harness.element("output").textContent, outputBeforePause);
    assert.equal(
        harness.element("output").textContent.includes("[cancelled]"),
        false,
    );
    assert.equal(harness.element("source-history").textContent, "");

    harness.element("step-limit-resume").click();

    assert.equal(harness.workers.length, 2);
    const replacementWorker = harness.workers[1];
    assert.equal(replacementWorker.terminated, false);
    assert.strictEqual(harness.element("output").lastElementChild, outputEntry);
    assert.equal(harness.element("output").textContent, outputBeforePause);
    replacementWorker.send({type: "ready", setList: ""});
    const restore = replacementWorker.messages.find(
        message => message.type === "load");
    assert.equal(restore.source, setList);
    assert.equal(
        replacementWorker.messages.some(
            message => message.type === "evaluate"),
        false,
        "Find must wait for saved definitions to be restored",
    );

    replacementWorker.send({
        type: "load-result",
        id: restore.id,
        setList,
        result: {success: true},
    });
    harness.flushAnimationFrames();
    const replay = replacementWorker.messages.filter(
        message => message.type === "evaluate").at(-1);
    assert.equal(replay.id, requestId);
    assert.equal(replay.source, expression);
    assert.equal(replay.singleStep, false);
    assert.equal(replay.keyStep, false);
    assert.equal(
        replacementWorker.messages.some(
            message => message.type === "inspect-definition"),
        false,
        "the preserved Find request must resume without a new entry or scan",
    );
    assert.strictEqual(harness.element("output").lastElementChild, outputEntry);
    assert.equal(harness.element("output").textContent, outputBeforePause);

    replacementWorker.send({
        type: "result",
        id: requestId,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "?=A\n",
            error: "",
            reductions: 0,
        },
    });
    assert.strictEqual(harness.element("output").lastElementChild, outputEntry);
    assert.equal(outputEntry.textContent, `${expression}\n?=A`);
    assert.equal(harness.element("source-history").textContent, expression);
    assert.equal(outputEntry.textContent.includes("[cancelled]"), false);
});

test("Pause dialog Cancel cancels a stopped Find", () => {
    const harness = createHarness();
    const {expression, outputEntry, worker} = beginFindEvaluation(harness);

    harness.element("cancel").click();
    assert.equal(worker.terminated, true);
    assert.equal(harness.element("step-limit-dialog").open, true);
    harness.element("step-limit-cancel").click();

    assert.equal(harness.workers.length, 2);
    assert.strictEqual(harness.element("output").lastElementChild, outputEntry);
    assert.equal(outputEntry.textContent, `${expression}\n[cancelled]`);
    assert.equal(
        outputEntry.textContent.split("[cancelled]").length - 1,
        1,
    );
    assertRedNotice(outputEntry, "[cancelled]");
    assertRedNotice(harness.element("source-history"), "[cancelled]");
});

test("renders a find no-match response as a red notice", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const worker = harness.workers[0];

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();
    source.value = "find ?x = Y";
    harness.pressEnter();
    harness.flushAnimationFrames();

    const inspection = worker.messages.find(
        message => message.type === "inspect-definition");
    worker.send({
        type: "definition-inspection-result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: true,
            showAll: false,
            find: true,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    worker.send({
        type: "result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "No match within search bounds\n",
            error: "",
            reductions: 0,
        },
    });

    const notice = childElements(harness.element("output")).find(
        element =>
            element.textContent === "No match within search bounds" &&
            element.dataset.kind === "notice");
    assert.ok(notice, "missing red find no-match notice");
});

test("uses a lowercase show end marker", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const worker = harness.workers[0];

    worker.send({type: "ready", setList: "set Foo = 0 I\n"});
    harness.flushAnimationFrames();
    source.value = "show all";
    harness.pressEnter();
    harness.flushAnimationFrames();

    const inspection = worker.messages.find(
        message => message.type === "inspect-definition");
    worker.send({
        type: "definition-inspection-result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            displayOnly: true,
            showAll: true,
            find: false,
            replacement: "",
        },
    });
    harness.flushAnimationFrames();
    worker.send({
        type: "result",
        id: inspection.id,
        result: {
            success: true,
            definition: false,
            recoverWorker: false,
            output: "set Foo = 0 I\n",
            error: "",
            reductions: 0,
        },
    });

    const marker = childElements(harness.element("output")).find(
        element => element.textContent === "[show end]" &&
            element.dataset.kind === "notice");
    assert.ok(marker, "missing lowercase show end marker");
});

test("keeps a circular definition path in the red error entry", () => {
    const harness = createHarness();
    const source = harness.element("source");
    const worker = harness.workers[0];
    const error =
        "Parse error at position 5: CircleA would have a " +
        "circular definition\nCircleA -> CircleB -> CircleA";

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();
    source.value = "set CircleA = 0 CircleB";
    harness.pressEnter();
    harness.flushAnimationFrames();

    const inspection = worker.messages.find(
        message => message.type === "inspect-definition");
    worker.send({
        type: "definition-inspection-result",
        id: inspection.id,
        result: {
            success: false,
            error,
        },
    });

    const displayed = childElements(harness.element("output")).find(
        element => element.textContent === error &&
            element.dataset.kind === "error");
    assert.ok(displayed, "missing red circular definition path");
});

test("marks restored cancellation and timeout history notices red", () => {
    const harness = createHarness({
        historyValues: [
            "Ix",
            "MM [cancelled]",
            "BKM(BKM) [timed out]",
        ],
    });
    const history = harness.element("source-history");

    assert.equal(history.textContent, [
        "Ix",
        "MM [cancelled]",
        "BKM(BKM) [timed out]",
    ].join(""));
    assertRedNotice(history, "[cancelled]");
    assertRedNotice(history, "[timed out]");
});
