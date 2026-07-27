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
    const heartbeatIntervalMs = 100;
    const timeoutMs = 1000;

    const safePositiveInteger = value =>
        Number.isSafeInteger(value) && value > 0;

    const createProgressState = () => ({
        sequence: 0,
        reductions: 0,
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
            return false;
        }

        const sequence = message.sequence;
        const reductions = message.reductions;

        if (!safePositiveInteger(sequence) ||
            !safePositiveInteger(reductions) ||
            sequence !== state.sequence + 1 ||
            reductions <= state.reductions) {
            return false;
        }

        state.sequence = sequence;
        state.reductions = reductions;
        return true;
    };

    return Object.freeze({
        heartbeatIntervalMs,
        timeoutMs,
        createProgressState,
        acceptProgress,
        timeoutMessage,
    });
})();
