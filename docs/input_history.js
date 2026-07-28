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

    return Object.freeze({create});
})();
