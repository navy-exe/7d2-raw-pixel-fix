"""Validate a TP*.TST comparison; count persistent bright Bayer outliers.

Source RAW files are never modified. A success report does not prove Bayer
phase, true analogue precision or continuous video reliability.
"""
import argparse,hashlib,json,struct
from pathlib import Path
import numpy as np
from render_mlv_preview import unpack14
FIELDS=('selector source_mux pack_mode hw_flags hw_rows hw_size hw_addr raw_addr '
 'width height pitch bpp frame_size live movie zoom resolution crop recording job raw_enabled '
 'iso shutter aperture ticks').split()
N=3708096
def sample(words):return dict(zip(FIELDS,words))
def valid(s,selector):
    rows=max((s['hw_rows']&65535)+1,(s['hw_size']>>16)+1)
    return (s['selector']==s['source_mux']==selector and s['pack_mode']==0x20 and
     (s['width'],s['height'],s['pitch'],s['bpp'],s['frame_size'])==(1984,1068,3472,14,N) and
     s['hw_size']&65535==3472 and rows==1068 and
     (s['hw_addr']^s['raw_addr'])&~0x40000000==0 and
     s['live'] and s['movie'] and s['zoom']==1 and not s['resolution'] and not s['crop'] and
     not s['recording'] and not s['job'] and s['raw_enabled'])
