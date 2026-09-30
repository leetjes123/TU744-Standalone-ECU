#include "board.h"
#include "lifecycle.h"
#include "diagnostic_monitors.h"
/* Nominal OEM half-turn PEC blocks, ending as the ROM's on teeth 1 and 31
   (sub_68678: 30 captures, then 28 across the gap). Acquisition and slow
   rotation retain one-capture delivery.
   Segment-zero buffers are required by the C167 PEC address registers.
   The linker reserves F600..F7FF for these, below the F800 system stack. */
static volatile u16 sdata capture_buffer[3][30];
static volatile u16 SYSTEM_RAM block_count[3], block_counter[3];
static volatile u32 SYSTEM_RAM block_stamp[3];
static volatile u8 SYSTEM_RAM capture_head, capture_tail, capture_failed, batch_enabled, capture_started,
                                capture_resync;
static volatile u16 SYSTEM_RAM phase_counter, previous_counter, previous_capture, previous_period;
static volatile u8 SYSTEM_RAM phase_tooth, phase_valid, block_tooth[3];
volatile u16 capture_schedule_counter, capture_overruns, capture_blocks;

static void capture_arm(u8 slot, u16 count) {
    block_count[slot] = count;
    DSTP6 = (u16)&capture_buffer[slot][0];
    PECC6 = 0x0200U | count;
}
void board_capture_init(void) {
    capture_head = capture_tail = capture_failed = batch_enabled = 0;
    capture_started = capture_resync = 0;
    capture_overruns = capture_blocks = 0;
    previous_counter = T0;
    previous_capture = previous_period = 0;
    SRCP6 = 0xFE9EU; /* CC15 */
    PECC7 = 0; /* T1 overflow shares level 15, group 3; never a PEC transfer. */
    capture_arm(0, 1);
}
/* Stable latest pair even at a buffer boundary or before a pending PEC word
   is copied. Called by board_capture_snapshot under hal_hard_lock. */
