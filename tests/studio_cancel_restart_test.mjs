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
    }

    append(...children) {
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

    focus() {}

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

const createHarness = ({historyValues = []} = {}) => {
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
    const evaluationWatchdog = {
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
        workers: FakeWorker.instances,
    };
};

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
