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

    const create = () => {
        const entries = [];

        const record = (source, outcome = "") => {
            const outcomeText = String(outcome);
            const entry =
                String(source) +
                (outcomeText === ""
                    ? ""
                    : ` [${outcomeText}]`);
            entries.push(entry);
            return entry;
        };

        const values = () => Object.freeze([...entries]);

        return Object.freeze({
            record,
            values,
        });
    };

    return Object.freeze({create, createCommandCompleter});
})();
