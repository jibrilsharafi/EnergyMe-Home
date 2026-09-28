// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Unit tests for the web UI energy aggregation (source/js/data-helpers.js).
// Run: node --test source/test/js/

const test = require('node:test');
const assert = require('node:assert/strict');
const { ChannelCache, EnergyAggregation: EA } = require('../../js/data-helpers.js');

const HOUR = 3600 * 1000;
const EPS = 1e-9;
const FUTURE = new Date('2030-01-01T00:00:00Z'); // loaded dates stop at "today"

// Whole-hour, DST (EU/US/southern), half-hour DST (Lord Howe), :30/:45 offsets, both
// date-line extremes.
const ZONES = ['UTC', 'Europe/Rome', 'America/New_York', 'America/Los_Angeles', 'Australia/Sydney',
    'Australia/Lord_Howe', 'Asia/Kolkata', 'Asia/Kathmandu', 'Pacific/Kiritimati', 'Pacific/Pago_Pago'];

function withTz(zone, fn) {
    const previous = process.env.TZ;
    process.env.TZ = zone;
    try { fn(); } finally { process.env.TZ = previous; }
}

// Hour-rounded UTC readings, as the firmware writes them. The per-hour increment varies
// with the hour so a misattributed interval changes the sums.
function makeReadings(startIso, endIso, channels = [0, 1, 2]) {
    const entries = [];
    const counters = channels.map(() => 1000000);
    for (let t = Date.parse(startIso), h = 0; t <= Date.parse(endIso); t += HOUR, h++) {
        channels.forEach((channel, i) => {
            entries.push({ timestamp: new Date(t).toISOString(), channel, activeImported: counters[i], activeExported: 0 });
            counters[i] += 100 + ((h * (i + 3)) % 17) * 10;
        });
    }
    return entries;
}

function sumBuckets(buckets) {
    const total = {};
    Object.values(buckets).forEach(bucket => Object.entries(bucket).forEach(([ch, v]) => {
        total[ch] = (total[ch] || 0) + v;
    }));
    return total;
}

function assertClose(actual, expected, message) {
    const channels = new Set([...Object.keys(actual), ...Object.keys(expected)]);
    channels.forEach(ch => assert.ok(Math.abs((actual[ch] || 0) - (expected[ch] || 0)) < EPS,
        `${message} ch${ch}: ${actual[ch]} != ${expected[ch]}`));
}

function localDays(year, month) {
    const days = [];
    const count = new Date(year, month, 0).getDate();
    for (let d = 1; d <= count; d++) days.push(`${year}-${EA.pad2(month)}-${EA.pad2(d)}`);
    return days;
}

// Covers every 2026 DST transition of the zones above plus margin around the year ends
const READINGS = makeReadings('2025-12-29T00:00:00Z', '2027-01-03T00:00:00Z');

