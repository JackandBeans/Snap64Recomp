"""Run the isolated Quest benchmark and retain evidence, including invalid runs.

Python standard library only. Metrics never equate compositor refresh with
application FPS. Screenshots run separately from performance qualification.
"""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'org.snap64.quest.benchmark'
REMOTE = f'/sdcard/Android/data/{PACKAGE}/files'


def percentile(values, p):
    if not values:
        return None
    values = sorted(values)
    return values[min(len(values)-1, int((len(values)-1)*p))]


def analyze_pose_uploads(path, frames):
    """Audit the matrices actually passed to the eye uploaders, not alpha alone.

    This establishes CPU pose progression and stereo agreement. It does not
    replace sequential image checks or verification of deforming vertices.
    """
    eyes=[{},{}];objects=set();eligible=intermediate=malformed=0
    if not path.exists():return {'available':False}
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream):
            try:
                frame=int(row['tracking_frame']);eye=int(row['eye'])
                if frame not in frames or eye not in (0,1):continue
                key=(frame,int(row['object']),int(row['matrix']))
                eyes[eye][key]=int(row['pose_hash']);objects.add(key[1])
                if row['blends']=='1' and .02<float(row['alpha'])<.98 and float(row['source_motion'])>.01:
                    eligible+=1
                    intermediate+=min(float(row['from_a']),float(row['from_b']))>1e-4
            except (ValueError,TypeError,KeyError):malformed+=1
    shared=eyes[0].keys()&eyes[1].keys()
    return {'available':True,'moving_objects':len(objects),'eligible_moving_matrix_samples':eligible,
            'intermediate_matrix_samples':intermediate,'stereo_comparisons':len(shared),
            'stereo_mismatches':sum(eyes[0][key]!=eyes[1][key] for key in shared),
            'malformed_rows':malformed,'evidence':'CPU matrices submitted for GPU upload; final image motion remains a separate check'}


def analyze_vertex_motion(path, frames):
    if not path.exists():return {'available':False}
    samples=[];malformed=0
    with path.open(newline='') as stream:
        reader=csv.DictReader(stream)
        if not reader.fieldnames or 'moving_vertices' not in reader.fieldnames:return {'available':False}
        for row in reader:
            try:
                if int(row['tracking_frame']) in frames:samples.append({k:float(v) for k,v in row.items()})
            except (ValueError,TypeError):malformed+=1
    moving=[r for r in samples if r['moving_vertices']>0]
    return {'available':True,'samples':len(samples),'moving_vertex_frames':len(moving),
            'intermediate_vertex_frames':sum(.001<r['alpha']<.999 for r in moving),
            'max_moving_vertices':max((r['moving_vertices'] for r in samples),default=0),
            'max_changed_meshes':max((r.get('vertex_changed',0) for r in samples),default=0),
            'max_local_delta':max((r['max_vertex_delta'] for r in samples),default=0),
            'malformed_rows':malformed,
            'evidence':'Snapshot correspondence and uploaded velocity inputs; final GPU vertex output remains unverified'}


def analyze_gpu_vertices(path, frames):
    if not path.exists():return {'available':False}
    rows=[];malformed=0
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream):
            try:
                if int(row['tracking_frame']) in frames:rows.append({k:float(v) for k,v in row.items()})
            except (ValueError,TypeError):malformed+=1
    return {'available':True,'samples':len(rows),'failures':sum(not r['passed'] for r in rows),
            'deforming_samples':sum(bool(r['deforming']) for r in rows),
            'moving_samples':sum(r['from_source']>.02 for r in rows),
            'eyes':sorted({int(r['eye']) for r in rows}),'malformed_rows':malformed,
            'max_error':max((r['max_error'] for r in rows),default=0),
            'evidence':'Fenced readback of real RSP compute output compared with the predicted-time CPU pose; final raster visibility remains a separate check'}


