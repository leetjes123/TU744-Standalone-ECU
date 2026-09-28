#include "lifecycle.h"
#include "oem_runtime.h"
#include "faults.h"
#include <string.h>
PowerState power;
u16 reset_capture_raw, reset_capture_marker;

void lifecycle_init(void) {
    memset(&power, 0, sizeof(power));
    memset(&wideband, 0, sizeof(wideband));
    memset(&vehicle, 0, sizeof(vehicle));
    oem_runtime_reset();
}

static void stop_outputs(u32 now) {
    ecu.key_input = 0;
    ecu.control.key_on = 0;
    ecu.control.prime_until = now;
    ecu.control.pump = ecu.control.boost = 0;
    safety_inhibit(INH_POWER, now);
    launch_disarm();
    iac_disable();
    hal_aux(0, ecu.control.fan, 0, ecu.control.gauge, 0, 0);
}

void power_poll(u32 now) {
    u8 input;
    Rotation rotation;
    faults_poll(now);
    if (!power.sampled || now - power.sample_at >= 10UL) {
        power.sample_at = now;
        power.sampled = 1;
        input = hal_run_permission();
        if (input > 1U) {
            power.observation_valid = 0;
            power.history = power.samples = 0;
            /* Missing acquisition revokes permission without pretending the
               observation proves a key-off or authorizes power removal. */
            stop_outputs(now);
            power.release_blocked = 1;
            return;
        }
        power.history = (u8)(((power.history << 1) | input) & 7U);
        if (power.samples < 3U)
            power.samples++;
        if (power.samples >= 3U) {
            power.observation_valid = (u8)(power.history == 0U || power.history == 7U);
            if (power.history == 7U && power.state <= POWER_RUN) {
                power.state = POWER_RUN;
                power.release_blocked = 0;
                ecu.key_input = 1;
            } else if (power.history == 0U &&
                       (power.state == POWER_BOOT || power.state == POWER_RUN)) {
                power.state = POWER_DRAIN;
                power.off_at = now;
                cal_abort(); /* Discard only volatile, uncommitted tool edits. */
                stop_outputs(now);
                oem_runtime_stop();
            } else if (power.history == 7U && power.state >= POWER_DRAIN &&
                       power.state != POWER_RELEASED && power.state != POWER_RESET_REQUESTED) {
                /* Native alternate-mode reassertion requests SRST, including
                   while rotation continues. Drain existing writes and outputs;
                   do not wait for rotation to stop or begin another save. */
                power.state = POWER_RESTART_REQUIRED;
                power.release_blocked = 1;
            }
        }
    }
    if (power.state < POWER_DRAIN || power.state == POWER_RELEASED ||
        power.state == POWER_RESET_REQUESTED)
        return;
    /* A client can reconnect while draining; it cannot hold restart hostage. */
    cal_abort();
    /* No new engine plans/IAC motion during drain, including invalid tune. */
    ecu.key_input = 0;
    if (ecu.iac.off_pending)
        iac_disable();
    if (power.state == POWER_RESTART_REQUIRED) {
        if (power.observation_valid && power.history == 7U &&
            !ecu.authority.coil_active[0] && !ecu.authority.coil_active[1] &&
            !ecu.authority.injector_active[0] && !ecu.authority.injector_active[1] &&
            !ecu.authority.injector_active[2] && !ecu.authority.injector_active[3] &&
            !ecu.iac.off_pending && !ecu.storage.phase && !ecu.storage_owner &&
            !hal_eeprom_busy()) {
            power.state = POWER_RESET_REQUESTED;
            hal_system_reset();
        }
        return;
    }
    rotation_snapshot(&rotation);
    if (rotation.state != ROT_UNSYNCED || rotation.rpm || ecu.iac.off_pending ||
        ecu.storage.phase || ecu.storage_owner || ecu.cal.staging)
        return;
    /* Save the standalone records without entering service or inhibiting a
       running engine. This point is already in stopped key-off ownership. */
    if (faults.dirty) {
        if ((!faults.attempted || faults.result == 1U) && faults_save(now))
            faults.attempted = 1;
        power.release_blocked = 1;
        return;
    }
    if (!power.save_attempted) {
        if (!oem_runtime.booted || !oem_runtime.history.ready) {
            power.release_blocked = 1;
            return;
        }
        if (oem_runtime.history.dirty) {
            if (!oem_runtime_save(now))
                return;
            power.save_attempted = 1;
            if (power.state != POWER_RESTART_REQUIRED)
                power.state = POWER_SAVE;
            return;
        }
        power.save_attempted = 1;
    }
    if (!oem_runtime_settled()) {
        power.release_blocked = 1;
        return;
    }
    power.state = POWER_HELD;
    /* Supplied training PDFp13 specifies at least15s of power latch and
       temperature-dependent extension. This standalone policy waits for the
       fan to finish, valid coolant, quiet actuators, and durable fault history.
       Calibration RAM edits still require an explicit save; no implicit tune
       commit is introduced at key-off. */
    if (!power.observation_valid || power.history ||
        now - power.off_at < 15000UL || ecu.control.fan ||
        !ecu.cal.valid || ecu.sensors.clt.quality != QUALITY_VALID ||
        hal_eeprom_busy())
        return;
    if (hal_power_release()) {
        power.state = POWER_RELEASED;
        power.release_blocked = 0;
    } else
        power.release_blocked = 1;
}
