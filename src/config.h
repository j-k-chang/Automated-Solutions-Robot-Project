#ifndef CONFIG_H
#define CONFIG_H

// --- Number of active connected pumps (can be dynamically configured up to 16) ---
#define PUMP_COUNT                8

// --- Max pump count supported by arrays ---
#define MAX_PUMP_COUNT            16

// --- Pump Specific STEP Pins ---
#define PUMP1_STEP                53
#define PUMP2_STEP                51
#define PUMP3_STEP                49
#define PUMP4_STEP                47
#define PUMP5_STEP                45
#define PUMP6_STEP                43
#define PUMP7_STEP                41

#define PUMP8_STEP                39
#define PUMP9_STEP                37
#define PUMP10_STEP               35
#define PUMP11_STEP               33
#define PUMP12_STEP               31

// Placeholder pins for extra channels (13-16) - set to -1
#define PUMP13_STEP               -1
#define PUMP14_STEP               -1
#define PUMP15_STEP               -1
#define PUMP16_STEP               -1

// --- Settle timeout for isScaleSettled() check ---
#define SETTLE_TIMEOUT_MS         500
#define TRIM_SETTLE_TIMEOUT_MS    200   // Faster stability window during trim
#define BULK_SETTLE_TOLERANCE_G   0.010f // Match DISPENSE_TOLERANCE_G
#define TRIM_SETTLE_TOLERANCE_G   0.005f

// --- Adaptive trim settle guards (ms, low-viscosity water) ---
#define TRIM_GUARD_LARGE_MS       250
#define TRIM_GUARD_MED_MS         400
#define TRIM_GUARD_FINE_MS        900   // Match DROP_CAL settle; avoid stacking fine pulses
#define BULK_SETTLE_GUARD_MS      350

// --- Mixer durations and timing config ---
#define MIX_BETWEEN_DURATION_MS   180000UL  // 3 minutes between pump cycles
#define MIX_FINAL_DURATION_MS     300000UL  // 5 minutes at end of recipe
#define MIXER_SETTLE_GUARD_MS     15000UL   // 15 seconds guard time after mixer stops
#define MIXER_SETTLE_WINDOW_MS    3000UL    // 3 seconds stability check window
#define MIXER_SETTLE_TOLERANCE_G  0.01f     // 0.01g tolerance for mixer settle check

// --- Maintenance baseline timings ---
#define LOW_VISC_PRIME_TIME_SEC   10UL      // Min prime time (water endpoint)
#define HIGH_VISC_PRIME_TIME_SEC  13UL      // Max prime time (glycerol endpoint)
#define PRIME_REF_LOW_VISC_CPS    1.0f     // Water reference for log10 prime scaling
#define PRIME_REF_HIGH_VISC_CPS   1000.0f  // Glycerol reference for log10 prime scaling
#define BASE_FLUSH_TIME_SEC       15UL      // 15 seconds baseline flush time

// --- Max scale settle timeout (for watchdog) ---
#define MAX_SETTLE_TIMEOUT_MS     30000

// --- Max single-pump dispense timeout (timer restarts for each pump) ---
#define MAX_DISPENSE_TIMEOUT_MS   600000  // 10 min/pump (large targets e.g. 100g)