def analyze(path,threshold=256):
    path=Path(path);b=path.read_bytes()
    if len(b)!=244:raise ValueError('Unexpected TP summary size')
    w=struct.unpack('<61I',b)
    if w[0]!=0x31505452 or w[1] not in (1,2):raise ValueError('Unknown TP summary format')
    result=w[2] if w[2]<0x80000000 else w[2]-0x100000000
    report=dict(file=str(path.resolve()),result=result,original_selector=w[3],restored_selector=w[4],
     baseline_frames=w[5],candidate_frames=w[6],restore_ok=bool(w[7]),duration_us=(w[9]-w[8])&0xffffffff,save_errors=w[10],
     initial=sample(w[11:36]),final=sample(w[36:61]),complete=result==0 and w[5:8]==(3,3,1) and w[3]==w[4] and w[10]==0,
     limitation='Short standby frame snapshots; may straddle sensor updates. Dark-frame outliers alone do not verify Bayer phase, true bit precision, correction-map loading or continuous recording.')
    report['version']=w[1];report['native_lifecycle_completed']=report['complete']
    if not report['complete']:return report
    missing=[str(path.with_name(path.stem+letter+str(i)+ext)) for letter in 'BA' for i in range(3) for ext in ('.RAW','.HDR') if not path.with_name(path.stem+letter+str(i)+ext).is_file()]
    if missing:
        report.update(complete=False,missing_files=missing,failure_reason='Native lifecycle completed but comparison payloads/headers are missing; no pixel conclusion possible.')
        return report
    masks=[None,None];cold_masks=[None,None];zero_masks=[None,None];images=[[],[]];raw_info=None;entries=[];baseline_raw=None
    for stage,letter in ((0,'B'),(1,'A')):
        expected=w[3] if stage==0 else 18
        for index in range(3):
            stem=path.with_name(path.stem+letter+str(index));hdr=stem.with_suffix('.HDR').read_bytes()
            if len(hdr)!=392:raise ValueError('Unexpected frame header size')
            h=struct.unpack('<98I',hdr)
            if h[:5]!=(0x31504654,w[1],stage,index,N):raise ValueError('Frame stage/index/length/version mismatch')
            before,after=sample(h[8:33]),sample(h[33:58]);raw=h[58:98]
            if not valid(before,expected) or not valid(after,expected):raise ValueError('Frame capture guards did not pass')
            for s in (before,after):
                if any(s[k]!=report['initial'][k] for k in ('iso','shutter','aperture')):raise ValueError('Exposure changed')
            if raw[2:7]!=(1068,1984,3472,N,14) or raw[19]!=0x02010100:raise ValueError('RAW metadata geometry/CFA mismatch')
            if baseline_raw is None:baseline_raw=raw
            if raw[9:17]!=baseline_raw[9:17]:raise ValueError('Active area or crop metadata changed')
            data=stem.with_suffix('.RAW').read_bytes()
            if len(data)!=N:raise ValueError('Truncated or oversized RAW frame')
            p=unpack14(data,1984,1068).astype(np.int32)
            y1,x1,y2,x2=raw[13:17]
            if not(0<=y1<y2<=1068 and 0<=x1<x2<=1984):raise ValueError('Invalid active area')
            y1=max(2,y1);x1=max(2,x1);y2=min(1066,y2);x2=min(1982,x2)
            c=p[y1:y2,x1:x2]
            local=np.median(np.stack((p[y1-2:y2-2,x1:x2],p[y1+2:y2+2,x1:x2],p[y1:y2,x1-2:x2-2],p[y1:y2,x1+2:x2+2])),axis=0)
            mask=(c>raw[7]+threshold)&(c-local>threshold)
            masks[stage]=mask if masks[stage] is None else masks[stage]&mask
            cold=(c<raw[7]-threshold)&(local-c>threshold)
            zero=c==0
            cold_masks[stage]=cold if cold_masks[stage] is None else cold_masks[stage]&cold
            zero_masks[stage]=zero if zero_masks[stage] is None else zero_masks[stage]&zero
            images[stage].append(c.copy())
            entries.append(dict(stage='before' if stage==0 else 'after',index=index,file=str(stem.with_suffix('.RAW').resolve()),sha256=hashlib.sha256(data).hexdigest(),
             median=float(np.median(c)),minimum=int(c.min()),maximum=int(c.max()),single_frame_bright_outliers=int(mask.sum()),single_frame_cold_outliers=int(cold.sum()),zero_pixels=int(zero.sum()),copy_us=h[5],vsyncs_during_copy=(h[7]-h[6])&0xffffffff,
             black_level=raw[7],white_level=raw[8]))
            raw_info=dict(width=1984,height=1068,pitch=3472,bits=14,active_area=[y1,x1,y2,x2],cfa='RGGB as reported; requires lit-frame validation')
    before_count=int(masks[0].sum());after_count=int(masks[1].sum())
    ys,xs=np.nonzero(masks[0]&~masks[1]);y1,x1,_,_=raw_info['active_area']
    before_entries=[e for e in entries if e['stage']=='before'];after_entries=[e for e in entries if e['stage']=='after']
    signal_checks=dict(unique_before_payloads=len({e['sha256'] for e in before_entries}),unique_after_payloads=len({e['sha256'] for e in after_entries}),
     candidate_all_frames_flat=all(e['minimum']==e['maximum'] for e in after_entries),
     median_before=float(np.median([e['median'] for e in before_entries])),median_after=float(np.median([e['median'] for e in after_entries])),
     requires_lit_frame_bayer_phase_check=True,precision_confirmed_beyond_14_bit_packing=False)
    report.update(raw=raw_info,frames=entries,signal_checks=signal_checks,threshold_adu=threshold,persistent_bright_outliers_before=before_count,persistent_bright_outliers_after=after_count,
     baseline_sites_absent_after=int(len(xs)),baseline_sites_remaining=int((masks[0]&masks[1]).sum()),new_candidate_sites=int((~masks[0]&masks[1]).sum()),
     removed_site_examples=[dict(x=int(x+x1),y=int(y+y1)) for x,y in zip(xs[:32],ys[:32])],
     interpretation='Compare a lens-cap test first. Bright scene detail can also be counted as outliers; fewer outliers alone is not proof of an acceptable RAW image.')
    replacements=np.stack(images[1])[:,zero_masks[0]]
    zy,zx=np.nonzero(zero_masks[0])
    report.update(persistent_cold_outliers_before=int(cold_masks[0].sum()),persistent_cold_outliers_after=int(cold_masks[1].sum()),
     persistent_zero_pixels_before=int(zero_masks[0].sum()),persistent_zero_pixels_after=int(zero_masks[1].sum()),
     baseline_zero_sites_still_zero_after=int((zero_masks[0]&zero_masks[1]).sum()),
     after_values_at_baseline_zero_sites=None if not replacements.size else dict(minimum=int(replacements.min()),maximum=int(replacements.max()),median=float(np.median(replacements))),
     baseline_zero_site_examples=[dict(x=int(x+x1),y=int(y+y1)) for x,y in zip(zx[:32],zy[:32])])
    return report
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('summary');p.add_argument('--threshold',type=int,default=256);a=p.parse_args()
    if not 1<=a.threshold<16384:raise ValueError('Threshold must be 1..16383')
    result=analyze(a.summary,a.threshold)
    target=Path(a.summary).with_suffix('.verification.json');target.write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))
    if not result['complete']:__import__('sys').exit(1)
