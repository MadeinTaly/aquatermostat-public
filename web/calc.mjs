// DEVE restare identica a src/webpage.h (testata)
// Pure calculation functions for frontend

export const HP_TO_W = 745.7;

/**
 * Clamp a value between min and max
 */
export function clamp(value, min, max) {
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

/**
 * Convert horsepower to watts (rounded to integer)
 */
export function hpToW(hp) {
  return Math.round(hp * HP_TO_W);
}

/**
 * Convert watts to horsepower (rounded to 2 decimals)
 */
export function wToHp(w) {
  return Math.round((w / HP_TO_W) * 100) / 100;
}

/**
 * Calculate physical stall minimum timeout based on water thermodynamics
 *
 * Formula:
 *   time_expected_s = liters * 4186 * stepC / Power_W
 *   stall_min = clamp(round(3 * time_expected_s / 60), 8, 45)
 *
 * @param {number} liters - Tank volume in liters
 * @param {number} watts - Heater/cooler power in watts
 * @param {number} stepC - Temperature step in °C (fault threshold)
 * @param {number} margin - Safety margin multiplier (default 3)
 * @returns {number|null} - Stall timeout in minutes, clamped [8, 45], or null if invalid inputs
 */
export function physicalStallMin(liters, watts, stepC, margin = 3) {
  if (liters <= 0 || watts <= 0) {
    return null;
  }

  const SPECIFIC_HEAT = 4186; // J/kg/K (water)
  const MIN_TIMEOUT = 8;
  const MAX_TIMEOUT = 45;

  const timeExpectedSeconds = (liters * SPECIFIC_HEAT * stepC) / watts;
  const stallMinutes = margin * timeExpectedSeconds / 60;

  return clamp(Math.round(stallMinutes), MIN_TIMEOUT, MAX_TIMEOUT);
}