def analyze(path, refresh, complete=False, scene='beach'):
    if not path.exists():
        return {'passed': False, 'reasons': ['No frame telemetry'], 'valid': False}
    malformed=0
    rows=[]
    with path.open(newline='') as stream:
        for row in csv.DictReader(stream):
            try:rows.append({k:float(v) for k,v in row.items()})
            except (ValueError,TypeError):malformed+=1
    gpu_path=path.with_name('gpu.csv')
    if gpu_path.exists():
        with gpu_path.open(newline='') as stream:
            completed={}
            for r in csv.DictReader(stream):
                try:completed[int(r['tracking_frame'])]={k:float(v) for k,v in r.items()}
                except (ValueError,TypeError,KeyError):malformed+=1
        for row in rows:
            row.update(completed.get(int(row['tracking_frame']),{}))
    course = [r for r in rows if (r['cinematic'] if scene=='intro' else r['course'] and not r['cinematic'])]
    reasons = ['Incomplete or malformed frame telemetry'] if malformed else []
    if not complete:
        reasons.append('Complete intro not confirmed' if scene=='intro' else 'Complete Beach route not confirmed')
    if len(course) < 2:
        return {'passed': False, 'valid': False, 'reasons': reasons + ['No measured gameplay']}
    # Reject truncated routes and untracked/unfocused samples, rather than
    # deleting inconvenient intervals from the measured run.
    elapsed = course[-1]['time'] - course[0]['time']
    minimum_seconds=10 if scene=='intro' else 60
    if elapsed < minimum_seconds:
        reasons.append(f'Measured scene shorter than {minimum_seconds} seconds')
    if any(not r['focused'] or not r['head_valid'] for r in course):
        reasons.append('Focus or tracking lost')
    if scene=='beach' and any(r.get('level_id',-1)!=0 for r in course):
        reasons.append('Beach identity not confirmed')
    if any(abs(r['hz']-refresh) > .1 for r in course):
        reasons.append('Physical refresh differs from requested rate')
    sizes = sorted({(int(r['width']), int(r['height'])) for r in course})
    if len(sizes) != 1:
        reasons.append('Eye resolution changed')
    intervals = [(b['time']-a['time'])*1000 for a, b in zip(course, course[1:])]
    predicted_intervals = [b['predicted']-a['predicted'] for a,b in zip(course, course[1:])]
    missed = sum(max(0, round(dt*refresh)-1) for dt in predicted_intervals)
    expected = max(1, round((course[-1]['predicted']-course[0]['predicted'])*refresh))
    fps = (len(course)-1)/elapsed if elapsed > 0 else 0
    valid = not reasons
    if fps < refresh*.99:
        reasons.append(f'Application FPS {fps:.2f} below {refresh*.99:.2f}')
    if missed/expected >= .01:
        reasons.append('Missed display interval fraction is at least 1%')
    # These are interpolation samples, not proof of rendered Pokemon poses.
    # Pose/vertex validation and visual motion evidence remain separate gates.
    stale = 0
    longest_stale = 0
    for a,b in zip(course, course[1:]):
        stale = stale+1 if (a['workload'],a['alpha']) == (b['workload'],b['alpha']) else 0
        longest_stale = max(stale,longest_stale)
    if longest_stale >= refresh*.1:
        reasons.append('Repeated animation pose for at least 100 ms')
    distinct = len({(r['epoch'],r['source_frame']) for r in course})
    poses=analyze_pose_uploads(path.with_name('pose_uploads.csv'),{int(r['tracking_frame']) for r in course})
    vertices=analyze_vertex_motion(path.with_name('animation.csv'),{int(r['tracking_frame']) for r in course})
    gpu_vertices=analyze_gpu_vertices(path.with_name('vertex_gpu.csv'),{int(r['tracking_frame']) for r in course})
    if gpu_vertices.get('failures',0) or gpu_vertices.get('malformed_rows',0):
        valid=False;reasons.append('GPU vertex output audit failed')
    if poses.get('stereo_mismatches',0) or poses.get('malformed_rows',0):
        valid=False
        reasons.append('Pose upload audit failed stereo agreement or data integrity')
    if scene=='beach' and not any(r['camera_held'] for r in course):
        reasons.append('Held-camera checkpoint missing')
    if scene=='beach' and min(r['film'] for r in course) >= max(r['film'] for r in course):
        reasons.append('Photo checkpoint missing')
    return {'passed': not reasons, 'valid': valid, 'reasons': reasons,
            'refresh': refresh, 'confirmed_refresh_rates': sorted({r['hz'] for r in course}),
            'eye_sizes': sizes, 'seconds': elapsed,
            'application_fps': fps, 'source_fps': distinct/elapsed,
            'missed_intervals': missed, 'missed_fraction': missed/expected,
            'longest_identical_interpolation_samples': longest_stale,
            'rendered_animation_verified': False,
            'pose_uploads': poses,
            'vertex_motion': vertices,
            'gpu_vertex_audit': gpu_vertices,
            'interval_ms': {'median': statistics.median(intervals), 'p95': percentile(intervals,.95), 'p99': percentile(intervals,.99), 'max': max(intervals)},
            'timings_ms': {key: {'median': statistics.median([r[key] for r in course if r[key]>=0]), 'p99': percentile([r[key] for r in course if r[key]>=0],.99)}
                           for key in ('cpu_ms','gpu_ms','wait_ms','left_ms','right_ms','viewfinder_ms','props_ms','copy_ms',
                                       'left_gpu_ms','right_gpu_ms','viewfinder_gpu_ms','xr_wait_ms','xr_begin_ms','xr_end_ms','source_cpu_ms','source_gpu_ms')
                           if key in course[0] and any(r[key]>=0 for r in course)}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--refresh', type=int, choices=(72,80,90), default=90)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--vertex-audit', action='store_true', help='Collect bounded deforming mesh pairs; excluded from timing qualification')
    parser.add_argument('--eye-size',type=int,nargs=2,metavar=('WIDTH','HEIGHT'),help='Explicit per-eye dimensions for separate resolution experiments')
    parser.add_argument('--aa',choices=('none','fxaa'),default='none',help='Headset output anti-aliasing; guest photo/scoring targets are unchanged')
    parser.add_argument('--scene', choices=('beach','intro'), default='beach')
    parser.add_argument('--full-course', action='store_true', help='Run the complete route for final qualification; default is a 77-second Beach iteration (approximately halfway)')
    parser.add_argument('--launch-check', action='store_true', help='Verify focused real XR startup with Touch controllers off; not an FPS qualification')
    parser.add_argument('--analyze', type=Path)
    args = parser.parse_args()
    if args.eye_size and any(value<1 or value>8192 for value in args.eye_size):parser.error('Eye dimensions must be between 1 and 8192')
    if args.analyze:
        print(json.dumps(analyze(args.analyze,args.refresh,scene=args.scene),indent=2));return
    commit = subprocess.check_output(['git','rev-parse','--short','HEAD'],cwd=ROOT,text=True).strip()
    run = ROOT/'artifacts/quest'/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+commit)
    for directory in ('logs','metrics','captures','traces'):
        (run/directory).mkdir(parents=True)
    report = {'passed':False,'valid':False,'reasons':[], 'commit':commit,'refresh':args.refresh,'capture_run':args.capture,'vertex_audit_run':args.vertex_audit,
              'requested_eye_size':args.eye_size,
              'anti_aliasing':args.aa,
              'scope':'intro' if args.scene=='intro' else 'full-course' if args.full_course else 'half-course-iteration'}
    def adb(*command, check=True):
        result = subprocess.run(['adb','-s',args.serial,*command],text=True,capture_output=True,timeout=30)
        with (run/'logs/adb.log').open('a',encoding='utf-8') as log:
            log.write(json.dumps(command)+'\n'+result.stdout+result.stderr+'\n')
        if check and result.returncode:
            raise RuntimeError('ADB command failed: '+' '.join(command)+' '+result.stderr)
        return result.stdout
    previous_stayon=None
    logcat_process=None
    logcat_file=None
    status={}
    try:
        if args.build:
            with (run/'logs/build.log').open('w') as log:
                subprocess.run([sys.executable,str(ROOT/'tools/build_quest.py'),'--benchmark','--install','--serial',args.serial],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        apk = ROOT/'build-quest/Snap64RecompVR-quest-benchmark.apk'
        report['apk_sha256'] = hashlib.sha256(apk.read_bytes()).hexdigest()
        installed=adb('shell','pm','path',PACKAGE).strip().removeprefix('package:')
        if not installed.startswith('/data/app/') or not installed.endswith('/base.apk'):
            raise RuntimeError('Installed benchmark APK path could not be verified')
        report['installed_apk_sha256']=adb('shell','sha256sum',installed).split()[0]
        if report['installed_apk_sha256']!=report['apk_sha256']:
            raise RuntimeError('Installed APK differs from the build being measured; install the current benchmark first')
        report['dirty'] = bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip())
        report['model'] = adb('shell','getprop','ro.product.model').strip()
        report['validation_run']=adb('shell','settings','get','global','enable_gpu_debug_layers').strip()=='1'
        report['synchronization_validation']=adb('shell','getprop','debug.vulkan.khronos_validation.validate_sync').strip() in ('true','1')
        previous_stayon=adb('shell','settings','get','global','stay_on_while_plugged_in').strip()
        adb('shell','svc','power','stayon','usb')
        # Meta's virtual proximity control prevents off-face suspend; actual
        # head tracking and session focus are still required below.
        adb('shell','am','broadcast','-a','com.oculus.vrpowermanager.prox_close')
        adb('shell','input','keyevent','224')
        report['keep_awake']='USB stay-on and virtual proximity close'
        # Android must create its own subdirectories: shell-created directories
        # are owned by shell and can make app asset installation fail EACCES.
        if not adb('shell','ls','-d',REMOTE+'/benchmark/results',check=False).strip().startswith('/'):
            adb('shell','am','start','-n',PACKAGE+'/org.snap64.quest.QuestActivity')
            for _ in range(20):
                if adb('shell','ls','-d',REMOTE+'/benchmark/results',check=False).strip().startswith('/'):break
                time.sleep(1)
            else:raise RuntimeError('App data initialization failed')
        adb('shell','am','force-stop',PACKAGE)
        # Read-only copy of the production save; subsequent writes are isolated.
        fixture = run/'metrics/pokemonsnap.bin'
        adb('pull','/sdcard/Android/data/org.snap64.quest/files/saves/pokemonsnap.bin',str(fixture))
        report['save_sha256'] = hashlib.sha256(fixture.read_bytes()).hexdigest()
        adb('push',str(fixture),REMOTE+'/saves/pokemonsnap.bin')
        adb('shell','chmod','666',REMOTE+'/saves/pokemonsnap.bin')
        rate = run/'metrics/refresh.txt';rate.write_text(str(args.refresh))
        adb('push',str(rate),REMOTE+'/benchmark/refresh.txt')
        scene=run/'metrics/scene.txt';scene.write_text(args.scene)
        adb('push',str(scene),REMOTE+'/benchmark/scene.txt')
        eye_size=run/'metrics/eye-size.txt';eye_size.write_text(' '.join(map(str,args.eye_size or [0,0])))
        adb('push',str(eye_size),REMOTE+'/benchmark/eye-size.txt')
        vertex_audit=run/'metrics/vertex-audit.txt';vertex_audit.write_text('1' if args.vertex_audit else '0')
        adb('push',str(vertex_audit),REMOTE+'/benchmark/vertex-audit.txt')
        early_capture=run/'metrics/early-capture.txt';early_capture.write_text('1' if args.capture and args.scene=='intro' else '0')
        adb('push',str(early_capture),REMOTE+'/benchmark/early-capture.txt')
        if args.capture and args.scene=='intro':
            adb('shell','rm','-f',REMOTE+'/benchmark/captures/intro-*.png',REMOTE+'/benchmark/captures/intro-frames.csv')
        adb('shell','rm','-f',*(REMOTE+f'/benchmark/results/vertex-pair-{index}.json' for index in range(1,17)))
        settings = run/'metrics/snapsettings.json'
        settings.write_text(json.dumps({'vr_render_scale':1.0,'resolution_scale':2,'vr_fxaa':args.aa=='fxaa'}))
        adb('push',str(settings),REMOTE+'/snapsettings.json')
        adb('shell','chmod','666',REMOTE+'/snapsettings.json')
        adb('shell','rm','-f',REMOTE+'/benchmark/finish.request')
        for name in ('frames.csv','gpu.csv','animation.csv','pose_uploads.csv','vertex_gpu.csv','status.json','status.tmp','finished.json'):
            adb('shell','rm','-f',REMOTE+'/benchmark/results/'+name)
        adb('shell','am','start','-n',PACKAGE+'/org.snap64.quest.QuestActivity')
        started = time.monotonic();status={};last_sample=0;last_progress=started;next_capture=started+40;entered_at=None;focused_once=False
        while time.monotonic()-started < args.timeout:
            now=time.monotonic()
            if logcat_process is None:
                pid=adb('shell','pidof',PACKAGE,check=False).strip()
                if pid:
                    logcat_file=(run/'logs/logcat.txt').open('w',encoding='utf-8')
                    logcat_process=subprocess.Popen(['adb','-s',args.serial,'logcat','--pid='+pid,'-v','threadtime'],stdout=logcat_file,stderr=subprocess.STDOUT)
            if now-started>5 and not adb('shell','pidof',PACKAGE,check=False).strip():
                raise RuntimeError('Benchmark process exited or crashed; run invalid')
            power=adb('shell','dumpsys','power')
            if 'mWakefulness=Asleep' in power:
                raise RuntimeError('Headset asleep; run invalid')
            raw=adb('shell','cat',REMOTE+'/benchmark/results/status.json',check=False)
            if raw.strip().startswith('{'):
                status=json.loads(raw)
                if status.get('samples',0)!=last_sample:
                    last_sample=status['samples'];last_progress=now
                tracked=status.get('focused') and status.get('head_valid')
                if not tracked:
                    if focused_once or status.get('entered') or now-started>30:
                        raise RuntimeError('OpenXR focus/tracking unavailable; run invalid')
                    time.sleep(1)
                    continue
                focused_once=True
                if args.launch_check and status.get('samples',0)>=120:
                    report['launch_passed']=status.get('real_controllers') is False
                    if not report['launch_passed']:report['reasons'].append('Controller-free startup not confirmed')
                    break
                if args.scene=='intro' and status.get('intro_complete'):break
                if args.scene=='beach' and status.get('complete'):break
                if status.get('entered') and entered_at is None:
                    entered_at=now
                if not args.full_course and entered_at is not None and now-entered_at>=77:
                    report['halfway_iteration_finished']=True
                    break
            if now-last_progress>30:
                raise RuntimeError('No new XR telemetry for 30 seconds; launch/focus stalled')
            if args.scene=='beach' and now-started>120 and not status.get('entered'):
                raise RuntimeError('Beach entry checkpoint timed out')
            if args.capture and now>=next_capture:
                adb('shell','touch',REMOTE+'/vr-capture.request');next_capture=now+30
            thermal=adb('shell','dumpsys','thermalservice',check=False)
            with (run/'metrics/thermal.txt').open('a') as log:log.write(f'\n{now-started:.2f}\n{thermal}')
            print(f'{now-started:.0f}s: {status}',flush=True)
            time.sleep(5)
        # Retire both GPU slots and close all telemetry before force-stop.
        # Acknowledgement is mandatory; force-stop alone can split CSV rows.
        adb('shell','touch',REMOTE+'/benchmark/finish.request')
        for _ in range(25):
            finished=adb('shell','cat',REMOTE+'/benchmark/results/finished.json',check=False)
            if finished.strip().startswith('{') and json.loads(finished).get('finished'):break
            time.sleep(.2)
        else:raise RuntimeError('Benchmark telemetry finalization timed out')
        report['telemetry_finalized']=True
        adb('shell','am','force-stop',PACKAGE)
        adb('pull',REMOTE+'/benchmark/results',str(run/'metrics'))
        report.update(analyze(run/'metrics/results/frames.csv',args.refresh,status.get('intro_complete' if args.scene=='intro' else 'complete',False),args.scene))
        if args.eye_size and report.get('eye_sizes')!=[tuple(args.eye_size)]:
            report['passed']=report['valid']=False;report['reasons'].append('Measured eye dimensions differ from requested dimensions')
        report['timing_passed']=report['passed']
        if not report.get('rendered_animation_verified'):
            report['passed']=False
            report['reasons'].append('Rendered animation verification remains outstanding; interpolation counters alone do not qualify')
        if args.scene=='beach' and not args.full_course:
            report['passed']=False
            report['reasons'].append('Half-course iteration only; full-course qualification still required')
        if args.launch_check:
            report['passed']=False
            report['reasons']=['Launch check only; not a performance qualification']
        if args.capture:
            report['passed']=False;report['reasons'].append('Visual capture run, excluded from timing qualification')
        if args.vertex_audit:
            report['passed']=False;report['reasons'].append('Vertex audit run, excluded from timing qualification')
        if report.get('validation_run'):
            report['passed']=False;report['reasons'].append('Vulkan validation run, excluded from timing qualification')
    except (RuntimeError,OSError,subprocess.SubprocessError,ValueError) as error:
        report['reasons'].append(str(error))
    finally:
        if logcat_process is not None:
            logcat_process.terminate()
            logcat_process.wait(timeout=10)
        if logcat_file is not None:logcat_file.close()
        try:
            (run/'logs/power.txt').write_text(adb('shell','dumpsys','vrpowermanager',check=False),encoding='utf-8')
        except (OSError,subprocess.SubprocessError):pass
        # Restore power even if subsequent evidence parsing or disk writes fail.
        if previous_stayon is not None:
            try:
                if previous_stayon=='null':adb('shell','settings','delete','global','stay_on_while_plugged_in',check=False)
                else:adb('shell','settings','put','global','stay_on_while_plugged_in',previous_stayon,check=False)
                adb('shell','am','broadcast','-a','com.oculus.vrpowermanager.automation_disable',check=False)
            except (OSError,subprocess.SubprocessError):pass
        try:
            (run/'logs/crash.txt').write_text(adb('logcat','-b','crash','-d',check=False),encoding='utf-8')
        except (OSError,subprocess.SubprocessError):pass
        for remote,destination in ((REMOTE+'/snap64.log',run/'logs/snap64.log'),(REMOTE+'/benchmark/results',run/'metrics'),
                                   (REMOTE+'/vr-submitted-left.png',run/'captures/left.png'),(REMOTE+'/vr-submitted-right.png',run/'captures/right.png')):
            try:adb('pull',remote,str(destination),check=False)
            except (OSError,subprocess.SubprocessError):pass
        report['diagnostics']=analyze(run/'metrics/results/frames.csv',args.refresh,status.get('intro_complete' if args.scene=='intro' else 'complete',False),args.scene)
        if args.capture and args.scene=='intro':
            try:adb('pull',REMOTE+'/benchmark/captures',str(run/'captures/early-intro'),check=False)
            except (OSError,subprocess.SubprocessError):pass
        renderer_log=run/'logs/snap64.log'
        report['anti_aliasing_confirmed']=False
        if renderer_log.exists():
            log=renderer_log.read_text(encoding='utf-8',errors='replace')
            aa_label='FXAA' if args.aa=='fxaa' else 'none'
            report['anti_aliasing_confirmed']=f'Quest antialiasing: {aa_label}' in log
            if any(marker in log for marker in ('vkQueueSubmit failed','vkWaitForFences failed','vkGetQueryPoolResults failed','VK_ERROR_DEVICE_LOST')):
                report['passed']=report['valid']=False
                report['reasons'].append('Vulkan submission, completion, or query failure; frame counters are not valid rendered-frame evidence')
        if not report['anti_aliasing_confirmed']:
            report['passed']=report['valid']=False;report['reasons'].append('Requested anti-aliasing mode not confirmed by renderer')
        validation_log=run/'logs/logcat.txt'
        if validation_log.exists() and 'Validation Error:' in validation_log.read_text(encoding='utf-8',errors='replace'):
            report['passed']=report['valid']=False
            report['reasons'].append('Vulkan validation errors; see logs/logcat.txt')
        (run/'report.json').write_text(json.dumps(report,indent=2))
        summary=['# Quest benchmark','',('PASS' if report['passed'] else 'NOT QUALIFIED'),'',
                 f"Scope: {report.get('scope')}. Build: {report.get('commit')} (dirty: {report.get('dirty')}).",
                 f"Anti-aliasing: {report.get('anti_aliasing')} (renderer confirmed: {report.get('anti_aliasing_confirmed')}).",
                 f"APK SHA-256: {report.get('apk_sha256')}",'']
        if 'application_fps' in report:
            summary += [f"Rendered FPS: {report['application_fps']:.2f}; source FPS: {report['source_fps']:.2f}; requested refresh: {report['refresh']} Hz; measured rates: {report.get('confirmed_refresh_rates',[])}.",
                        f"Eye dimensions: {report['eye_sizes']}; measured gameplay: {report['seconds']:.2f} seconds.",
                        f"Missed intervals: {report['missed_fraction']:.2%}; frame interval p95/p99: {report['interval_ms']['p95']:.2f}/{report['interval_ms']['p99']:.2f} ms.",'']
        gpu_audit=report.get('gpu_vertex_audit',{})
        if gpu_audit.get('available'):
            summary += [f"GPU vertex audit: {gpu_audit['samples']} samples, {gpu_audit['failures']} mismatches, {gpu_audit['deforming_samples']} deforming samples. Zero deforming samples cannot verify deformation. Raster visibility remains unverified.",'']
        summary += ['- '+r for r in report['reasons']]
        (run/'report.md').write_text('\n'.join(summary)+'\n')
        print(json.dumps(report,indent=2));print('Evidence:',run)
    sys.exit(0 if report['passed'] or report.get('launch_passed',False) else 1)


if __name__=='__main__':main()
