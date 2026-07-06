#ifndef CONFIG_H
#define CONFIG_H

// --- Number of active connected pumps (can be dynamically configured up to 16) ---
#define PUMP_COUNT                7

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

// --- Bulk fill fraction (85% of adjusted target before settle) ---
#define BULK_FILL_FRACTION        0.85f

// --- Settle timeout for isScaleSettled() check ---
#define SETTLE_TIMEOUT_MS         500

// --- Mixer durations and timing config ---
#define MIX_BETWEEN_DURATION_MS   180000UL  // 3 minutes between pump cycles
#define MIX_FINAL_DURATION_MS     300000UL  // 5 minutes at end of recipe
#define MIXER_SETTLE_GUARD_MS     15000UL   // 15 seconds guard time after mixer stops
#define MIXER_SETTLE_WINDOW_MS    3000UL    // 3 seconds stability check window
#define MIXER_SETTLE_TOLERANCE_G  0.01f     // 0.01g tolerance for mixer settle check

// --- Maintenance baseline timings and speeds ---
#define BASE_BULK_SPEED           10000.0f
#define BASE_TRICKLE_SPEED        12000.0f
#define LOW_VISC_PRIME_TIME_SEC   2UL      // Min prime time (water endpoint)
#define HIGH_VISC_PRIME_TIME_SEC  5UL      // Max prime time (glycerol endpoint)
#define PRIME_REF_LOW_VISC_CPS    1.0f     // Water reference for log10 prime scaling
#define PRIME_REF_HIGH_VISC_CPS   1000.0f  // Glycerol reference for log10 prime scaling
#define BASE_FLUSH_TIME_SEC       15UL      // 15 seconds baseline flush time

// --- Max scale settle timeout (for watchdog) ---
#define MAX_SETTLE_TIMEOUT_MS     30000

// --- Max single-pump dispense timeout (timer restarts for each pump) ---
#define MAX_DISPENSE_TIMEOUT_MS   120000

// --- Calibration run: fixed step count ---
#define CALIBRATION_RUN_STEPS     10000L

// --- Mixer continuous rotation target (effectively infinite) ---
#define MIXER_CONTINUOUS_STEPS    1000000000L

// --- Max steps per pump (safety limit) ---
#define MAX_DISPENSE_STEPS        500000000L

#endif // CONFIG_H
