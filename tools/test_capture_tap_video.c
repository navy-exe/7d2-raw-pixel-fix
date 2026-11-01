#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../source/magiclantern_simplified/modules/raw_video/mlv_lite/capture_tap_7d2.h"
static struct capture_tap_sample_7d2 s;
static struct capture_tap_state_7d2 state;
static int sleeps,sets,restores,set_rc,stall_mux,stall_ticks,mode_drift;
static void sample(struct capture_tap_sample_7d2 *out){*out=s;}
static int select_corrected(void){sets++;s.selector=18;return set_rc;}
static void restore(uint32_t original){restores++;s.selector=original;}
static void sleep10(void){
    sleeps++;
    if(!stall_ticks)s.ticks++;
    if(!stall_mux)s.mux=s.selector;
    if(mode_drift)s.width=2000;
}
static const struct capture_tap_ops_7d2 ops={sample,select_corrected,restore,sleep10};
static void reset(void){
    memset(&s,0,sizeof(s));memset(&state,0,sizeof(state));
    sleeps=sets=restores=set_rc=stall_mux=stall_ticks=mode_drift=0;
    s.selector=s.mux=14;s.pack=0x20;s.hw_size=(1067u<<16)|3472;
    s.width=1984;s.height=1068;s.pitch=3472;s.bytes=3708096;
    s.bits=14;s.cfa=0x02010100;s.live=s.movie=s.zoom=s.raw=1;
}
static void cleanup(void){capture_tap_restore_7d2(&state,&ops);assert(!state.armed && !state.active && s.selector==14);}
int main(void){
    int cases=0;
    reset();assert(!capture_tap_begin_7d2(&state,&ops,2));assert(state.active && state.armed && sets==1 && sleeps==3);
    assert(capture_tap_matches_7d2(&s,18));cleanup();assert(restores==1);
    assert(!capture_tap_verify_restored_7d2(&state,&ops));assert(s.mux==14 && sleeps==5);cases++;
    /* Reuse, manual stop and automatic stop both use the same cleanup ownership. */
    assert(!capture_tap_begin_7d2(&state,&ops,2));cleanup();assert(!capture_tap_verify_restored_7d2(&state,&ops));assert(restores==2);cases++;
    reset();assert(!capture_tap_begin_7d2(&state,&ops,1));assert(sets==0 && !state.armed && state.active);cleanup();assert(restores==0);assert(!capture_tap_verify_restored_7d2(&state,&ops));cases++;
    reset();set_rc=-1;assert(capture_tap_begin_7d2(&state,&ops,2)==-37);assert(state.armed && !state.active);cleanup();assert(restores==1 && !capture_tap_verify_restored_7d2(&state,&ops));cases++;
    reset();stall_mux=1;assert(capture_tap_begin_7d2(&state,&ops,2)==-33);assert(sleeps==100 && state.armed);cleanup();cases++;
    reset();stall_ticks=1;assert(capture_tap_begin_7d2(&state,&ops,2)==-33);assert(sleeps==100);cleanup();cases++;
    reset();mode_drift=1;assert(capture_tap_begin_7d2(&state,&ops,2)==-31);cleanup();cases++;
    reset();assert(!capture_tap_begin_7d2(&state,&ops,2));cleanup();stall_mux=1;assert(capture_tap_verify_restored_7d2(&state,&ops)==-33);assert(state.fault);assert(capture_tap_begin_7d2(&state,&ops,2)==-34);cases++;
    reset();s.ticks=0xfffffffeu;assert(!capture_tap_begin_7d2(&state,&ops,2));cleanup();assert(!capture_tap_verify_restored_7d2(&state,&ops));cases++;
    /* All these initial states must refuse selection without a write. */
    for(int bad=0;bad<20;bad++){
        reset();switch(bad){
            case 0:s.selector=18;break;case 1:s.mux=34;break;case 2:s.pack=0x120;break;
            case 3:s.width=2000;break;case 4:s.height=1070;break;case 5:s.pitch=3500;break;
            case 6:s.bytes--;break;case 7:s.bits=12;break;case 8:s.cfa=0;break;
            case 9:s.live=0;break;case 10:s.movie=0;break;case 11:s.zoom=5;break;
            case 12:s.resolution=1;break;case 13:s.crop=1;break;case 14:s.raw=0;break;
            case 15:s.h264=1;break;case 16:s.job=1;break;case 17:s.hw_size--;break;
            case 18:s.hw_size=(1066u<<16)|3472;break;case 19:s.hw_rows=1070;break;
        }
        assert(capture_tap_begin_7d2(&state,&ops,2)==-36);assert(!sets && !state.armed && !state.active);cases++;
    }
    reset();assert(capture_tap_begin_7d2(&state,&ops,0)==-35);cases++;
    reset();assert(!capture_tap_begin_7d2(&state,&ops,2));assert(capture_tap_begin_7d2(&state,&ops,2)==-34);cleanup();cases++;
    printf("PASS: %d lifecycle cases: baseline/corrected, reuse, ownership, failures, bounded settling, mode guards, wraparound, restoration fault lockout. Hardware mocked.\n",cases);
}
