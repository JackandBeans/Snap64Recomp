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


def analyze(path, refresh, complete=False):
    if not path.exists():
        return {'passed': False, 'reasons': ['No frame telemetry'], 'valid': False}
    with path.open(newline='') as stream:
        rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(stream)]
    course = [r for r in rows if r['course'] and not r['cinematic']]
    reasons = []
    if not complete:
        reasons.append('Complete Beach route not confirmed')
    if len(course) < 2:
        return {'passed': False, 'valid': False, 'reasons': reasons + ['No measured gameplay']}
    # Reject truncated routes and untracked/unfocused samples, rather than
    # deleting inconvenient intervals from the measured run.
    elapsed = course[-1]['time'] - course[0]['time']
    if elapsed < 60:
        reasons.append('Gameplay shorter than 60 seconds')
    if any(not r['focused'] or not r['head_valid'] for r in course):
        reasons.append('Focus or tracking lost')
    if any(r.get('level_id',-1)!=0 for r in course):
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
    # Changed interpolation weights count as new animation samples; reusing
    # exactly the same source/weight across many submissions does not.
    stale = 0
    longest_stale = 0
    for a,b in zip(course, course[1:]):
        stale = stale+1 if (a['workload'],a['alpha']) == (b['workload'],b['alpha']) else 0
        longest_stale = max(stale,longest_stale)
    if longest_stale >= refresh*.1:
        reasons.append('Repeated animation pose for at least 100 ms')
    distinct = len({(r['epoch'],r['source_frame']) for r in course})
    if not any(r['camera_held'] for r in course):
        reasons.append('Held-camera checkpoint missing')
    if min(r['film'] for r in course) >= max(r['film'] for r in course):
        reasons.append('Photo checkpoint missing')
    return {'passed': not reasons, 'valid': valid, 'reasons': reasons,
            'refresh': refresh, 'eye_sizes': sizes, 'seconds': elapsed,
            'application_fps': fps, 'source_fps': distinct/elapsed,
            'missed_intervals': missed, 'missed_fraction': missed/expected,
            'longest_identical_pose_frames': longest_stale,
            'interval_ms': {'median': statistics.median(intervals), 'p95': percentile(intervals,.95), 'p99': percentile(intervals,.99), 'max': max(intervals)},
            'timings_ms': {key: {'median': statistics.median([r[key] for r in course]), 'p99': percentile([r[key] for r in course],.99)}
                           for key in ('cpu_ms','gpu_ms','wait_ms','left_ms','right_ms','viewfinder_ms','props_ms','copy_ms')}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--refresh', type=int, choices=(72,80), default=80)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--full-course', action='store_true', help='Run the complete route for final qualification; default is a 77-second Beach iteration (approximately halfway)')
    parser.add_argument('--launch-check', action='store_true', help='Verify focused real XR startup with Touch controllers off; not an FPS qualification')
    parser.add_argument('--analyze', type=Path)
    args = parser.parse_args()
    if args.analyze:
        print(json.dumps(analyze(args.analyze,args.refresh),indent=2));return
    commit = subprocess.check_output(['git','rev-parse','--short','HEAD'],cwd=ROOT,text=True).strip()
    run = ROOT/'artifacts/quest'/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+commit)
    for directory in ('logs','metrics','captures','traces'):
        (run/directory).mkdir(parents=True)
    report = {'passed':False,'valid':False,'reasons':[], 'commit':commit,'refresh':args.refresh,'capture_run':args.capture,
              'scope':'full-course' if args.full_course else 'half-course-iteration'}
    def adb(*command, check=True):
        result = subprocess.run(['adb','-s',args.serial,*command],text=True,capture_output=True,timeout=30)
        with (run/'logs/adb.log').open('a',encoding='utf-8') as log:
            log.write(json.dumps(command)+'\n'+result.stdout+result.stderr+'\n')
        if check and result.returncode:
            raise RuntimeError('ADB command failed: '+' '.join(command)+' '+result.stderr)
        return result.stdout
    try:
        if args.build:
            with (run/'logs/build.log').open('w') as log:
                subprocess.run([sys.executable,str(ROOT/'tools/build_quest.py'),'--benchmark','--install','--serial',args.serial],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        apk = ROOT/'build-quest/Snap64RecompVR-quest-benchmark.apk'
        report['apk_sha256'] = hashlib.sha256(apk.read_bytes()).hexdigest()
        report['dirty'] = bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip())
        report['model'] = adb('shell','getprop','ro.product.model').strip()
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
        settings = run/'metrics/snapsettings.json'
        settings.write_text(json.dumps({'vr_render_scale':1.0,'resolution_scale':2}))
        adb('push',str(settings),REMOTE+'/snapsettings.json')
        adb('shell','chmod','666',REMOTE+'/snapsettings.json')
        for name in ('frames.csv','status.json','status.tmp'):
            adb('shell','rm','-f',REMOTE+'/benchmark/results/'+name)
        adb('shell','am','start','-n',PACKAGE+'/org.snap64.quest.QuestActivity')
        started = time.monotonic();status={};last_sample=0;last_progress=started;next_capture=started+40;entered_at=None;focused_once=False
        while time.monotonic()-started < args.timeout:
            now=time.monotonic()
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
                if status.get('complete'):break
                if status.get('entered') and entered_at is None:
                    entered_at=now
                if not args.full_course and entered_at is not None and now-entered_at>=77:
                    report['halfway_iteration_finished']=True
                    break
            if now-last_progress>30:
                raise RuntimeError('No new XR telemetry for 30 seconds; launch/focus stalled')
            if now-started>120 and not status.get('entered'):
                raise RuntimeError('Beach entry checkpoint timed out')
            if args.capture and now>=next_capture:
                adb('shell','touch',REMOTE+'/vr-capture.request');next_capture=now+30
            thermal=adb('shell','dumpsys','thermalservice',check=False)
            with (run/'metrics/thermal.txt').open('a') as log:log.write(f'\n{now-started:.2f}\n{thermal}')
            print(f'{now-started:.0f}s: {status}',flush=True)
            time.sleep(5)
        # Freeze the buffered evidence before either pull so the report and
        # retained CSV describe the same measured interval.
        adb('shell','am','force-stop',PACKAGE)
        adb('pull',REMOTE+'/benchmark/results',str(run/'metrics'))
        report.update(analyze(run/'metrics/results/frames.csv',args.refresh,status.get('complete',False)))
        if not args.full_course:
            report['passed']=False
            report['reasons'].append('Half-course iteration only; full-course qualification still required')
        if args.launch_check:
            report['passed']=False
            report['reasons']=['Launch check only; not a performance qualification']
        if args.capture:
            report['passed']=False;report['reasons'].append('Visual capture run, excluded from timing qualification')
    except (RuntimeError,OSError,subprocess.SubprocessError,ValueError) as error:
        report['reasons'].append(str(error))
    finally:
        try:
            (run/'logs/crash.txt').write_text(adb('logcat','-b','crash','-d',check=False),encoding='utf-8')
        except (OSError,subprocess.SubprocessError):pass
        for remote,destination in ((REMOTE+'/snap64.log',run/'logs/snap64.log'),(REMOTE+'/benchmark/results',run/'metrics'),
                                   (REMOTE+'/vr-submitted-left.png',run/'captures/left.png'),(REMOTE+'/vr-submitted-right.png',run/'captures/right.png')):
            try:adb('pull',remote,str(destination),check=False)
            except (OSError,subprocess.SubprocessError):pass
        report['diagnostics']=analyze(run/'metrics/results/frames.csv',args.refresh,False)
        (run/'report.json').write_text(json.dumps(report,indent=2))
        summary=['# Quest benchmark','',('PASS' if report['passed'] else 'NOT QUALIFIED'),'',
                 f"Scope: {report.get('scope')}. Build: {report.get('commit')} (dirty: {report.get('dirty')}).",
                 f"APK SHA-256: {report.get('apk_sha256')}",'']
        if 'application_fps' in report:
            summary += [f"Rendered FPS: {report['application_fps']:.2f}; source FPS: {report['source_fps']:.2f}; physical refresh: {report['refresh']} Hz.",
                        f"Eye dimensions: {report['eye_sizes']}; measured gameplay: {report['seconds']:.2f} seconds.",
                        f"Missed intervals: {report['missed_fraction']:.2%}; frame interval p95/p99: {report['interval_ms']['p95']:.2f}/{report['interval_ms']['p99']:.2f} ms.",'']
        summary += ['- '+r for r in report['reasons']]
        (run/'report.md').write_text('\n'.join(summary)+'\n')
        print(json.dumps(report,indent=2));print('Evidence:',run)
    sys.exit(0 if report['passed'] or report.get('launch_passed',False) else 1)


if __name__=='__main__':main()
