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

const baseUrl = new URL(".", self.location.href);
let modulePromise;
let busy = false;
let steppingRequestId;
let steppingBasisStep = false;
let steppingColorize = false;

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
                    steppingBasisStep = Boolean(message.basisStep);
                    steppingColorize = Boolean(message.colorize);
                }
                self.postMessage({
                    type: "step-ready",
                    id: message.id,
                    result,
                });
            } else {
                const result = message.singleStep
                    ? (message.colorize
                        ? module.colorStepRun(
                            String(message.source),
                            Boolean(message.basisStep))
                        : module.singleStepRun(
                            String(message.source),
                            Boolean(message.basisStep)))
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
            const result = module.takeSingleStep(
                steppingBasisStep, steppingColorize);
            if (!result.success || !result.reduced) {
                steppingRequestId = undefined;
                steppingBasisStep = false;
                steppingColorize = false;
            }
            self.postMessage({
                type: "step-result",
                id: message.id,
                result,
            });
        } catch (error) {
            steppingRequestId = undefined;
            steppingBasisStep = false;
            steppingColorize = false;
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