for (const zone of ZONES) {
    test(`views are mutually consistent and lossless in ${zone}`, () => withTz(zone, () => {
        const total = EA.aggregate(READINGS, 'total', null).imported;
        const yearly = EA.aggregate(READINGS, 'yearly', '2026').imported;
        assertClose(sumBuckets(yearly), total['2026'], 'months sum to year');

        for (let month = 1; month <= 12; month++) {
            const period = `2026-${EA.pad2(month)}`;
            const monthly = EA.aggregate(READINGS, 'monthly', period).imported;
            assertClose(sumBuckets(monthly), yearly[EA.pad2(month)], `days sum to ${period}`);

            for (const day of localDays(2026, month)) {
                const daily = EA.aggregate(READINGS, 'daily', day).imported;
                assertClose(sumBuckets(daily), monthly[day.substring(8)], `hours sum to ${day}`);
            }
        }

        // Year total equals the counter delta between the readings at local new year
        const startOf = y => new Date(y, 0, 1).getTime();
        const firstAtOrAfter = t => Math.ceil(t / HOUR) * HOUR;
        const counterAt = (t, ch) => READINGS.find(e => Date.parse(e.timestamp) === t && e.channel === ch).activeImported;
        [0, 1, 2].forEach(ch => {
            const expected = (counterAt(firstAtOrAfter(startOf(2027)), ch) - counterAt(firstAtOrAfter(startOf(2026)), ch)) / 1000;
            assert.ok(Math.abs(total['2026'][ch] - expected) < EPS, `2026 total ch${ch}`);
        });
    }));

    test(`every hour of a past day is present, including the last, in ${zone}`, () => withTz(zone, () => {
        for (const day of ['2026-01-15', '2026-03-08', '2026-03-29', '2026-04-05', '2026-10-04', '2026-10-25', '2026-11-01']) {
            const [y, m, d] = day.split('-').map(Number);
            // Every UTC sample hour starting inside the local day, the first and last included
            const expected = new Set();
            let lastStart = null;
            for (let t = Math.floor(new Date(y, m - 1, d).getTime() / HOUR) * HOUR - HOUR; t < new Date(y, m - 1, d + 1).getTime(); t += HOUR) {
                const start = new Date(t);
                if (EA.localDateString(start) !== day) continue;
                expected.add(`${EA.pad2(start.getHours())}:${EA.pad2(start.getMinutes())}`);
                lastStart = start;
            }
            const daily = EA.aggregate(READINGS, 'daily', day).imported;
            assert.deepEqual(new Set(Object.keys(daily)), expected, day);
            // Hour 23 (or 23:30/23:15 in offset zones) is the one that used to go missing
            assert.equal(lastStart.getHours(), 23, `${day} last hour`);
        }
    }));

    test(`loaded UTC dates cover every period and its closing reading in ${zone}`, () => withTz(zone, () => {
        const covers = (dates, t) => dates.includes(new Date(t).toISOString().substring(0, 10));
        for (const day of ['2026-01-01', '2026-03-29', '2026-10-25', '2026-12-31']) {
            const [y, m, d] = day.split('-').map(Number);
            const dates = EA.utcDatesForPeriod('daily', day, null, FUTURE);
            assert.ok(covers(dates, new Date(y, m - 1, d).getTime() - HOUR), `${day} start`);
            assert.ok(covers(dates, new Date(y, m - 1, d + 1).getTime() + HOUR), `${day} closing reading`);
        }
        const monthDates = EA.utcDatesForPeriod('monthly', '2026-12', null, FUTURE);
        assert.ok(covers(monthDates, new Date(2027, 0, 1).getTime() + HOUR), 'month closing reading');
        const yearDates = EA.utcDatesForPeriod('yearly', '2026', null, FUTURE);
        assert.ok(covers(yearDates, new Date(2026, 0, 1).getTime() - HOUR), 'year start');
        assert.ok(covers(yearDates, new Date(2027, 0, 1).getTime() + HOUR), 'year closing reading');
        const now = new Date(2026, 8, 22, 23, 30);
        const totalDates = EA.utcDatesForPeriod('total', null, '2025', now);
        assert.ok(covers(totalDates, new Date(2025, 0, 1).getTime()), 'total start');
        assert.ok(covers(totalDates, now.getTime()), 'total now');
        assert.ok(!covers(totalDates, now.getTime() + 2 * EA.MS_PER_DAY), 'total stops at today');
    }));
}

test('the in-progress hour of today is not invented', () => withTz('Europe/Rome', () => {
    const readings = makeReadings('2026-09-21T22:00:00Z', '2026-09-22T20:00:00Z');
    const keys = Object.keys(EA.aggregate(readings, 'daily', '2026-09-22').imported).sort();
    assert.equal(keys[0], '00:00');
    assert.equal(keys[keys.length - 1], '21:00'); // 21:00-22:00 local closes at 20:00Z
    assert.equal(keys.length, 22);
}));

test('duplicated and shuffled input gives the same result', () => withTz('Europe/Rome', () => {
    const readings = makeReadings('2026-09-01T00:00:00Z', '2026-09-03T00:00:00Z');
    const messy = [...readings, ...readings.slice(10, 40)].reverse();
    assert.deepEqual(EA.aggregate(messy, 'monthly', '2026-09'), EA.aggregate(readings, 'monthly', '2026-09'));
}));

