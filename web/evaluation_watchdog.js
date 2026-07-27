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

globalThis.combdslEvaluationWatchdog = (() => {
    const initialWaitMs = 10000;
    const minimumWaitMs = 250;
    const progressMargin = 1.5;
    const messagesPerPhase = 10;
    const firstStepsPerMessage = 100;

    const safePositiveInteger = value =>
        Number.isSafeInteger(value) && value > 0;

    const createProgressState = () => ({
        sequence: 0,
        reductions: 0,
        elapsedMs: 0,
        hasProgress: false,
        stepsPerMessage: firstStepsPerMessage,
        messagesInPhase: 0,
        phaseElapsedTotalMs: 0,
        phaseIntervalCount: 0,
    });

    const timeoutMessage = state => {
        const reductions =
            Number.isSafeInteger(state?.reductions) &&
            state.reductions >= 0
                ? state.reductions
                : 0;
        return `[timed out after more than ${reductions} steps]`;
    };

    const acceptProgress = (state, message) => {
        if (state === null || typeof state !== "object" ||
            message === null || typeof message !== "object") {
            return undefined;
        }

        const sequence = message.sequence;
        const reductions = message.reductions;
        const stepsPerMessage = message.stepsPerMessage;
        const nextStepsPerMessage = message.nextStepsPerMessage;
        const elapsedMs = message.elapsedMs;

        if (!safePositiveInteger(sequence) ||
            !safePositiveInteger(reductions) ||
            !safePositiveInteger(stepsPerMessage) ||
            !safePositiveInteger(nextStepsPerMessage) ||
            typeof elapsedMs !== "number" ||
            !Number.isFinite(elapsedMs) ||
            elapsedMs < 0 ||
            sequence !== state.sequence + 1 ||
            stepsPerMessage !== state.stepsPerMessage ||
            reductions !== state.reductions + stepsPerMessage ||
            (state.hasProgress && elapsedMs < state.elapsedMs)) {
            return undefined;
        }

        const endsPhase =
            state.messagesInPhase + 1 === messagesPerPhase;
        const expectedNextSteps = endsPhase
            ? stepsPerMessage * 10
            : stepsPerMessage;
        if (!Number.isSafeInteger(expectedNextSteps) ||
            nextStepsPerMessage !== expectedNextSteps) {
            return undefined;
        }

        const intervalMs = state.hasProgress
            ? elapsedMs - state.elapsedMs
            : elapsedMs;
        const phaseElapsedTotalMs =
            state.phaseElapsedTotalMs + intervalMs;
        const phaseIntervalCount =
            state.phaseIntervalCount + 1;
        const phaseMeanMs =
            phaseElapsedTotalMs / phaseIntervalCount;

        state.sequence = sequence;
        state.reductions = reductions;
        state.elapsedMs = elapsedMs;
        state.hasProgress = true;

        let expectedIntervalMs = phaseMeanMs;
        if (endsPhase) {
            const phaseScale =
                nextStepsPerMessage / stepsPerMessage;
            expectedIntervalMs *= phaseScale;
            state.stepsPerMessage = nextStepsPerMessage;
            state.messagesInPhase = 0;
            state.phaseElapsedTotalMs = 0;
            state.phaseIntervalCount = 0;
        } else {
            state.messagesInPhase += 1;
            state.phaseElapsedTotalMs = phaseElapsedTotalMs;
            state.phaseIntervalCount = phaseIntervalCount;
        }

        return Math.max(
            minimumWaitMs,
            expectedIntervalMs * progressMargin);
    };

    return Object.freeze({
        initialWaitMs,
        minimumWaitMs,
        createProgressState,
        acceptProgress,
        timeoutMessage,
    });
})();