// --- Dispense stop-lead (grams of fluid inertia compensation) ---
// Endpoint values interpolated by log10 viscosity (PRIME_REF_*_VISC_CPS).
#define LOW_VISC_STOP_LEAD_G        0.01f   // Bulk fill, water endpoint
#define HIGH_VISC_STOP_LEAD_G       0.10f   // Bulk fill, glycerol endpoint
#define LOW_VISC_TRIM_STOP_LEAD_G   0.005f  // Trim pulse, water endpoint
#define HIGH_VISC_TRIM_STOP_LEAD_G  0.02f   // Trim pulse, glycerol endpoint
#define SCALE_LAG_SEC               0.15f   // Predictive stop: trim-phase scale serial lag
#define BULK_WEIGHT_STOP_MIN_STEP_FRAC    0.92f // Weight stop only near end of bulk
#define BULK_LEARN_MIN_STEP_FRAC    0.75f
// Step undercut so post-stop in-flight mass settles under target for trim.
#define BULK_SETTLE_UNDERCUT_LOW_G  0.55f
#define BULK_SETTLE_UNDERCUT_HIGH_G 1.20f
// Large doses: high fill-rate leaves ~2g+ in flight; use a fixed bulk margin.
#define BULK_LARGE_TARGET_G         30.0f
#define BULK_LARGE_MARGIN_G         5.0f
#define BULK_MIN_TRIM_MARGIN_LOW_G  0.50f
#define BULK_MIN_TRIM_MARGIN_HIGH_G 1.5f
#define BULK_MIN_EFFECTIVE_FRAC     0.45f   // Floor for bulk step grams vs target
#define TRIM_SPEED_FINAL            4500.0f
#define TRIM_BACKLOG_WAIT_MS        700UL
#define TRIM_BACKLOG_WAIT_NEAR_MS   900UL   // Hang wait near target (was 450 — too fast)
#define TRIM_FINISH_CONFIRM_MS      1200UL  // Tip settle before suck-back after lower band
#define TRIM_FINISH_CONFIRM_CYCLES  3
#define MAX_TRIM_PULSES_PER_PUMP    6
#define MAX_TRIM_CATCHUP_PULSES     8
#define MAX_TRIM_COARSE_PULSES      12      // Large residual after bulk margin; separate from fine budget
#define MAX_BACKLOG_WAIT_CYCLES     3
#define TRIM_COARSE_REMAIN_G        1.50f   // Above this: allow coarse pulse size
#define TRIM_PULSE_MAX_COARSE_G     1.50f
#define TRIM_FINE_ENTRY_G           1.00f   // At/below: use fine pulse budget (small-dose path)
#define TRIM_PULSE_MAX_LARGE_G      0.55f
#define TRIM_PULSE_MAX_MED_G        0.30f
#define TRIM_PULSE_MAX_SMALL_G      0.10f
// Drop mass from pump-1 DROP CAL (mean 0.0225g); used for all pumps until per-pump KV persist.
#define DROP_MASS_DEFAULT_G         0.0225f
#define TRIM_HALF_DROP_G            0.011f   // 0.5 × DROP_MASS_DEFAULT_G
#define TRIM_NEAR_BAND_G            0.06f    // Hard settle/land gate below this remaining
#define TRIM_INCHWORM_REMAIN_G      (2.0f * DROP_MASS_DEFAULT_G)  // ~0.045g → 64 µstep bursts
#define TRIM_INCHWORM_MAX_BURSTS    48
#define TRIM_PULSE_NEAR_G           DROP_MASS_DEFAULT_G  // ≤1 drop when remaining ~0.03–0.08g
#define TRIM_PULSE_MAX_FINE_G       0.010f   // ~0.45 × drop — final pulses cannot stack a full drop
#define TRIM_PULSE_CATCHUP_MAX_G    TRIM_HALF_DROP_G
#define BACKLOG_ACCUM_MAX_G         0.060f
#define BACKLOG_FINISH_MAX_G        0.010f
#define TRIM_CATCHUP_HEADROOM_G     TRIM_HALF_DROP_G  // Leave ≥½ drop unclaimed vs remaining
#define TRIM_GUARD_NEAR_MS          900UL    // Min motor-stop→decide guard in near band
#define TRIM_SETTLE_NEAR_MS         350UL    // Stability window in near band

// --- Drop characterization (inchworm bursts at trim microstepping) ---
#define DROP_CAL_BURST_STEPS        64L     // Microsteps per burst (~1 full step @ 1/64)
#define DROP_CAL_SPEED              3500.0f
#define DROP_CAL_SETTLE_MS          800UL
#define DROP_CAL_DETECT_G           0.018f  // ≥2 scale LSB @ 0.01g resolution
#define DROP_CAL_MAX_DROP_G         0.100f  // Reject stream/priming dumps
#define DROP_CAL_STABLE_G           0.006f  // Wait while scale is still drifting
#define DROP_CAL_DEFAULT_COUNT      15
#define DROP_CAL_MAX_EVENTS         32
#define DROP_CAL_MAX_TOTAL_STEPS    400000L

// --- Calibration run: fixed microstep count at bulk (1/8) resolution ---
#define CALIBRATION_RUN_STEPS     50000L
#define CALIBRATION_MICROSTEPS    8

// --- Mixer continuous rotation target (effectively infinite) ---
#define MIXER_CONTINUOUS_STEPS    1000000000L

// --- Max steps per pump (safety limit) ---
#define MAX_DISPENSE_STEPS        500000000L

#endif // CONFIG_H
