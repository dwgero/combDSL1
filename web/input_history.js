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

    const legacyHistoryStorageVersion = 1;
    const historyStorageVersion = 2;
    const historyStorageFormat = "entry-keys";
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
        now = Date.now,
        writerId,
    } = {}) => {
        const storedEntryLimit =
            Number.isSafeInteger(maximumStoredEntries) &&
                maximumStoredEntries > 0
                ? maximumStoredEntries
                : defaultMaximumStoredEntries;
        let persistentStorage = storage;
        const entryStoragePrefix = `${storageKey}.entry.`;
        const legacyDeletionPrefix =
            `${storageKey}.legacy-deleted.`;
        const markerText = JSON.stringify({
            version: historyStorageVersion,
            format: historyStorageFormat,
        });

        const disablePersistentStorage = () => {
            persistentStorage = undefined;
        };

        const storageGet = key => {
            if (persistentStorage === undefined) {
                return null;
            }
            try {
                return persistentStorage.getItem(key);
            } catch {
                disablePersistentStorage();
                return null;
            }
        };

        const storageSet = (key, value) => {
            if (persistentStorage === undefined) {
                return false;
            }
            try {
                persistentStorage.setItem(key, value);
                return true;
            } catch {
                disablePersistentStorage();
                return false;
            }
        };

        const storageRemove = key => {
            if (persistentStorage === undefined) {
                return false;
            }
            try {
                persistentStorage.removeItem(key);
                return true;
            } catch {
                disablePersistentStorage();
                return false;
            }
        };

        const validStoredEntry = entry =>
            entry !== null &&
            typeof entry === "object" &&
            typeof entry.id === "string" &&
            entry.id !== "" &&
            typeof entry.source === "string" &&
            typeof entry.outcome === "string" &&
            storedOutcomes.has(entry.outcome);

        const legacyEntryHash = entry => {
            const text = `${entry.source}\0${entry.outcome}`;
            let hash = 2166136261;
            for (let index = 0; index < text.length; ++index) {
                hash ^= text.charCodeAt(index);
                hash = Math.imul(hash, 16777619);
            }
            return (hash >>> 0).toString(16).padStart(8, "0");
        };

        const entryStorageKey = id =>
            entryStoragePrefix + encodeURIComponent(id);
        const legacyDeletionKey = id =>
            legacyDeletionPrefix + encodeURIComponent(id);
        const isLegacyEntryId = id =>
            /^m[0-9]{8}-[0-9a-f]{8}$/.test(id);

        const decodeStorageId = (key, prefix) => {
            if (typeof key !== "string" || !key.startsWith(prefix)) {
                return undefined;
            }
            try {
                const id = decodeURIComponent(key.slice(prefix.length));
                return id === "" ? undefined : id;
            } catch {
                return undefined;
            }
        };

        const validLegacyEntries = stored =>
            stored?.version === legacyHistoryStorageVersion &&
                Array.isArray(stored.entries)
                ? stored.entries.filter(entry =>
                    entry !== null &&
                    typeof entry === "object" &&
                    typeof entry.source === "string" &&
                    typeof entry.outcome === "string" &&
                    storedOutcomes.has(entry.outcome))
                    .slice(-storedEntryLimit)
                : undefined;

        const prepareStorage = () => {
            if (persistentStorage === undefined) {
                return;
            }
            if (typeof persistentStorage.key !== "function" ||
                typeof persistentStorage.removeItem !== "function") {
                disablePersistentStorage();
                return;
            }

            const text = storageGet(storageKey);
            if (persistentStorage === undefined) {
                return;
            }
            let stored;
            try {
                stored = text === null ? undefined : JSON.parse(text);
            } catch {
                stored = undefined;
            }
            if (stored?.version === historyStorageVersion &&
                stored?.format === historyStorageFormat) {
                return;
            }

            const legacyEntries = validLegacyEntries(stored);
            if (legacyEntries !== undefined) {
                legacyEntries.forEach((entry, index) => {
                    const id = `m${String(index).padStart(8, "0")}-${
                        legacyEntryHash(entry)}`;
                    if (storageGet(legacyDeletionKey(id)) !== null ||
                        storageGet(entryStorageKey(id)) !== null) {
                        return;
                    }
                    storageSet(entryStorageKey(id), JSON.stringify({
                        version: historyStorageVersion,
                        id,
                        source: entry.source,
                        outcome: entry.outcome,
                    }));
                });
            }
            storageSet(storageKey, markerText);
        };

        const relevantStorageSnapshot = () => {
            if (persistentStorage === undefined) {
                return undefined;
            }
            const records = [];
            let length;
            try {
                length = persistentStorage.length;
            } catch {
                disablePersistentStorage();
                return undefined;
            }
            for (let index = 0; index < length; ++index) {
                let key;
                try {
                    key = persistentStorage.key(index);
                } catch {
                    disablePersistentStorage();
                    return undefined;
                }
                if (typeof key !== "string" ||
                    (!key.startsWith(entryStoragePrefix) &&
                        !key.startsWith(legacyDeletionPrefix))) {
                    continue;
                }
                const value = storageGet(key);
                if (persistentStorage === undefined) {
                    return undefined;
                }
                records.push([key, value]);
            }
            records.sort(([left], [right]) =>
                left < right ? -1 : left > right ? 1 : 0);
            return records;
        };

        const sameSnapshot = (left, right) =>
            left !== undefined && right !== undefined &&
            left.length === right.length &&
            left.every((record, index) =>
                record[0] === right[index][0] &&
                record[1] === right[index][1]);

        const stableStorageSnapshot = () => {
            let previous = relevantStorageSnapshot();
            if (previous === undefined) {
                return {records: [], stable: false};
            }
            for (let attempt = 0; attempt < 3; ++attempt) {
                const current = relevantStorageSnapshot();
                if (sameSnapshot(previous, current)) {
                    return {records: current, stable: true};
                }
                if (current === undefined) {
                    return {records: previous, stable: false};
                }
                previous = current;
            }
            return {records: previous, stable: false};
        };

        const removeStoredEntry = entry => {
            if (persistentStorage === undefined) {
                return;
            }
            if (isLegacyEntryId(entry.id) &&
                !storageSet(legacyDeletionKey(entry.id), JSON.stringify({
                    version: historyStorageVersion,
                    id: entry.id,
                }))) {
                return;
            }
            storageRemove(entryStorageKey(entry.id));
        };

        const entriesFromStorage = () => {
            const {records, stable} = stableStorageSnapshot();
            if (persistentStorage === undefined || !stable) {
                return undefined;
            }
            const deletedLegacyIds = new Set();
            const loadedById = new Map();

            for (const [key, value] of records) {
                const deletedId =
                    decodeStorageId(key, legacyDeletionPrefix);
                if (deletedId !== undefined) {
                    let stored;
                    try {
                        stored = value === null
                            ? undefined
                            : JSON.parse(value);
                    } catch {
                        stored = undefined;
                    }
                    if (isLegacyEntryId(deletedId) &&
                        stored?.version === historyStorageVersion &&
                        stored?.id === deletedId) {
                        deletedLegacyIds.add(deletedId);
                    }
                    continue;
                }
                const id = decodeStorageId(key, entryStoragePrefix);
                if (id === undefined || value === null) {
                    continue;
                }
                let stored;
                try {
                    stored = JSON.parse(value);
                } catch {
                    continue;
                }
                if (stored?.version !== historyStorageVersion ||
                    !validStoredEntry(stored) || stored.id !== id) {
                    continue;
                }
                loadedById.set(id, {
                    id,
                    source: stored.source,
                    outcome: stored.outcome,
                });
            }

            let loaded = [...loadedById.values()]
                .filter(entry => !deletedLegacyIds.has(entry.id))
                .sort((left, right) =>
                    left.id < right.id ? -1 : left.id > right.id ? 1 : 0);
            const discarded = [];
            const deduplicated = [];
            for (const entry of loaded) {
                if (deduplicated.at(-1)?.source === entry.source) {
                    discarded.push(entry);
                } else {
                    deduplicated.push(entry);
                }
            }
            loaded = deduplicated;
            if (loaded.length > storedEntryLimit) {
                discarded.push(...loaded.slice(
                    0, loaded.length - storedEntryLimit));
                loaded = loaded.slice(-storedEntryLimit);
            }
            for (const entry of discarded) {
                removeStoredEntry(entry);
            }
            for (const id of deletedLegacyIds) {
                const entry = loadedById.get(id);
                if (entry !== undefined) {
                    storageRemove(entryStorageKey(id));
                }
            }
            return loaded;
        };

        const createWriterId = () => {
            if (writerId !== undefined && String(writerId) !== "") {
                return String(writerId).replace(/[^A-Za-z0-9_-]/g, "_");
            }
            try {
                if (typeof globalThis.crypto?.randomUUID === "function") {
                    return globalThis.crypto.randomUUID().replaceAll("-", "");
                }
            } catch {
                // Fall back to two independent random fractions below.
            }
            return `${Math.random().toString(36).slice(2)}${
                Math.random().toString(36).slice(2)}`;
        };

        const localWriterId = createWriterId();
        let lastRuntimeTimestamp = 0;
        let runtimeCounter = 0;
        const runtimeTimestamp = id => {
            const match = /^r([0-9]{16})-/.exec(id);
            return match === null ? 0 : Number(match[1]);
        };

        let entries = [];
        const nextEntryId = () => {
            const newestTimestamp = entries.reduce(
                (latest, entry) =>
                    Math.max(latest, runtimeTimestamp(entry.id)), 0);
            let clockValue;
            try {
                clockValue = Number(now());
            } catch {
                clockValue = Date.now();
            }
            if (!Number.isSafeInteger(clockValue) || clockValue < 0) {
                clockValue = Date.now();
            }
            const timestamp = Math.max(
                clockValue, newestTimestamp + 1, lastRuntimeTimestamp);
            if (timestamp === lastRuntimeTimestamp) {
                ++runtimeCounter;
            } else {
                lastRuntimeTimestamp = timestamp;
                runtimeCounter = 0;
            }
            return `r${String(timestamp).padStart(16, "0")}-${
                String(runtimeCounter).padStart(8, "0")}-${localWriterId}`;
        };

        prepareStorage();
        if (persistentStorage !== undefined) {
            entries = entriesFromStorage() ?? [];
        }

        let currentEntryId;
        let detachedEntryId;
        let draft;
        const operateAndGetNextPositions = new WeakMap();

        const displayEntry = entry =>
            entry.source +
            (entry.outcome === ""
                ? ""
                : ` [${entry.outcome}]`);

        const resetNavigation = () => {
            currentEntryId = undefined;
            detachedEntryId = undefined;
            draft = undefined;
        };

        const sameEntries = (left, right) =>
            left.length === right.length &&
            left.every((entry, index) =>
                entry.id === right[index].id &&
                entry.source === right[index].source &&
                entry.outcome === right[index].outcome);

        const synchronizeStorage = () => {
            if (persistentStorage === undefined) {
                return Object.freeze({
                    changed: false,
                    currentRemoved: false,
                });
            }
            prepareStorage();
            if (persistentStorage === undefined) {
                return Object.freeze({
                    changed: false,
                    currentRemoved: false,
                });
            }
            const synchronized = entriesFromStorage();
            if (synchronized === undefined) {
                return Object.freeze({
                    changed: false,
                    currentRemoved: false,
                });
            }
            const changed = !sameEntries(entries, synchronized);
            const currentRemoved = currentEntryId !== undefined &&
                !synchronized.some(entry => entry.id === currentEntryId);
            if (currentRemoved) {
                detachedEntryId = currentEntryId;
                currentEntryId = undefined;
            }
            entries = synchronized;
            return Object.freeze({changed, currentRemoved});
        };

        const handlesStorageKey = key =>
            key === null || key === storageKey ||
            (typeof key === "string" &&
                (key.startsWith(entryStoragePrefix) ||
                    key.startsWith(legacyDeletionPrefix)));

        const applyStorageEvent = (key, _newValue) =>
            handlesStorageKey(key)
                ? synchronizeStorage()
                : undefined;

        const record = (source, outcome = "") => {
            synchronizeStorage();
            const entry = {
                id: nextEntryId(),
                source: String(source),
                outcome: String(outcome),
            };
            if (entries.at(-1)?.source === entry.source) {
                resetNavigation();
                return undefined;
            }
            entries.push(entry);
            resetNavigation();
            if (persistentStorage !== undefined) {
                prepareStorage();
                storageSet(entryStorageKey(entry.id), JSON.stringify({
                    version: historyStorageVersion,
                    ...entry,
                }));
            }
            if (entries.length > storedEntryLimit) {
                const removed = entries.splice(
                    0, entries.length - storedEntryLimit);
                for (const oldEntry of removed) {
                    removeStoredEntry(oldEntry);
                }
            }
            return displayEntry(entry);
        };

        const values = () =>
            Object.freeze(entries.map(displayEntry));

        const previous = currentDraft => {
            synchronizeStorage();
            if (entries.length === 0) {
                return undefined;
            }
            let index;
            if (currentEntryId !== undefined) {
                index = entries.findIndex(
                    entry => entry.id === currentEntryId);
                if (index <= 0) {
                    return undefined;
                }
                --index;
            } else if (detachedEntryId !== undefined) {
                index = entries.findIndex(
                    entry => entry.id >= detachedEntryId) - 1;
                if (index < 0 &&
                    entries.at(-1)?.id < detachedEntryId) {
                    index = entries.length - 1;
                }
                if (index < 0) {
                    return undefined;
                }
            } else {
                draft = String(currentDraft);
                index = entries.length - 1;
            }
            currentEntryId = entries[index].id;
            detachedEntryId = undefined;
            return entries[index].source;
        };

        const next = () => {
            synchronizeStorage();
            if (currentEntryId === undefined &&
                detachedEntryId === undefined) {
                return undefined;
            }
            let index;
            if (currentEntryId !== undefined) {
                index = entries.findIndex(
                    entry => entry.id === currentEntryId) + 1;
            } else {
                index = entries.findIndex(
                    entry => entry.id > detachedEntryId);
                if (index === -1) {
                    index = entries.length;
                }
            }
            detachedEntryId = undefined;
            if (index >= entries.length) {
                currentEntryId = undefined;
                return draft ?? "";
            }
            currentEntryId = entries[index].id;
            return entries[index].source;
        };

        const hasCurrent = () => currentEntryId !== undefined &&
            entries.some(entry => entry.id === currentEntryId);

        const removeCurrent = () => {
            synchronizeStorage();
            if (!hasCurrent()) {
                return undefined;
            }

            const index = entries.findIndex(
                entry => entry.id === currentEntryId);
            const [removed] = entries.splice(index, 1);
            removeStoredEntry(removed);
            detachedEntryId = undefined;
            if (index < entries.length) {
                currentEntryId = entries[index].id;
            } else {
                currentEntryId = undefined;
            }
            return Object.freeze({
                index,
                nextSource: currentEntryId === undefined
                    ? draft ?? ""
                    : entries[index].source,
            });
        };

        const prepareOperateAndGetNext = () => {
            const operation = Object.freeze({});
            operateAndGetNextPositions.set(operation, Object.freeze({
                anchor: currentEntryId ?? detachedEntryId,
                live: currentEntryId === undefined &&
                    detachedEntryId === undefined,
            }));
            return operation;
        };

        const resumeOperateAndGetNext = operation => {
            const saved =
                operateAndGetNextPositions.get(operation);
            if (saved === undefined) {
                return undefined;
            }
            operateAndGetNextPositions.delete(operation);
            draft = undefined;
            detachedEntryId = undefined;
            if (saved.live || saved.anchor === undefined) {
                currentEntryId = undefined;
                return "";
            }
            const index = entries.findIndex(
                entry => entry.id > saved.anchor);
            if (index === -1) {
                currentEntryId = undefined;
                return "";
            }
            currentEntryId = entries[index].id;
            return entries[index].source;
        };

        return Object.freeze({
            applyStorageEvent,
            hasCurrent,
            handlesStorageKey,
            next,
            prepareOperateAndGetNext,
            previous,
            record,
            removeCurrent,
            resetNavigation,
            resumeOperateAndGetNext,
            synchronizeStorage,
            values,
        });
    };

    return Object.freeze({create, createCommandCompleter});
})();