u16 board_recent_period(u16 *captured) {
    u16 base = (u16)&capture_buffer[capture_head][0], dest = DSTP6;
    u16 last, before;
    if (dest == base) { last = previous_capture; before = last - previous_period; }
    else {
        last = *((u16 volatile sdata *)(dest - 2U));
        before = dest == (u16)(base + 2U) ? previous_capture :
                 *((u16 volatile sdata *)(dest - 4U));
    }
    if (CC15IR && CC15 != last) { before = last; last = CC15; }
    *captured = last;
    return (u16)(last - before);
}
u8 board_capture_snapshot(u16 *counter, u16 *captured, u16 *period) {
    u8 attempt, result = 0;
    u16 lock = hal_hard_lock(); /* PEC and the level-15 handler move these words */
    /* Masking CPU interrupts does not stop T0 or CC15. Reject a torn pair,
       and retry only a bounded number of times before the caller skips or
       finishes the event against its retained time deadline. */
    for (attempt = 0; attempt < 3U; attempt++) {
        *counter = T0;
        *period = board_recent_period(captured);
        if (*counter == T0) { result = 1; break; }
    }
    hal_hard_unlock(lock);
    return result;
}
static void capture_failure_snapshot(u16 reason, u16 counter, u16 expected,
                                     u16 captured, u8 slot, u8 next, u16 count) {
    u8 i;
    if (timing_health.capture_reason) return;
    timing_health.capture_counter = counter;
    timing_health.capture_expected = expected;
    timing_health.capture_last = captured;
    timing_health.capture_latest = CC15;
    timing_health.capture_irq = CC15IC;
    timing_health.capture_pec = PECC6;
    timing_health.capture_dest = DSTP6;
    timing_health.capture_head = slot;
    timing_health.capture_tail = capture_tail;
    timing_health.capture_next = next;
    timing_health.capture_count = (u8)count;
    for (i = 0; i < 30U; i++)
        timing_health.capture_words[i] = i < count ? capture_buffer[slot][i] : 0U;
    timing_health.capture_reason = reason; /* publish immutable snapshot last */
}
static void capture_latch(void) {
    capture_failed = 1; CC15IE = 0; PECC6 = 0;
    XP1IR = 1; /* the worker latches DEADLINE outside level 15 */
}
void capture_isr(void) IRQ_HANDLER(0x1F) {
    u8 slot = capture_head, next = (u8)(slot + 1U), attempt, pending;
    u16 count = block_count[slot], captured, current, high, counter, distance, tooth;
    u32 stamp;
    board_knock_crank_seen();
    if (next == 3U) next = 0;
    counter = (u16)(previous_counter + count);
    current = T0;
    captured = capture_buffer[slot][count - 1U];
    if (!capture_started) {
        /* Boot can read storage with interrupts disabled while T0 already
           counts a turning crank. The first one-word PEC capture begins
           acquisition; those pre-acquisition edges are not lost transfers.
           Seed its counter coherently, excluding one newer pending edge. */
        for (attempt = 0; attempt < 3U; attempt++) {
            current = T0;
            pending = (u8)(CC15IR && CC15 != captured);
            if (T0 == current) break;
        }
        if (attempt == 3U) {
            capture_failure_snapshot(4U, current, counter, captured, slot, next, count);
            capture_overruns++;
            if (timing_health.capture_overruns != 65535U) timing_health.capture_overruns++;
            capture_latch();
            return;
        }
        counter = (u16)(current - pending);
        capture_started = 1;
    }
    /* A missed PEC request cannot be hidden by apparently plausible timings. */
    if (next == capture_tail || (current != counter &&
        !((u16)(current - counter) == 1U && CC15IR && CC15 != captured && T0 == current))) {
        capture_failure_snapshot((u16)(next == capture_tail ? 1U : 2U),
                                 current, counter, captured, slot, next, count);
        capture_overruns++;
        if (timing_health.capture_overruns != 65535U) timing_health.capture_overruns++;
        if (next == capture_tail) { capture_latch(); return; } /* worker starved */
        /* T0 counted an edge that no PEC transfer delivered (two edges within
           one service latency). The hardware still works; only this block's
           tooth relation is lost. Never decode it: discard it, reseed from T0
           exactly as at acquisition, and let the worker revoke angle before
           it decodes anything newer. Sync then needs two consistent gaps. */
        if (timing_health.capture_resyncs != 65535U) timing_health.capture_resyncs++;
        capture_started = 0; batch_enabled = 0; phase_valid = 0;
        capture_resync = 1;
        capture_arm(slot, 1);
        XP1IR = 1;
        return;
    }
    /* An edge pending in CC15 belongs to the next PEC buffer. It is not a
       missing transfer and must not be included in this block's counter. */
    current = T1; high = capture_high;
    if (T1IR) { high++; current = T1; }
    if (captured > current) high--;
    stamp = ((u32)high << 16) | captured;
    previous_period = count > 1U ? (u16)(captured - capture_buffer[slot][count - 2U]) :
                                  (u16)(captured - previous_capture);
    previous_capture = captured; previous_counter = counter;
    block_stamp[slot] = stamp; block_counter[slot] = counter;
    /* Tooth of this block's last edge, from the last decoded angle, so the
       worker can run the segment scheduler before decoding (as the ROM runs
       sub_37CA0 straight after CC15INT). 0xFF: no asserted angle. */
    tooth = 0xFFU;
    if (phase_valid) {
        distance = (u16)(counter - phase_counter);
        tooth = (u16)(phase_tooth + distance);
        while (tooth >= 58U) tooth -= 58U;
    }
    block_tooth[slot] = (u8)tooth;
    count = 1;
    if (batch_enabled == 1U) count = 30;
    else if (batch_enabled == 2U && tooth < 58U)
        count = tooth < 1U ? 1U - tooth : (tooth < 31U ? 31U - tooth : 59U - tooth);
    capture_arm(next, count);
    capture_head = next; /* publish only after metadata and next PEC destination */
    capture_blocks++;
    XP1IR = 1;
}
static void capture_timeout(void) {
    u16 lock;
    if (!ecu.rotation.seen || hal_capture_clock() - ecu.rotation.last <= 250000UL) return;
    lock = hal_hard_lock();
    ecu.rotation.state = ROT_UNSYNCED; ecu.rotation.rpm = 0;
    ecu.rotation.normal = 0; ecu.rotation.have_gap = ecu.rotation.seen = 0;
    ecu.rotation.epoch++; batch_enabled = 0; phase_valid = 0;
    safety_inhibit(INH_SYNC, ecu.milliseconds);
    previous_counter = T0;
    capture_arm(capture_head, 1); /* discard an incomplete old block */
    hal_hard_unlock(lock);
}
/* Angle is lost: no later edge may extend the old tooth relation. Without
   this a latched capture left the last rpm and ROT_VALID in place, so a
   stopped engine stayed RUNNING and its monitors kept evaluating it. */
