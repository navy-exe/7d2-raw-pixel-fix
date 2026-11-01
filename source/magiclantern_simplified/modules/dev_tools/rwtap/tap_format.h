#ifndef RWTAP_FORMAT_H
#define RWTAP_FORMAT_H
#define TAP_BYTES 3708096u
#define TAP_CANDIDATE 18u
#define TAP_VERSION 2u
/* Avoid the camera's printf formatter for evidence filenames. */
static int tap_prefix(char *out,unsigned index)
{
    static const char text[]="ML/LOGS/TP0000";
    if(index>=1000)return 0;
    for(unsigned i=0;i<sizeof(text);i++)out[i]=text[i];
    while(index>=100){out[11]++;index-=100;}
    while(index>=10){out[12]++;index-=10;}
    out[13]='0'+index;
    return 1;
}
static int tap_summary_path(char *out,const char *base)
{
    for(unsigned i=0;i<14;i++){if(!base[i])return 0;out[i]=base[i];}
    if(base[14])return 0;
    static const char ext[]=".TST";for(unsigned i=0;i<sizeof(ext);i++)out[14+i]=ext[i];
    return 1;
}
static int tap_frame_path(char *out,const char *base,unsigned stage,unsigned index,int header)
{
    if(stage>1 || index>=3)return 0;
    for(unsigned i=0;i<14;i++){if(!base[i])return 0;out[i]=base[i];}
    if(base[14])return 0;
    out[14]=stage?'A':'B';out[15]='0'+index;out[16]='.';
    const char *ext=header?"HDR":"RAW";
    for(unsigned i=0;i<4;i++)out[17+i]=ext[i];
    return 1;
}
struct tap_sample {
    uint32_t selector,source_mux,pack_mode,hw_flags,hw_rows,hw_size,hw_addr,raw_addr;
    uint32_t width,height,pitch,bpp,frame_size;
    uint32_t live,movie,zoom,resolution,crop,recording,job,raw_enabled;
    uint32_t iso,shutter,aperture,ticks;
};
struct tap_frame {
    uint32_t magic,version,stage,index,payload_bytes,copy_us,ticks_before,ticks_after;
    struct tap_sample before,after;
    uint32_t raw_words[40];
};
struct tap_report {
    uint32_t magic,version;
    int32_t result;
    uint32_t original,restored,baseline_frames,candidate_frames,restore_ok;
    uint32_t started_us,finished_us,save_errors;
    struct tap_sample initial,final;
};
static int tap_ram(uint32_t a,uint32_t n)
{
    uint32_t p=a&~0x40000000u;
    return !(a&0xa0000000u) && !(p&3) && p>=0x4000 && p<0x20000000u && n<=0x20000000u-p;
}
static int tap_ready(const struct tap_sample *s,unsigned expected)
{
    unsigned rows=(s->hw_rows&65535)+1;
    unsigned rows2=(s->hw_size>>16)+1;if(rows2>rows)rows=rows2;
    return s->selector==expected && s->source_mux==expected && s->pack_mode==0x20 &&
        s->width==1984 && s->height==1068 && s->pitch==3472 && s->bpp==14 &&
        s->frame_size==TAP_BYTES && (s->hw_size&65535)==3472 && rows==1068 &&
        tap_ram(s->hw_addr,TAP_BYTES) && tap_ram(s->raw_addr,TAP_BYTES) &&
        ((s->hw_addr^s->raw_addr)&~0x40000000u)==0 &&
        s->live && s->movie && s->zoom==1 && s->resolution==0 && !s->crop &&
        !s->recording && !s->job && s->raw_enabled;
}
#endif
