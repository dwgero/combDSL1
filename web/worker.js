"use strict";

const baseUrl = new URL(".", self.location.href);
let modulePromise;
let busy = false;
let steppingRequestId;

const errorMessage = error =>
    error instanceof Error ? error.message : String(error);

try {
    importScripts(new URL("combdsl.js", baseUrl).href);
    modulePromise = createCombdslModule({
        locateFile: file => new URL(file, baseUrl).href,
    });
    modulePromise.then(
        () => self.postMessage({type: "ready"}),
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

    if (message.type === "evaluate" &&
        steppingRequestId === undefined) {
        busy = true;
        try {
            const module = await modulePromise;
            if (message.keyStep) {
                const result = module.beginSingleStep(
                    String(message.source));
                if (result.success) {
                    steppingRequestId = message.id;
                }
                self.postMessage({
                    type: "step-ready",
                    id: message.id,
                    result,
                });
            } else {
                const result = message.singleStep
                    ? module.singleStepRun(String(message.source))
                    : module.parseEval(String(message.source));
                self.postMessage({type: "result", id: message.id, result});
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
            const result = module.takeSingleStep();
            if (!result.success || !result.reduced) {
                steppingRequestId = undefined;
            }
            self.postMessage({
                type: "step-result",
                id: message.id,
                result,
            });
        } catch (error) {
            steppingRequestId = undefined;
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
