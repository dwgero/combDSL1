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
const evaluationWatchdogUrl = new URL(
    "../web/evaluation_watchdog.js", import.meta.url);
const evaluationWatchdogContext = vm.createContext({});
new vm.Script(readFileSync(evaluationWatchdogUrl, "utf8"), {
    filename: evaluationWatchdogUrl.pathname,
}).runInContext(evaluationWatchdogContext);
const realEvaluationWatchdog =
    evaluationWatchdogContext.combdslEvaluationWatchdog;

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

    querySelector() {
        return null;
    }

    setSelectionRange(start, end) {
        this.selectionStart = start;
        this.selectionEnd = end;
    }

    focus() {
        ++this.focusCount;
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

const createHarness = ({historyValues = [], watchdog} = {}) => {
    FakeWorker.instances = [];
    const animationFrames = [];
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

    const document = new FakeEventTarget();
    document.baseURI = "https://example.test/combdsl/index.html";
    document.hidden = false;
    document.querySelector = selector => elements.get(selector.slice(1));
    document.getElementById = id => elements.get(id);
    document.createElement = tagName => new FakeElement(tagName);
    document.createTextNode = text => new FakeTextNode(text);

    const storage = new Map();
    const window = new FakeEventTarget();
    window.localStorage = {
        getItem: key => storage.get(key) ?? null,
        setItem: (key, value) => storage.set(key, String(value)),
    };
    window.location = {protocol: "https:"};
    window.focus = () => {};
    const selection = {isCollapsed: true};
    window.getSelection = () => selection;

    const inputHistory = {
        record: (source, outcome = "") => outcome === ""
            ? source
            : `${source} [${outcome}]`,
        values: () => historyValues,
        resetNavigation: () => {},
        previous: () => undefined,
        next: () => undefined,
        prepareOperateAndGetNext: () => undefined,
        resumeOperateAndGetNext: () => undefined,
    };
    const inputHistoryTools = {
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
        clearTimeout: () => {},
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
        setTimeout: () => 1,
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

    return {
        element: id => elements.get(id),
        flushAnimationFrames() {
            while (animationFrames.length !== 0) {
                const frame = animationFrames.shift();
                frame();
            }
        },
        pressEnter() {
            elements.get("source").dispatch("keydown", {
                key: "Enter",
                isComposing: false,
            });
        },
        selection,
        workers: FakeWorker.instances,
    };
};

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

    harness.element("cancel").click();
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

test("routes abstract steps as display-only with modes enabled", () => {
    for (const steppingMode of ["single-step", "key-step"]) {
        const harness = createHarness();
        const source = harness.element("source");
        const worker = harness.workers[0];

        worker.send({type: "ready", setList: ""});
        harness.flushAnimationFrames();
        harness.element(steppingMode).click();
        harness.element("basis-step").click();
        harness.element("colorize").click();

        source.value = "abstract steps ?xy = x(yx)";
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
        assert.equal(
            evaluation.source,
            "abstract steps ?xy = x(yx)");
        assert.equal(evaluation.singleStep, false);
        assert.equal(evaluation.keyStep, false);
        assert.equal(evaluation.basisStep, false);
        assert.equal(evaluation.colorize, false);
    }
});

test("routes captured and live definitions as silent commands", () => {
    const cases = [
        "define captured StudioCaptured xy = x(yx)",
        "set live StudioLive = 1 StudioCaptured",
    ];

    for (const command of cases) {
        const harness = createHarness();
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

test("routes every dependency alias as display-only", () => {
    const cases = [
        ["dependson A", "A is depended on by: B C\n"],
        ["depends-on A", "A is depended on by: B C\n"],
        ["depends on A", "A is depended on by: B C\n"],
        ["usedby A", "A uses: B C\n"],
        ["used-by A", "A uses: B C\n"],
        ["used by A", "A uses: B C\n"],
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
        harness.element("cancel").click();

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
    stepLimitDialog.close("resume");
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
    stepLimitDialog.close("cancel");

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
    const harness = createHarness();
    const source = harness.element("source");
    const worker = harness.workers[0];

    worker.send({type: "ready", setList: ""});
    harness.flushAnimationFrames();
    harness.element("single-step").click();
    harness.element("basis-step").click();
    harness.element("colorize").click();

    source.value = "find ?xy = x(yx)";
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
    assert.equal(evaluation.source, "find ?xy = x(yx)");
    assert.equal(evaluation.singleStep, false);
    assert.equal(evaluation.keyStep, false);
    assert.equal(evaluation.basisStep, false);
    assert.equal(evaluation.colorize, false);
    assert.equal(harness.element("cancel").disabled, false);
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
