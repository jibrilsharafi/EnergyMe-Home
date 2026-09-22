// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Unit tests for the energy archive cache (source/js/data-helpers.js ArchiveCache).
// Run: node --test source/test/js/

const test = require('node:test');
const assert = require('node:assert/strict');

const store = new Map();
global.localStorage = {
    getItem: key => (store.has(key) ? store.get(key) : null),
    setItem: (key, value) => store.set(key, String(value)),
    removeItem: key => store.delete(key),
};
const { ArchiveCache, DataHelpers } = require('../../js/data-helpers.js');

let fetches = [];
DataHelpers.decompressGzipFile = async filename => { fetches.push(filename); return `fresh:${filename}`; };

function reset(listing) {
    store.clear();
    fetches = [];
    ArchiveCache.monthly = {};
    ArchiveCache.yearly = {};
    ArchiveCache.archiveFiles = { monthly: listing, yearly: null };
}

const seed = (key, entry) => store.set(ArchiveCache.CONFIG.PREFIX + key, JSON.stringify(entry));

test('an archive no longer listed is purged from the cache, not served', async () => {
    reset(new Map());
    seed('monthly_2026-08', { data: 'partial', timestamp: Date.now(), completed: true, size: 10 });
    await assert.rejects(ArchiveCache.loadArchiveWithCache('monthly', '2026-08'), /No monthly archive/);
    assert.equal(store.size, 0);
    assert.equal(fetches.length, 0);
});

test('a copy saved while the period was current gets the short ttl', async () => {
    reset(new Map([['2026-08', 10]]));
    seed('monthly_2026-08', { data: 'partial', timestamp: Date.now() - 2 * 3600 * 1000, completed: false, size: 10 });
    assert.equal(await ArchiveCache.loadArchiveWithCache('monthly', '2026-08'), 'fresh:energy/monthly/2026-08.csv.gz');
});

test('a completed copy with the listed size is served without a fetch', async () => {
    reset(new Map([['2026-08', 10]]));
    seed('monthly_2026-08', { data: 'cached', timestamp: Date.now() - 5 * 24 * 3600 * 1000, completed: true, size: 10 });
    assert.equal(await ArchiveCache.loadArchiveWithCache('monthly', '2026-08'), 'cached');
    assert.equal(fetches.length, 0);
});

test('a replaced file (different size) is refetched despite a valid ttl', async () => {
    reset(new Map([['2026-08', 12]]));
    seed('monthly_2026-08', { data: 'old', timestamp: Date.now(), completed: true, size: 10 });
    assert.equal(await ArchiveCache.loadArchiveWithCache('monthly', '2026-08'), 'fresh:energy/monthly/2026-08.csv.gz');
    assert.equal(JSON.parse(store.get(ArchiveCache.CONFIG.PREFIX + 'monthly_2026-08')).size, 12);
});

test('the memory copy is also checked against the listed size', async () => {
    reset(new Map([['2026-08', 10]]));
    await ArchiveCache.loadArchiveWithCache('monthly', '2026-08');
    ArchiveCache.archiveFiles.monthly = new Map([['2026-08', 11]]);
    store.clear();
    await ArchiveCache.loadArchiveWithCache('monthly', '2026-08');
    assert.equal(fetches.length, 2);
});

test('without a listing the fetch is attempted and cached', async () => {
    reset(null);
    assert.equal(await ArchiveCache.loadArchiveWithCache('monthly', '2026-08'), 'fresh:energy/monthly/2026-08.csv.gz');
    assert.equal(await ArchiveCache.loadArchiveWithCache('monthly', '2026-08'), 'fresh:energy/monthly/2026-08.csv.gz');
    assert.equal(fetches.length, 1);
});
