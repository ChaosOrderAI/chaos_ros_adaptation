"""Mock command roundtrip, process CPU and memory benchmark. No hardware or broker."""
import argparse, hashlib, json, math, os, signal, statistics, subprocess, sys, tempfile, time
from pathlib import Path
os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
os.environ.pop('ROS_LOCALHOST_ONLY', None)
import rclpy
import yaml
from rclpy.node import Node
from chaos_interfaces.msg import VelocityCommand, CommandResult
from chaos_interfaces.srv import ControlLease

root=Path(__file__).parent
parser=argparse.ArgumentParser()
parser.add_argument('rate', type=float, nargs='?', default=100.0)
parser.add_argument('samples', type=int, nargs='?', default=500)
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--asset', type=Path, default=root/'fixtures/mock_asset.json')
parser.add_argument('--domain', type=int, default=194)
parser.add_argument('--native', action='store_true')
args=parser.parse_args()
if not math.isfinite(args.rate) or args.rate <= 0 or args.samples <= 0:
    parser.error('rate must be finite and rate/samples positive')
if not 0 <= args.domain <= 232: parser.error('domain must be 0..232')
if (50 + args.samples) / args.rate + 3 >= 28:
    parser.error('measurement must fit the explicit 30-second lease')
os.environ['ROS_DOMAIN_ID'] = str(args.domain)
if args.native and not args.executable: parser.error('--native requires --executable')
if args.executable:
    args.executable=args.executable.resolve(strict=True)
    if args.native and args.executable.read_bytes()[:4]!=b'\x7fELF':
        parser.error('native benchmark requires an ELF executable')

def process_usage(pid):
    """Linux process CPU counters and resident high-water mark, no descendants."""
    stat=Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()
    cpu=(int(stat[11])+int(stat[12]))/os.sysconf('SC_CLK_TCK')
    status=Path(f'/proc/{pid}/status').read_text().splitlines()
    memory={key:int(next(row.split()[1] for row in status if row.startswith(key+':')))
            for key in ('VmRSS','VmHWM')}
    return {'cpu_s':cpu,'rss_kib':memory['VmRSS'],'peak_rss_kib':memory['VmHWM']}
asset=json.loads(args.asset.read_text())
assert asset['mode']=='mock' and asset['driver']['plugin']=='mock'
asset['ros_domain_id']=args.domain
asset['namespace']='/benchmark_cpp_refactor'
endpoint=asset['namespace']
results={}
with tempfile.TemporaryDirectory(prefix='chaos-review-latency-') as td:
    os.environ['ROS_LOG_DIR']=td+'/ros-log'
    log=open(td+'/runtime.log','w+')
    params=Path(td)/'params.yaml'
    params.write_text(yaml.safe_dump({'/**': {'ros__parameters': {'asset_json': json.dumps(asset)}}}))
    command=([str(args.executable)] if args.executable else ['ros2','run','chaos_runtime','robot_runtime'])
    p=subprocess.Popen(command+['--ros-args','--params-file',str(params)],
                       stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    node=None
    try:
        rclpy.init()
        node=Node('review_latency_client')
        pub=node.create_publisher(VelocityCommand,endpoint+'/motion/velocity',10)
        pending={}
        latencies=[]
        rejected=[]
        def receive(msg):
            started=pending.pop(msg.request_id,None)
            if started is not None:
                latencies.append((time.perf_counter_ns()-started)/1000)
                if not msg.success: rejected.append(msg.code)
        sub=node.create_subscription(CommandResult,endpoint+'/command_results',receive,100)
        client=node.create_client(ControlLease,endpoint+'/control/lease')
        until=time.monotonic()+15
        while not client.service_is_ready() or pub.get_subscription_count()<1:
            if p.poll() is not None or time.monotonic()>until: raise RuntimeError('mock runtime startup failed')
            rclpy.spin_once(node,timeout_sec=.02)
        req=ControlLease.Request(operation='acquire',owner='review_benchmark',duration_s=30.0)
        future=client.call_async(req)
        rclpy.spin_until_future_complete(node,future,timeout_sec=5)
        lease=future.result()
        assert lease and lease.success
        rate=args.rate
        sample_count=args.samples
        warmup,total,period=50,50+sample_count,1.0/rate
        usage_start=process_usage(p.pid) if args.executable else None
        measurement_start=time.monotonic()
        next_send=time.monotonic()
        for i in range(total):
            while time.monotonic()<next_send:
                rclpy.spin_once(node,timeout_sec=min(.001,max(0,next_send-time.monotonic())))
            m=VelocityCommand()
            m.header.stamp=node.get_clock().now().to_msg()
            m.header.frame_id=asset['frames']['base']
            m.request_id=f'review_{i}'
            m.lease_token=lease.token
            m.valid_for_s=.5
            m.velocity.linear.x=.1
            if i>=warmup: pending[m.request_id]=time.perf_counter_ns()
            pub.publish(m)
            next_send+=period
            rclpy.spin_once(node,timeout_sec=0)
        deadline=time.monotonic()+3
        while time.monotonic()<deadline: rclpy.spin_once(node,timeout_sec=.01)
        values=sorted(latencies)
        def percentile(q): return values[round(q*(len(values)-1))] if values else None
        results={'samples_expected':total-warmup,'samples_received':len(values),'missing':len(pending),'rejected':rejected,
                 'asset_sha256':hashlib.sha256(args.asset.read_bytes()).hexdigest(),
                 'domain':args.domain,'rate_hz':rate,'warmup':warmup,'latency_unit':'us','p50':percentile(.5),'p95':percentile(.95),'p99':percentile(.99),
                 'max':max(values) if values else None,'raw_latency_us':latencies,
                 'note':'Host wall-clock roundtrip publish->CommandResult callback; includes DDS, executor and benchmark client spin. Mock backend; percentiles include received samples only, so always inspect missing and rejected counts. No hard real-time or hardware claim.'}
        if args.executable:
            usage_end=process_usage(p.pid)
            elapsed=time.monotonic()-measurement_start
            results['runtime_process']={
                'executable':str(args.executable),
                'sha256':hashlib.sha256(args.executable.read_bytes()).hexdigest(),
                'native_elf':args.native,
                'cpu_s':usage_end['cpu_s']-usage_start['cpu_s'],
                'cpu_percent_one_core':100*(usage_end['cpu_s']-usage_start['cpu_s'])/elapsed,
                'interval_s':elapsed,
                'rss_kib':usage_end['rss_kib'],
                'peak_rss_kib':usage_end['peak_rss_kib'],
                'note':'CPU interval includes warmup and a fixed 3-second final drain for both implementations; VmHWM includes startup. Direct runtime process only, excludes benchmark client.'}
        release=ControlLease.Request(operation='release',owner='review_benchmark',token=lease.token)
        f=client.call_async(release)
        rclpy.spin_until_future_complete(node,f,timeout_sec=3)
    finally:
        if node: node.destroy_node()
        if rclpy.ok(): rclpy.shutdown()
        if p.poll() is None:
            os.killpg(p.pid,signal.SIGINT)
            try: p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid,signal.SIGTERM)
                try: p.wait(timeout=3)
                except subprocess.TimeoutExpired: os.killpg(p.pid,signal.SIGKILL); p.wait()
        if not results:
            log.seek(0)
            print(log.read()[-5000:],file=sys.stderr)
        log.close()
print(json.dumps(results,indent=2))
