/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded task-context lifecycle for the 7D2.112 RAW capture-point video test.
 * Hardware access is supplied by the caller; host tests mock only that boundary.
 */
#ifndef CAPTURE_TAP_7D2_H
#define CAPTURE_TAP_7D2_H
#include <stdint.h>
struct capture_tap_sample_7d2 {
    uint32_t selector, mux, pack, hw_rows, hw_size;
    uint32_t width, height, pitch, bytes, bits, cfa, ticks;
    int live, movie, zoom, resolution, crop, raw, h264, job;
};
struct capture_tap_ops_7d2 {
    void (*sample)(struct capture_tap_sample_7d2 *);
    int (*select_corrected)(void);
    void (*restore)(uint32_t);
    void (*sleep10)(void);
};
struct capture_tap_state_7d2 {
    uint32_t original, expected;
    int armed, active, fault;
};
static int capture_tap_geometry_7d2(const struct capture_tap_sample_7d2 *s)
{
    unsigned rows=(s->hw_rows&65535)+1, rows2=(s->hw_size>>16)+1;
    if(rows2>rows)rows=rows2;
    return s->live && s->movie && s->zoom==1 && !s->resolution && !s->crop &&
        s->raw && !s->h264 && !s->job && s->pack==0x20 &&
        s->width==1984 && s->height==1068 && s->pitch==3472 &&
        s->bytes==3708096 && s->bits==14 && s->cfa==0x02010100 &&
        (s->hw_size&65535)==3472 && rows==1068;
}
static int capture_tap_matches_7d2(const struct capture_tap_sample_7d2 *s,unsigned expected)
{return capture_tap_geometry_7d2(s) && s->selector==expected && s->mux==expected;}
static int capture_tap_wait_7d2(const struct capture_tap_ops_7d2 *ops,unsigned expected,unsigned frames)
{
    struct capture_tap_sample_7d2 s;ops->sample(&s);unsigned start=s.ticks;
    for(unsigned i=0;i<100;i++){
        ops->sample(&s);
        if(!capture_tap_geometry_7d2(&s))return -31;
        if(s.selector!=expected)return -32;
        if(s.mux==expected && (unsigned)(s.ticks-start)>=frames)return 0;
        ops->sleep10();
    }
    return -33;
}
static int capture_tap_begin_7d2(struct capture_tap_state_7d2 *state,const struct capture_tap_ops_7d2 *ops,int mode)
{
    struct capture_tap_sample_7d2 s;
    if(state->armed || state->active || state->fault)return -34;
    if(mode!=1 && mode!=2)return -35;
    ops->sample(&s);
    /* Never take ownership of an unexpected selector or someone else's tap. */
    if(!capture_tap_matches_7d2(&s,14))return -36;
    state->original=14;state->expected=mode==2?18:14;
    if(mode==2){
        state->armed=1; /* cleanup owns restoration even on setter failure */
        if(ops->select_corrected())return -37;
    }
    int rc=capture_tap_wait_7d2(ops,state->expected,3);
    if(!rc)state->active=1;
    return rc;
}
static void capture_tap_restore_7d2(struct capture_tap_state_7d2 *state,const struct capture_tap_ops_7d2 *ops)
{
    state->active=0;
    if(state->armed){ops->restore(state->original);state->armed=0;}
}
static int capture_tap_verify_restored_7d2(struct capture_tap_state_7d2 *state,const struct capture_tap_ops_7d2 *ops)
{
    int rc=capture_tap_wait_7d2(ops,state->original,2);
    if(rc)state->fault=1; /* refuse another trial until reboot */
    return rc;
}
#endif
