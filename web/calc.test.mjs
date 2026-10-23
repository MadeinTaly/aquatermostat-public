// Tests for calc.mjs using Node.js test runner
import { test } from 'node:test';
import assert from 'node:assert';
import { HP_TO_W, clamp, hpToW, wToHp, physicalStallMin } from './calc.mjs';

test('clamp function', async (t) => {
  await t.test('returns min when value < min', () => {
    assert.strictEqual(clamp(5, 10, 20), 10);
  });

  await t.test('returns max when value > max', () => {
    assert.strictEqual(clamp(25, 10, 20), 20);
  });

  await t.test('returns value when within range', () => {
    assert.strictEqual(clamp(15, 10, 20), 15);
  });

  await t.test('returns value when equal to min', () => {
    assert.strictEqual(clamp(10, 10, 20), 10);
  });

  await t.test('returns value when equal to max', () => {
    assert.strictEqual(clamp(20, 10, 20), 20);
  });
});

test('hpToW function', async (t) => {
  await t.test('converts 0.25 HP to approximately 186 W', () => {
    const result = hpToW(0.25);
    assert.strictEqual(result, 186);
  });

  await t.test('converts 1 HP to approximately 746 W', () => {
    const result = hpToW(1);
    assert.strictEqual(result, 746);
  });

  await t.test('converts 2 HP to approximately 1491 W', () => {
    const result = hpToW(2);
    assert.strictEqual(result, 1491);
  });

  await t.test('converts 0 HP to 0 W', () => {
    assert.strictEqual(hpToW(0), 0);
  });
});

test('wToHp function', async (t) => {
  await t.test('converts 186 W to approximately 0.25 HP', () => {
    const result = wToHp(186);
    assert.strictEqual(result, 0.25);
  });

  await t.test('converts 746 W to approximately 1.00 HP', () => {
    const result = wToHp(746);
    assert.strictEqual(result, 1.00);
  });

  await t.test('converts 1491 W to approximately 2.00 HP', () => {
    const result = wToHp(1491);
    assert.strictEqual(result, 2.00);
  });

  await t.test('converts 0 W to 0 HP', () => {
    assert.strictEqual(wToHp(0), 0);
  });
});

test('wToHp / hpToW round-trip', async (t) => {
  await t.test('round-trip 0.5 HP', () => {
    const hp = 0.5;
    const w = hpToW(hp);
    const hpBack = wToHp(w);
    assert.strictEqual(hpBack, hp);
  });

  await t.test('round-trip 1 HP', () => {
    const hp = 1;
    const w = hpToW(hp);
    const hpBack = wToHp(w);
    assert.strictEqual(hpBack, hp);
  });

  await t.test('round-trip 3.5 HP', () => {
    const hp = 3.5;
    const w = hpToW(hp);
    const hpBack = wToHp(w);
    assert.strictEqual(hpBack, hp);
  });
});

test('physicalStallMin function', async (t) => {
  await t.test('returns null if liters <= 0', () => {
    assert.strictEqual(physicalStallMin(0, 300, 0.2), null);
    assert.strictEqual(physicalStallMin(-10, 300, 0.2), null);
  });

  await t.test('returns null if watts <= 0', () => {
    assert.strictEqual(physicalStallMin(200, 0, 0.2), null);
    assert.strictEqual(physicalStallMin(200, -100, 0.2), null);
  });

  await t.test('calculates stall timeout for typical 200L / 300W / 0.2 step', () => {
    // Expected: time = 200 * 4186 * 0.2 / 300 ≈ 558.13 s
    // stall = 3 * 558.13 / 60 ≈ 27.9 min → clamp to [8, 45] = 28
    const result = physicalStallMin(200, 300, 0.2);
    assert.strictEqual(result, 28);
  });

  await t.test('clamps result to min (8 min)', () => {
    // Small tank, high power: stall should be clamped up
    const result = physicalStallMin(10, 5000, 0.1);
    assert.ok(result >= 8, `Expected >= 8, got ${result}`);
  });

  await t.test('clamps result to max (45 min)', () => {
    // Large tank, low power: stall should be clamped down
    const result = physicalStallMin(1000, 10, 0.2);
    assert.ok(result <= 45, `Expected <= 45, got ${result}`);
  });

  await t.test('respects custom margin', () => {
    // margin=1 should give lower result than margin=3
    const result1 = physicalStallMin(200, 300, 0.2, 1);
    const result3 = physicalStallMin(200, 300, 0.2, 3);
    assert.ok(result1 < result3, `margin=1 (${result1}) should be < margin=3 (${result3})`);
  });

  await t.test('typical cooler scenario: 200L / 500W / 0.1 step', () => {
    // time = 200 * 4186 * 0.1 / 500 ≈ 167.44 s
    // stall = 3 * 167.44 / 60 ≈ 8.37 min → clamp [8, 45] = 8
    const result = physicalStallMin(200, 500, 0.1);
    assert.ok(result >= 8 && result <= 45, `Result ${result} out of bounds`);
  });
});

test('HP_TO_W constant', async (t) => {
  await t.test('HP_TO_W is 745.7', () => {
    assert.strictEqual(HP_TO_W, 745.7);
  });
});