static void capture_rotation_lost(void) {
    u16 lock = hal_hard_lock();
    if (ecu.rotation.state == ROT_VALID) ecu.rotation.losses++;
    ecu.rotation.state = ROT_UNSYNCED; ecu.rotation.rpm = 0;
    ecu.rotation.normal = 0; ecu.rotation.have_gap = ecu.rotation.seen = 0;
    ecu.rotation.epoch++; batch_enabled = 0; phase_valid = 0;
    safety_inhibit(INH_SYNC, ecu.milliseconds);
    hal_hard_unlock(lock);
}
static void capture_resynchronize(void) {
    u16 lock = hal_hard_lock();
    capture_resync = 0;
    capture_tail = capture_head; /* queued blocks straddle the lost edge */
    hal_hard_unlock(lock);
    capture_rotation_lost();
}
void engine_work_isr(void) IRQ_HANDLER(0x41) {
    u8 slot, accepted;
    u16 count, lock;
    u32 fastest;
    if (capture_failed) {
        /* Raised by capture_isr at level 15, which never touches authority. */
        safety_inhibit(INH_DEADLINE, ecu.milliseconds);
        if (ecu.rotation.state != ROT_UNSYNCED || ecu.rotation.seen) capture_rotation_lost();
        return;
    }
    if (capture_resync) capture_resynchronize();
    if (capture_tail == capture_head) { capture_timeout(); return; }
    slot = capture_tail; count = block_count[slot];
    /* OEM order: the segment ignition pass runs on the boundary capture
       before the block is decoded. Decoding (up to ~1 ms for 30 teeth)
       would otherwise delay every charge start near the boundary. */
    if ((block_tooth[slot] == 1U || block_tooth[slot] == 31U) && ecu.rotation.state == ROT_VALID) {
        board_knock_reference(block_counter[slot], block_stamp[slot]);
        board_ignition_segment(block_tooth[slot], block_stamp[slot], block_counter[slot],
                               count > 1U ? (u16)(capture_buffer[slot][count - 1U] -
                                                  capture_buffer[slot][count - 2U]) :
                                            (u16)ecu.rotation.normal);
    }
    /* The whole block is decoded at one instant: one misfire-window
       eligibility, one rotation-state write-back for regular teeth. */
    accepted = rotation_block(capture_buffer[slot], (u8)count, block_stamp[slot],
                              diagnostic_edge_eligible(), &fastest);
    lock = hal_hard_lock(); /* shared with capture_isr (level 15) */
    if (capture_resync) {
        /* A lost edge arrived during decode: batch/phase state from this
           block would be applied to the reseeded counter. */
        hal_hard_unlock(lock);
        capture_resynchronize();
        return;
    }
    /* Keep fast acquisition in blocks too. Switching to one-word completions
       while the preceding 30-word block is being decoded can fill the queue.
       Until angle is valid, blocks have no asserted phase boundary. */
    /* Judge the regular tooth period, not a block holding only the post-gap
       edge (whose interval is the gap). Switching to one-word blocks while a
       30-word block is armed fills the 3-slot queue at ~1150 rpm and latches
       DEADLINE. Hysteresis: enter at <=1092 ticks, leave only at >=1250. */
    /* After a sync loss normal can hold a misread gap interval, and a block
       holding only the post-gap edge has the gap as its fastest interval:
       the smaller of the two is the regular tooth period in both cases. */
    if (ecu.rotation.normal && ecu.rotation.normal < fastest)
        fastest = ecu.rotation.normal;
    batch_enabled = (fastest <= 1092UL || (batch_enabled && fastest < 1250UL)) ?
                    (ecu.rotation.state == ROT_VALID ? 2U : 1U) : 0U;
    phase_tooth = ecu.rotation.tooth; phase_counter = block_counter[slot];
    phase_valid = (u8)(ecu.rotation.state == ROT_VALID);
    capture_schedule_counter = block_counter[slot];
    hal_hard_unlock(lock);
    if (accepted && ecu.rotation.state == ROT_VALID) board_schedule(ecu.rotation.last);
    lock = hal_hard_lock(); /* shared with capture_isr (level 15) */
    capture_tail = slot == 2U ? 0U : (u8)(slot + 1U);
    if (capture_tail != capture_head) XP1IR = 1;
    hal_hard_unlock(lock);
    capture_timeout();
}