test('a gap is spread over its hours and conserves energy', () => withTz('UTC', () => {
    const readings = makeReadings('2026-09-01T00:00:00Z', '2026-09-02T00:00:00Z', [0]);
    const gapped = readings.filter(e => !['10', '11', '12'].includes(e.timestamp.substring(11, 13)));
    const daily = EA.aggregate(gapped, 'daily', '2026-09-01').imported;
    const expectedGap = (readings[13].activeImported - readings[9].activeImported) / 1000 / 4;
    ['09:00', '10:00', '11:00', '12:00'].forEach(h => assert.ok(Math.abs(daily[h][0] - expectedGap) < EPS, h));
    assertClose(sumBuckets(daily), sumBuckets(EA.aggregate(readings, 'daily', '2026-09-01').imported), 'day total');
}));

test('a gap across midnight is split between the two days', () => withTz('UTC', () => {
    const readings = makeReadings('2026-09-01T20:00:00Z', '2026-09-02T04:00:00Z', [0])
        .filter(e => e.timestamp < '2026-09-01T22' || e.timestamp >= '2026-09-02T02');
    const before = sumBuckets(EA.aggregate(readings, 'daily', '2026-09-01').imported)[0];
    const after = sumBuckets(EA.aggregate(readings, 'daily', '2026-09-02').imported)[0];
    const total = (readings[readings.length - 1].activeImported - readings[0].activeImported) / 1000;
    assert.ok(Math.abs(before + after - total) < EPS);
    assert.ok(after > 0 && before > 0);
}));

// What the page does: each view only sees the UTC dates it loads
const loadedFor = (entries, view, period, firstYear, now) => {
    const dates = new Set(EA.utcDatesForPeriod(view, period, firstYear, now));
    return entries.filter(e => dates.has(e.timestamp.substring(0, 10)));
};

for (const zone of ['Europe/Rome', 'America/New_York', 'Pacific/Kiritimati', 'Pacific/Pago_Pago', 'Asia/Kathmandu']) {
    test(`views agree across gaps with only their own files loaded in ${zone}`, () => withTz(zone, () => {
        const now = new Date('2026-12-31T00:00:00Z');
        const gaps = [['2026-09-03T10', '2026-09-03T13'], ['2026-09-06T02', '2026-09-07T01'], ['2026-09-10T12', '2026-09-13T12'], ['2026-09-19T22', '2026-09-20T23']];
        const readings = makeReadings('2026-08-25T00:00:00Z', '2026-10-05T00:00:00Z')
            .filter(e => !gaps.some(([a, b]) => e.timestamp >= a && e.timestamp < b));
        const monthly = EA.aggregate(loadedFor(readings, 'monthly', '2026-09', null, now), 'monthly', '2026-09').imported;
        for (const day of localDays(2026, 9)) {
            const daily = EA.aggregate(loadedFor(readings, 'daily', day, null, now), 'daily', day).imported;
            assertClose(sumBuckets(daily), monthly[day.substring(8)] || {}, `${day} vs monthly`);
        }
        const yearly = EA.aggregate(loadedFor(readings, 'yearly', '2026', null, now), 'yearly', '2026').imported;
        assertClose(sumBuckets(monthly), yearly['09'], 'September vs yearly');
    }));
}

test('a gap longer than a day is unknown, not spread', () => withTz('UTC', () => {
    const all = makeReadings('2026-09-01T00:00:00Z', '2026-09-05T00:00:00Z', [0]);
    const at = iso => all.find(e => e.timestamp.startsWith(iso)).activeImported;
    // 26 h gap (02T00 -> 03T02): dropped
    const long = all.filter(e => e.timestamp < '2026-09-02T01' || e.timestamp >= '2026-09-03T02');
    const monthly = EA.aggregate(long, 'monthly', '2026-09').imported;
    assert.equal(monthly['02'], undefined);
    const gapWh = at('2026-09-03T02') - at('2026-09-02T00');
    const allWh = at('2026-09-05T00') - at('2026-09-01T00');
    assert.ok(Math.abs(sumBuckets(monthly)[0] - (allWh - gapWh) / 1000) < EPS);
    // Exactly 24 h (02T00 -> 03T00): spread
    const day = all.filter(e => e.timestamp < '2026-09-02T01' || e.timestamp >= '2026-09-03T00');
    assert.ok(Math.abs(sumBuckets(EA.aggregate(day, 'monthly', '2026-09').imported)[0] - allWh / 1000) < EPS);
}));

