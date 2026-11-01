/* SPDX-License-Identifier: GPL-2.0-or-later
 * Separate short RAW tap diagnostic. No recording, encoder or DMA redirection. */
#include "module.h"
#include "dryos.h"
#include "menu.h"
#include "shoot.h"
#include "lens.h"
#include "property.h"
#include "bmp.h"
#include "mem.h"
#include "raw.h"
#include "edmac.h"
#include "tap_format.h"
static volatile unsigned busy,used_once,ticks,watch,armed,cancel;
static unsigned original,retained;
static void *allocation;
static unsigned char *image;
static struct tap_report report;
static struct tap_frame frame;
const uint32_t rwtap_abi[]={sizeof(struct tap_sample),sizeof(struct tap_frame),sizeof(struct tap_report),TAP_BYTES};
static volatile uint32_t *const selector=(void*)0x2d7d0;
static unsigned int vsync(unsigned int unused){if(watch)ticks++;return 0;}
static void sample(struct tap_sample *s)
{
    memset(s,0,sizeof(*s));s->selector=*selector;s->ticks=ticks;
    s->live=lv;s->movie=is_movie_mode();s->zoom=lv_dispsize;
    s->resolution=video_mode_resolution;s->crop=video_mode_crop;
    s->recording=RECORDING?1:0;s->job=lens_info.job_state;s->raw_enabled=raw_lv_is_enabled();
    s->iso=lens_info.iso;s->shutter=lens_info.shutter;s->aperture=lens_info.aperture;
    s->width=raw_info.width;s->height=raw_info.height;s->pitch=raw_info.pitch;
    s->bpp=raw_info.bits_per_pixel;s->frame_size=raw_info.frame_size;
    s->raw_addr=(uint32_t)(uintptr_t)raw_info.buffer;
    if(!s->live || !s->movie)return;
    s->source_mux=shamem_read(0xd0037014)&63;
    s->pack_mode=shamem_read(0xd0008094);
    s->hw_flags=shamem_read(0xd0004204);s->hw_rows=shamem_read(0xd000420c);
    s->hw_size=shamem_read(0xd0004210);s->hw_addr=shamem_read(0xd0004208);
}
static int exposure_same(const struct tap_sample *s)
{return s->iso==report.initial.iso && s->shutter==report.initial.shutter && s->aperture==report.initial.aperture;}
static int wait_ready(unsigned expected,unsigned minimum_ticks,unsigned timeout)
{
    unsigned start=ticks;
    for(unsigned i=0;i<timeout;i+=10){
        struct tap_sample s;sample(&s);
        if(cancel || !s.live || !s.movie || s.recording || s.zoom!=1 || s.resolution || s.crop || s.job || !s.raw_enabled)return -20;
        /* Mode/geometry changes are not allowed to settle into a larger DMA. */
        if(s.width!=1984 || s.height!=1068 || s.pitch!=3472 || s.bpp!=14 || s.frame_size!=TAP_BYTES || s.pack_mode!=0x20)return -21;
        if(tap_ready(&s,expected) && exposure_same(&s) && (unsigned)(ticks-start)>=minimum_ticks)return 0;
        msleep(10);
    }
    return -22;
}
static int save_file(const char *path,const void *data,unsigned bytes)
{
    FILE *f=FIO_CreateFile(path);int ok=f && FIO_WriteFile(f,data,bytes)==(int)bytes;
    if(f)FIO_CloseFile(f);
    uint32_t actual=0;
    if(ok && (FIO_GetFileSize(path,&actual) || actual!=bytes))ok=0;
    if(!ok)report.save_errors++;
    return ok;
}
static int save_new_file(const char *path,const void *data,unsigned bytes)
{
    uint32_t existing=0;
    if(!FIO_GetFileSize(path,&existing)){report.save_errors++;return 0;}
    return save_file(path,data,bytes);
}
static int guards_ok(void)
{for(unsigned i=0;i<64;i++)if(image[(int)i-64]!=0xa5 || image[TAP_BYTES+i]!=0x5a)return 0;return 1;}
static int capture(const char *base,unsigned stage,unsigned index,unsigned expected)
{
    int rc=wait_ready(expected,1,800);if(rc)return rc;
    memset(&frame,0,sizeof(frame));frame.magic=0x31504654;frame.version=TAP_VERSION;
    frame.stage=stage;frame.index=index;frame.payload_bytes=TAP_BYTES;
    sample(&frame.before);if(!tap_ready(&frame.before,expected) || !exposure_same(&frame.before))return -23;
    memcpy(frame.raw_words,&raw_info,sizeof(raw_info));
    memset(image-64,0xa5,64);memset(image+TAP_BYTES,0x5a,64);
    frame.ticks_before=ticks;unsigned start=(uint32_t)get_us_clock();
    /* Existing synchronous fast-copy backend; no changes to the Canon DMA destination. */
    dma_memcpy(image,(void*)(uintptr_t)(frame.before.hw_addr|0x40000000u),TAP_BYTES);
    frame.copy_us=(uint32_t)get_us_clock()-start;frame.ticks_after=ticks;sample(&frame.after);
    if(!guards_ok()){retained=1;return -24;}
    if(!tap_ready(&frame.after,expected) || !exposure_same(&frame.after))return -25;
    char path[64];
    if(!tap_frame_path(path,base,stage,index,0) || !save_new_file(path,image,TAP_BYTES))return -26;
    if(!tap_frame_path(path,base,stage,index,1) || !save_new_file(path,&frame,sizeof(frame)))return -26;
    return 0;
}
static void restore_selector(void)
{
    if(!armed)return;
    uint32_t irq=cli();*selector=original;asm volatile("":::"memory");armed=0;sei(irq);
}
static void run_test(void)
{
    if(busy || used_once){NotifyBox(5000,"RAW tap test: once per boot. Restart first.");return;}
    busy=1;used_once=1;cancel=0;retained=0;
    memset(&report,0,sizeof(report));report.magic=0x31505452;report.version=TAP_VERSION;
    report.started_us=(uint32_t)get_us_clock();
    char base[48]={0},summary[64];
    for(unsigned i=0;i<1000;i++){uint32_t n;if(!tap_prefix(base,i) || !tap_summary_path(summary,base)){base[0]=0;break;}if(FIO_GetFileSize(summary,&n))break;base[0]=0;}
    if(!base[0]){busy=0;NotifyBox(5000,"No free RAW tap log prefix.");return;}
    if(!save_file(summary,&report,sizeof(report))){busy=0;NotifyBox(6000,"Cannot reserve RAW tap log.");return;}
    int rc=-1;
    original=*selector;report.original=original;
    static const unsigned char signature[8]={0x24,0x49,0x0e,0x28,0x10,0xb5,0x2f,0xd2};
    if(!is_camera("7D2","1.1.2") || memcmp((void*)0xfe2b196a,signature,8)){rc=-2;goto finish;}
    sample(&report.initial);
    if(original>=48 || original==TAP_CANDIDATE || !tap_ready(&report.initial,original) || raw_info.cfa_pattern!=0x02010100){rc=-3;goto finish;}
    allocation=fio_malloc(TAP_BYTES+256);if(!allocation){rc=-4;goto finish;}
    image=(void*)(((((uint32_t)(uintptr_t)allocation+63)&~63u)+64u)|0x40000000u);
    watch=1;gui_stop_menu();NotifyBox(5000,"RAW tap A/B: keep camera still; no buttons.");
    rc=wait_ready(original,3,800);if(rc)goto finish;
    for(unsigned i=0;i<3;i++){rc=capture(base,0,i,original);if(rc)goto finish;report.baseline_frames++;}
    /* Arm restoration before calling Canon. Native setter changes only state/logging. */
    armed=1;
    rc=((int(*)(unsigned))0xfe2b196b)(4);
    if(rc || *selector!=TAP_CANDIDATE){rc=-5;goto finish;}
    rc=wait_ready(TAP_CANDIDATE,3,1000);if(rc)goto finish;
    for(unsigned i=0;i<3;i++){rc=capture(base,1,i,TAP_CANDIDATE);if(rc)goto finish;report.candidate_frames++;}
finish:
    restore_selector();report.restored=*selector;
    if(watch && original<48){int restore_rc=wait_ready(original,2,1000);report.restore_ok=!restore_rc;if(restore_rc && !rc)rc=-27;}
    else report.restore_ok=report.restored==report.original;
    watch=0;sample(&report.final);report.result=rc;report.finished_us=(uint32_t)get_us_clock();
    if(allocation && !retained){fio_free(allocation);allocation=0;image=0;}
    int saved=save_file(summary,&report,sizeof(report));
    if(retained || !report.restore_ok)NotifyBox(12000,"RAW tap %d. RESTART camera. %s",rc,summary);
    else if(!saved || rc)NotifyBox(10000,"RAW tap stopped %d. Saved %s",rc,summary);
    else NotifyBox(12000,"RAW tap A/B saved. Original restored. %s",summary);
    busy=0;
}
static struct menu_entry entries[]={
 {.name="7D2 RAW tap A/B",.select=run_in_separate_task,.priv=run_test,
  .help="Save 3 RAW frames before/after DEFCORRE; restore original tap.",
  .help2="Movie LV 1080, 1x, RAW ON but not recording. Lens cap on first. Once per boot."}
};
static unsigned int init(void){if(!is_camera("7D2","1.1.2"))return 1;menu_add("Debug",entries,COUNT(entries));return 0;}
static unsigned int deinit(void){cancel=1;if(busy || retained)return 1;restore_selector();menu_remove("Debug",entries,COUNT(entries));return 0;}
MODULE_INFO_START()
 MODULE_INIT(init)
 MODULE_DEINIT(deinit)
 MODULE_LONGNAME("7D2 guarded RAW capture-point comparison")
MODULE_INFO_END()
MODULE_CBRS_START()
 MODULE_CBR(CBR_VSYNC,vsync,0)
MODULE_CBRS_END()
