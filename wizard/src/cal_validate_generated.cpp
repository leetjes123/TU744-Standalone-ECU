// Generated from src/calibration.c; one issue for each failed firmware predicate.
#include "calibration_safety.h"
#include <cstdint>
#include <cstdio>
using u8 = uint8_t; using s8 = int8_t; using u16 = uint16_t;
using s16 = int16_t; using u32 = uint32_t;
static u16 get16(const u8* p) { return (p[0]<<8)|p[1]; }
#define CAL_DTC_ENABLE 0x940U
#define CAL_DTC_ENABLE_BYTES 14U
#define CAL_DTC_FAIL_COUNT 0x94EU
#define CAL_DTC_PASS_COUNT 0x94FU
#define CAL_DTC_TRIM_MS 0x950U
#define CAL_DTC_O2_ACTIVITY_MS 0x952U
#define CAL_DTC_O2_SLOW_MS 0x954U
#define CAL_DTC_MISFIRE_PERCENT 0x956U
#define CAL_DTC_MISFIRE_COUNT 0x957U
#define CAL_DTC_PHASE_TIMEOUT_MS 0x958U
#define CAL_DTC_OUTPUT_FAIL_COUNT 0x95AU
#define CAL_DTC_VSS_MAX_KPH 0x95BU
#define CAL_DTC_PHASE_MIN_TICKS 0x95CU
#define CAL_DTC_PHASE_MAX_TICKS 0x95EU
#define CAL_DTC_PHASE_DELTA_TICKS 0x960U
#define CAL_DTC_PHASE_POLARITY 0x962U
#define CAL_DTC_TPS_SLEW 0x964U /* tenths-percent per second */
#define CAL_DTC_MAP_SLEW 0x966U /* kPa per second */
#define CAL_DTC_TEMP_SLEW 0x968U /* degrees C per second */
#define CAL_DTC_BATTERY_SLEW 0x96AU /* mV per second */
#define CAL_DTC_SUBTYPE_ENABLE 0x980U /* 107 low-nibble masks: 1/2/4/8. */
#define CAL_SCHEMA 4U
#define CAL_MAGIC 0x900U
#define CAL_RUN_RPM 0x904U
#define CAL_CRANK_RPM 0x906U
#define CAL_RUN_MS 0x908U
#define CAL_AE_DECAY 0x90AU
#define CAL_STFT_KI 0x90CU
#define CAL_MAX_MAP 0x90EU
#define CAL_IAC_MAX 0x910U
#define CAL_STOICH 0x912U
#define CAL_VSS_PPM 0x914U
#define CAL_FLAGS 0x916U
#define CAL_INJ_PHASE 0x918U
#define CAL_CRANK_ADV 0x91AU
#define CAL_HOME_MS 0x91CU
#define CAL_MIN_BAT 0x91EU
#define CAL_SENSOR_AGE 0x920U
#define CAL_PLAN_AGE 0x922U
#define CAL_IDLE_STEP_MS 0x924U
#define CAL_IAC_HOME_STEPS 0x93CU
#define IAC_HOME_STEPS_DEFAULT 250U
#define IAC_HOME_MS_PER_STEP 12U /* 10 ms step cadence plus scheduling margin */
#define CFG_ALPHA_N 0x01U
#define CFG_STFT 0x20U
#define CFG_DFCO 0x02U
#define CFG_BOOST 0x10U
#define CFG_LAUNCH 0x04U
#define EQUIP_UPSTREAM_RELAY_HEATER 0x01U
#define EQUIP_DOWNSTREAM_RELAY_HEATER 0x02U
#define CAL_TRANSACTION_MS 5000UL
#define CAL_WB_POLICY 0x926U
#define CAL_WB_WARM_MS 0x928U
#define CAL_WB_GOOD_MS 0x92AU
#define CAL_WB_MIN_MV 0x92CU
#define CAL_WB_MAX_MV 0x92EU
#define CAL_LAUNCH_MS 0x930U
#define CAL_LAUNCH_SOFT_RPM 0x932U
#define CAL_LAUNCH_SOFT_PERCENT 0x934U
#define CAL_ANTILAG_MS 0x936U
#define CAL_ANTILAG_CLT 0x938U
#define CAL_ANTILAG_IAT 0x939U
#define CAL_DWELL_FEEDBACK 0x93AU /* 0=fixed calibrated dwell, 1=qualified CC9 */
std::vector<CalibrationIssue> ValidateCalibration(const CalBuffer& cal) {
    std::vector<CalibrationIssue> issues;
    if (!cal.loaded) { issues.push_back({IssueSeverity::Error, "Load a calibration first", "unloaded"}); return issues; }
    const u8* c = cal.data;
    auto report = [&](int offset) {
        char id[32]; snprintf(id, sizeof(id), "rule-%03X", offset);
        std::string message = "Firmware validation failed at " + std::string(id + 5);
        CalibrationIssue issue{IssueSeverity::Error, message, id};
        for (int i=0; i<NUM_SCALARS; ++i) if (ALL_SCALARS[i].offset == offset) {
            issue.target=IssueTarget::Scalar; issue.targetIndex=i;
            issue.message += ": " + std::string(ALL_SCALARS[i].name);
            issue.allowed=ALL_SCALARS[i].description;
        }
        issues.push_back(issue);
    };

    u16 i, a, b, base;
    s16 sa, sb;
    if (c[CAL_MAGIC] != 'L' || c[CAL_MAGIC + 1] != 'R' || get16(c + CAL_MAGIC + 2) != CAL_SCHEMA)
        report(CAL_MAGIC);
    for (base = 0x400; base <= 0x460; base += 0x20) {
        for (i = 0; i < 16; i++) {
            a = get16(c + base + 2 * i);
            if (base == 0x460) {
                sa = (s16)a;
                if (sa < -40 || sa > 150)
                    report((u16)(base + 2 * i));
                if (i && sa <= (s16)get16(c + base + 2 * i - 2))
                    report(base);
            } else {
                b = base == 0x400 ? 12000U : (base == 0x420 ? 600U : 100U);
                if (a > b || (i && a <= get16(c + base + 2 * i - 2)))
                    report(base);
            }
        }
    }
    for (i = 0; i < 256; i++) {
        if (c[0x100 + i] > 140)
            report((u16)(0x100 + i)); /* -20..50 deg */
        if (c[0x200 + i] < 70 || c[0x200 + i] > 220)
            report((u16)(0x200 + i));
        if (c[0x300 + i] > 100)
            report((u16)(0x300 + i));
    }
    for (i = 0; i < 16; i++) {
        if (get16(c + 0x490 + 2 * i) > 1000 || c[0x4B0 + i] < 100 ||
            get16(c + 0x4D0 + 2 * i) < 500 ||
            get16(c + 0x4D0 + 2 * i) > 2500)
            report(0x490);
        if (c[0x4F0 + i] > get16(c + CAL_IAC_MAX) || c[0x500 + i] > get16(c + CAL_IAC_MAX) ||
            c[0x510 + i] > 60 || c[0x5F0 + i] > 100)
            report(0x4F0);
        if (get16(c + 0x594 + 2 * i) > 600 ||
            (i && get16(c + 0x594 + 2 * i) < get16(c + 0x592 + 2 * i)))
            report(0x594);
    }
    for (i = 0; i < 8; i++) {
        a = get16(c + 0x530 + 2 * i);
        b = get16(c + 0x540 + 2 * i);
        if (a < 500 || a > 6000 || b > 5000)
            report(0x530);
        sa = (s16)get16(c + 0x610 + 2 * i);
        if (sa < -3000 || sa > 3000 || (i && sa <= (s16)get16(c + 0x60E + 2 * i)))
            report(0x610);
        if (c[0x620 + i] > 80)
            report(0x620);
        if (get16(c + 0x740 + 2 * i) > 10000 ||
            (i && get16(c + 0x740 + 2 * i) <= get16(c + 0x73E + 2 * i)))
            report(0x740);
        if (get16(c + 0x789 + 2 * i) > 12000 ||
            (i && get16(c + 0x789 + 2 * i) <= get16(c + 0x787 + 2 * i)))
            report(0x789);
    }
    for (base = 0x550; base <= 0x572; base += 0x22) {
        if (!get16(c + base) || get16(c + base) > 20000)
            report(base);
        for (i = 0; i < 16; i++) {
            a = get16(c + base + 2 + 2 * i);
            if (!a || (i && a >= get16(c + base + 2 * i)))
                report(base);
        }
    }
    for (i = 0; i < 6; i++) {
        if (c[0x628 + i] > 240)
            report(0x628);
        if (c[0x750 + i] > 100 || (i && c[0x750 + i] <= c[0x74F + i]))
            report(0x750);
    }
    for (i = 0; i < 48; i++)
        if (c[0x759 + i] < 100)
            report((u16)(0x759 + i));
    if (c[0x5D4] & 0xC8U || c[0x5D8] > 2 || c[0x5E4] != 0 || c[0x600] > 1)
        report(0x5D4);
    if ((s8)c[0x5E2] < 70 || (s8)c[0x5E2] > 120 || (s8)c[0x5E3] >= (s8)c[0x5E2])
        report(0x5E2);
    a = get16(c + 0x5DB);
    b = get16(c + 0x5DD);
    if (a < 1500 || a > 10000 || b < 1000 || b >= a || get16(c + 0x5DF) > 20000 ||
        !get16(c + 0x5DF))
        report(0x5DB);
    if (get16(c + 0x7B0) >= get16(c + 0x7B2) || get16(c + 0x7B2) > 1023 ||
        get16(c + 0x7B2) - get16(c + 0x7B0) < 100)
        report(0x7B0);
    if (c[0x8CF] < 10 || c[0x8CF] > 95 || c[0x8D0] > 1 || c[0x5E6] > 20 || c[0x5EC] > 100)
        report(0x8CF);
    if (c[0x731] < 128 || c[0x731] > 192 || c[0x732] > 128 || c[0x732] < 64 || !c[0x730])
        report(0x730);
    if (c[0x8C3] >= c[0x8C4] || c[0x8C4] > 200 || !c[0x8C2] || c[0x8C2] > 16 || c[0x8C8] > c[0x8C9])
        report(0x8C3);
    if (c[0x601] < 50 || c[0x602] > 250 || c[0x601] >= c[0x602])
        report(0x601);
    if (get16(c + 0x630) > 10000 || get16(c + 0x632) > 10000 || get16(c + 0x634) != 0)
        report(0x630); /* PI: no unqualified D term */
    if (get16(c + CAL_CRANK_RPM) < 100 || get16(c + CAL_RUN_RPM) <= get16(c + CAL_CRANK_RPM) ||
        get16(c + CAL_RUN_RPM) > 2000)
        report(CAL_RUN_RPM);
    if (get16(c + CAL_RUN_MS) < 100 || get16(c + CAL_RUN_MS) > 5000)
        report(CAL_RUN_MS);
    if (get16(c + CAL_AE_DECAY) < 10 || get16(c + CAL_AE_DECAY) > 5000 ||
        get16(c + CAL_STFT_KI) > 2000)
        report(CAL_AE_DECAY);
    if (get16(c + CAL_MAX_MAP) < 100 || get16(c + CAL_MAX_MAP) > 600 || !get16(c + CAL_IAC_MAX) ||
        get16(c + CAL_IAC_MAX) > 220)
        report(CAL_MAX_MAP);
    if (get16(c + CAL_STOICH) < 70 || get16(c + CAL_STOICH) > 220 || !get16(c + CAL_VSS_PPM) ||
        get16(c + CAL_VSS_PPM) > 100)
        report(CAL_STOICH);
    /* Both paired events must land on present teeth: n and n+30, n <= 27. */
    if (c[CAL_FLAGS] & 0xFCU || get16(c + 0x605) > 3599 || get16(c + CAL_INJ_PHASE) > 1620 ||
        get16(c + CAL_INJ_PHASE) % 60U)
        report(CAL_INJ_PHASE);
    if (c[CAL_DWELL_FEEDBACK] > 1U)
        report(CAL_DWELL_FEEDBACK);
    if ((s16)get16(c + CAL_CRANK_ADV) < -100 || (s16)get16(c + CAL_CRANK_ADV) > 200 ||
        get16(c + CAL_HOME_MS) < 3000 || get16(c + CAL_HOME_MS) > 10000)
        report(CAL_HOME_MS);
    if (get16(c + CAL_MIN_BAT) < 5000 || get16(c + CAL_MIN_BAT) > 12000 ||
        get16(c + CAL_SENSOR_AGE) < 20 || get16(c + CAL_SENSOR_AGE) > 100 ||
        get16(c + CAL_PLAN_AGE) < 20 || get16(c + CAL_PLAN_AGE) > 50)
        report(CAL_MIN_BAT);
    if (c[0x7B7] > 100 || get16(c + 0x7B5) >= get16(c + 0x5DB) ||
        ((c[0x7B4] & 0x80U) && !(c[0x7B4] & 0x40U)))
        report(0x7B4);
    if ((c[0x7B4] & 0x40U) &&
        (!(c[0x5D4] & CFG_LAUNCH) || !get16(c + CAL_LAUNCH_MS) ||
         get16(c + CAL_ANTILAG_MS) < 100U || get16(c + CAL_ANTILAG_MS) > 3000U ||
         c[CAL_ANTILAG_CLT] < 60U || c[CAL_ANTILAG_CLT] > 110U ||
         c[CAL_ANTILAG_IAT] > 80U || c[0x7B8] > 140U || !c[0x7B9]))
        report(CAL_ANTILAG_MS);
    if (get16(c + 0x7BA) > get16(c + 0x7BC) || get16(c + 0x7BC) > 12000 || c[0x7BF] > 100 ||
        c[0x8C5] > 100)
        report(0x7BA);
    if ((c[0x5D4] & CFG_LAUNCH) &&
        (!c[0x7AF] || get16(c + 0x5E7) < 1500 || get16(c + 0x5E7) > get16(c + 0x5DB)))
        report(0x5E7);
    if (get16(c + 0x5E9) > 10000 || !c[0x758] || c[0x758] > 50 || c[0x757])
        report(0x5E9);
    if (c[0x5D9] != 5 || c[0x5DA] != 6)
        report(0x5D9); /* fixed development IAC waveform */
    if (get16(c + CAL_IDLE_STEP_MS) < 20 || get16(c + CAL_IDLE_STEP_MS) > 1000)
        report(CAL_IDLE_STEP_MS);
    /* Homing must cover the full travel and finish inside its timeout. */
    a = get16(c + CAL_IAC_HOME_STEPS);
    if (a && (a < get16(c + CAL_IAC_MAX) || a > 800U))
        report(CAL_IAC_HOME_STEPS);
    if ((u32)(a ? a : IAC_HOME_STEPS_DEFAULT) * IAC_HOME_MS_PER_STEP > get16(c + CAL_HOME_MS))
        report(CAL_IAC_HOME_STEPS);
    if (c[CAL_WB_POLICY] > 1U || c[CAL_WB_POLICY + 1U])
        report(CAL_WB_POLICY);
    if (c[0x600] && !(c[CAL_FLAGS] & EQUIP_UPSTREAM_RELAY_HEATER))
        report(CAL_FLAGS);
    if (c[CAL_WB_POLICY] &&
        (get16(c + CAL_WB_WARM_MS) < 10000U || get16(c + CAL_WB_WARM_MS) > 60000U ||
         get16(c + CAL_WB_GOOD_MS) < 500U || get16(c + CAL_WB_GOOD_MS) > 10000U ||
         get16(c + CAL_WB_MIN_MV) < 10U || get16(c + CAL_WB_MAX_MV) > 4990U ||
         get16(c + CAL_WB_MIN_MV) >= get16(c + CAL_WB_MAX_MV)))
        report(CAL_WB_WARM_MS);
    if (get16(c + CAL_LAUNCH_MS) > 30000U ||
        (get16(c + CAL_LAUNCH_MS) && get16(c + CAL_LAUNCH_MS) < 1000U) ||
        c[CAL_LAUNCH_SOFT_PERCENT] > 100U ||
        (get16(c + CAL_LAUNCH_SOFT_RPM) &&
         (get16(c + CAL_LAUNCH_SOFT_RPM) < 1000U ||
          get16(c + CAL_LAUNCH_SOFT_RPM) >= get16(c + 0x5E7))))
        report(CAL_LAUNCH_MS);
     if ((c[CAL_DTC_ENABLE + 13U] & 0xF8U) ||
        c[CAL_DTC_FAIL_COUNT] > 100U || c[CAL_DTC_PASS_COUNT] > 100U ||
        (get16(c + CAL_DTC_TRIM_MS) &&
         (get16(c + CAL_DTC_TRIM_MS) < 1000U || get16(c + CAL_DTC_TRIM_MS) > 60000U)) ||
        (get16(c + CAL_DTC_O2_ACTIVITY_MS) &&
         (get16(c + CAL_DTC_O2_ACTIVITY_MS) < 1000U ||
          get16(c + CAL_DTC_O2_ACTIVITY_MS) > 30000U)) ||
        (get16(c + CAL_DTC_O2_SLOW_MS) &&
         (get16(c + CAL_DTC_O2_SLOW_MS) < 100U || get16(c + CAL_DTC_O2_SLOW_MS) > 10000U)) ||
         c[CAL_DTC_MISFIRE_PERCENT] > 100U || c[CAL_DTC_MISFIRE_COUNT] > 100U ||
         get16(c + CAL_DTC_PHASE_TIMEOUT_MS) > 10000U || c[CAL_DTC_OUTPUT_FAIL_COUNT] > 100U ||
         c[CAL_DTC_PHASE_POLARITY] > 1U)
         report(CAL_DTC_ENABLE);
    for (i = 0; i < 107U; i++)
        if (c[CAL_DTC_SUBTYPE_ENABLE + i] & 0xF0U)
            report((u16)(CAL_DTC_SUBTYPE_ENABLE + i));
    if ((get16(c + CAL_DTC_PHASE_MIN_TICKS) || get16(c + CAL_DTC_PHASE_MAX_TICKS) ||
         get16(c + CAL_DTC_PHASE_DELTA_TICKS)) &&
        (get16(c + CAL_DTC_PHASE_MIN_TICKS) >= get16(c + CAL_DTC_PHASE_MAX_TICKS) ||
         !get16(c + CAL_DTC_PHASE_DELTA_TICKS) ||
         get16(c + CAL_DTC_PHASE_DELTA_TICKS) >= get16(c + CAL_DTC_PHASE_MAX_TICKS)))
        report(CAL_DTC_PHASE_MIN_TICKS);
    for (i = 0; i < 5; i++)
        if (get16(c + 0x7A3 + 2 * i) < 10 || get16(c + 0x7A3 + 2 * i) > 1000)
            report(0x7A3);
    if (get16(c + 0x7A1) < 10 || get16(c + 0x7A1) > 1000 || get16(c + 0x7AD) < 500 ||
        get16(c + 0x7AD) > 4000)
        report(0x7A1);
    sb = 0;
    return issues;
}
const CalibrationRange STRUCTURAL_RANGES[] = {
    {0x460, 0x480}, {0x550, 0x5B4}, {0x5D4, 0x5D5}, {0x5E4, 0x5E5},
    {0x600, 0x603}, {0x605, 0x607}, {0x7A1, 0x7AF}, {0x7B0, 0x7B4}, {0x900, CAL_SIZE}};
const int NUM_STRUCTURAL_RANGES = sizeof(STRUCTURAL_RANGES)/sizeof(CalibrationRange);
bool IsStructuralOffset(int offset) {
    for (const auto& r : STRUCTURAL_RANGES) if (offset >= r.start && offset < r.end) return true;
    return false;
}