test('loaded dates never go past today', () => withTz('Europe/Rome', () => {
    const now = new Date('2026-09-22T20:00:00Z');
    const dates = EA.utcDatesForPeriod('monthly', '2026-09', null, now);
    assert.equal(dates[dates.length - 1], '2026-09-22');
    assert.equal(dates[0], '2026-08-30');
}));

test('local available dates follow the browser timezone and stop at local today', () => {
    withTz('America/New_York', () => {
        // 21:30 EDT on Sep 22 is Sep 23 in UTC, where a new daily file already exists
        const now = new Date('2026-09-23T01:30:00Z');
        assert.deepEqual(EA.localDatesForUtcDates(['2026-09-22', '2026-09-23'], now), ['2026-09-21', '2026-09-22']);
    });
    withTz('Pacific/Kiritimati', () => {
        // +14: local Sep 23 has started while UTC is still Sep 22
        const now = new Date('2026-09-22T12:00:00Z');
        assert.deepEqual(EA.localDatesForUtcDates(['2026-09-22'], now), ['2026-09-22', '2026-09-23']);
    });
    withTz('UTC', () => {
        assert.deepEqual(EA.localDatesForUtcDates(['2026-09-21'], new Date('2026-09-22T00:00:00Z')), ['2026-09-21']);
    });
});

test('a counter reset never yields negative energy', () => withTz('UTC', () => {
    const readings = makeReadings('2026-09-01T00:00:00Z', '2026-09-01T05:00:00Z', [0]);
    readings[3] = { ...readings[3], activeImported: 5 };
    const daily = EA.aggregate(readings, 'daily', '2026-09-01').imported;
    Object.values(daily).forEach(bucket => assert.ok(bucket[0] >= 0));
    assert.equal(daily['02:00'][0], 0);
}));

test('export deltas are aggregated like imports', () => withTz('UTC', () => {
    const readings = makeReadings('2026-09-01T00:00:00Z', '2026-09-01T03:00:00Z', [0])
        .map((e, i) => ({ ...e, activeExported: i * 500 }));
    const { exported } = EA.aggregate(readings, 'daily', '2026-09-01');
    assert.deepEqual(Object.keys(exported).sort(), ['00:00', '01:00', '02:00']);
    assert.equal(exported['01:00'][0], 0.5);
}));

test('half-hour offset zones key hours by their real local start', () => withTz('Asia/Kolkata', () => {
    const keys = Object.keys(EA.aggregate(READINGS, 'daily', '2026-09-22').imported).sort();
    assert.equal(keys.length, 24);
    assert.equal(keys[0], '00:30');
    assert.equal(keys[23], '23:30');
}));

test('csv labels are full local-time periods', () => {
    assert.equal(EA.csvPeriodLabel('23:00', 'daily', '2026-09-21'), '2026-09-21 23:00');
    assert.equal(EA.csvPeriodLabel('05', 'monthly', '2026-09'), '2026-09-05');
    assert.equal(EA.csvPeriodLabel('12', 'yearly', '2026'), '2026-12');
    assert.equal(EA.csvPeriodLabel('2026', 'total', null), '2026');
});

test('finalize derives Other from grid and hides role channels', () => {
    ChannelCache.setChannelData([
        { index: 0, role: 'grid', label: 'Grid' },
        { index: 1, role: 'load', label: 'A' },
        { index: 2, role: 'load', label: 'B' },
    ]);
    const result = EA.finalize({ imported: { '10:00': { 0: 3, 1: 1, 2: 0.5 } }, exported: {} });
    assert.deepEqual(result.imported['10:00'], { 1: 1, 2: 0.5, Other: 1.5 });
    assert.deepEqual(result.rawImported['10:00'], { 0: 3, 1: 1, 2: 0.5 });
    ChannelCache.setChannelData(null);
});
