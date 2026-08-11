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

globalThis.combdslInputHistory = (() => {
    const commandWhitespace = "[ \\t\\n\\r\\f\\v]";
    const commandWhitespaceOnly =
        new RegExp(`^${commandWhitespace}+$`);
    const commandWhitespaceRuns =
        new RegExp(`${commandWhitespace}+`, "g");
    const commandParts =
        new RegExp(`${commandWhitespace}+|[^ \\t\\n\\r\\f\\v]+`, "g");
    const leadingOrTrailingCommandWhitespace =
        new RegExp(
            `^${commandWhitespace}+|${commandWhitespace}+$`, "g");
    const endsWithCommandWhitespace =
        new RegExp(`${commandWhitespace}$`);

    const createCommandCompleter = (
        commandPhrases,
        {appendSpaceToExact = false} = {},
    ) => {
        const commands = [...new Set(
            Array.from(
                commandPhrases,
                phrase => String(phrase)
                    .replace(leadingOrTrailingCommandWhitespace, "")
                    .replace(commandWhitespaceRuns, " "),
            ).filter(phrase => phrase !== ""),
        )].map(phrase => phrase.split(" "));

        return source => {
            const original = String(source);
            const parts = original.match(commandParts) ?? [];
            const prefixes = parts.filter(
                part => !commandWhitespaceOnly.test(part));
            if (prefixes.length === 0) {
                return undefined;
            }

            const matches = commands.filter(words =>
                words.length >= prefixes.length &&
                prefixes.every((prefix, index) =>
                    words[index].startsWith(prefix)));
            if (matches.length !== 1) {
                return undefined;
            }

            const words = matches[0];
            let word = 0;
            let completed = parts.map(part => {
                if (commandWhitespaceOnly.test(part)) {
                    return part;
                }
                return words[word++];
            }).join("");

            while (word < words.length) {
                if (!endsWithCommandWhitespace.test(completed)) {
                    completed += " ";
                }
                completed += words[word++];
            }

            if (appendSpaceToExact &&
                !endsWithCommandWhitespace.test(completed)) {
                completed += " ";
            }
            return completed === original ? undefined : completed;
        };
    };

    const historyStorageVersion = 1;
    const defaultStorageKey =
        "combdsl.studio.input-history.v1";
    const defaultMaximumStoredEntries = 500;
    const storedOutcomes = new Set([
        "", "cancelled", "timed out", "step limit",
    ]);

    const create = ({
        storage,
        storageKey = defaultStorageKey,
        maximumStoredEntries = defaultMaximumStoredEntries,
    } = {}) => {
        const storedEntryLimit =
            Number.isSafeInteger(maximumStoredEntries) &&
                maximumStoredEntries > 0
                ? maximumStoredEntries
                : defaultMaximumStoredEntries;
        let persistentStorage = storage;

        const loadEntries = () => {
            if (persistentStorage === undefined) {
                return [];
            }

            let text;
            try {
                text = persistentStorage.getItem(storageKey);
            } catch {
                persistentStorage = undefined;
                return [];
            }
            if (text === null) {
                return [];
            }

            let stored;
            try {
                stored = JSON.parse(text);
            } catch {
                return [];
            }
            if (stored?.version !== historyStorageVersion ||
                !Array.isArray(stored.entries)) {
                return [];
            }
            return stored.entries
                .filter(entry =>
                    entry !== null &&
                    typeof entry === "object" &&
                    typeof entry.source === "string" &&
                    typeof entry.outcome === "string" &&
                    storedOutcomes.has(entry.outcome))
                .slice(-storedEntryLimit)
                .map(entry => ({
                    source: entry.source,
                    outcome: entry.outcome,
                }));
        };

        const entries = loadEntries();
        let position = entries.length;
        let draft;
        const operateAndGetNextPositions = new WeakMap();

        const displayEntry = entry =>
            entry.source +
            (entry.outcome === ""
                ? ""
                : ` [${entry.outcome}]`);

        const persist = () => {
            if (persistentStorage === undefined) {
                return;
            }
            try {
                persistentStorage.setItem(
                    storageKey,
                    JSON.stringify({
                        version: historyStorageVersion,
                        entries: entries.slice(-storedEntryLimit),
                    }),
                );
            } catch {
                persistentStorage = undefined;
            }
        };

        const resetNavigation = () => {
            position = entries.length;
            draft = undefined;
        };

        const record = (source, outcome = "") => {
            const entry = {
                source: String(source),
                outcome: String(outcome),
            };
            entries.push(entry);
            resetNavigation();
            persist();
            return displayEntry(entry);
        };

        const values = () =>
            Object.freeze(entries.map(displayEntry));

        const previous = currentDraft => {
            if (entries.length === 0 || position === 0) {
                return undefined;
            }
            if (position === entries.length) {
                draft = String(currentDraft);
            }
            --position;
            return entries[position].source;
        };

        const next = () => {
            if (position === entries.length) {
                return undefined;
            }
            ++position;
            return position === entries.length
                ? draft ?? ""
                : entries[position].source;
        };

        const prepareOperateAndGetNext = () => {
            const operation = Object.freeze({});
            operateAndGetNextPositions.set(
                operation, position + 1);
            return operation;
        };

        const resumeOperateAndGetNext = operation => {
            const savedPosition =
                operateAndGetNextPositions.get(operation);
            if (savedPosition === undefined) {
                return undefined;
            }
            operateAndGetNextPositions.delete(operation);
            position = Math.min(savedPosition, entries.length);
            draft = undefined;
            return position === entries.length
                ? ""
                : entries[position].source;
        };

        return Object.freeze({
            next,
            prepareOperateAndGetNext,
            previous,
            record,
            resetNavigation,
            resumeOperateAndGetNext,
            values,
        });
    };

    return Object.freeze({create, createCommandCompleter});
})();
