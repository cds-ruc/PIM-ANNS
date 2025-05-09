import json
import argparse
try:
    import babeltrace
except ImportError:
    babeltrace = None


def ns_to_us(timestamp):
    return timestamp / float(1000)


def process_sample_event(event):
    name = event.name
    cat = name.split(":")[0].replace("probe_", "")
    if cat == "libdpu":
        cat = "dpu"
    if cat.startswith("perf_"):
        cat = cat.replace("perf_", "")
    ph = (
        "B"
        if name.endswith("__entry")
        else "E"
        if name.endswith("__exit__return")
        else "I"
    )
    record = {
        "name": name,
        "cat": cat,
        "pid": event["perf_pid"] if "perf_pid" in event else event["pid"],
        "tid": event["perf_tid"] if "perf_tid" in event else event["tid"],
        "ts": ns_to_us(event.timestamp),
        "ph": ph,
        "args": {
            k: v
            for k, v in event.items()
            if event._field(k).scope == babeltrace.CTFScope.EVENT_FIELDS
        },
    }
    return record


def ctf_to_tef(input_ctf, output_tef):
    if babeltrace is None:
        raise ImportError("Please install babeltrace to use function profiling")
    traces = babeltrace.TraceCollection()
    traces.add_traces_recursive(input_ctf, "ctf")
    trace_events = []
    for event in traces.events:
        trace_events.append(process_sample_event(event))
    with open(output_tef, "w") as f:
        f.write(json.dumps({"traceEvents": trace_events}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Convert CTF (Common Trace Format) traces to Chrome TEF (Trace Event Format)"
    )
    parser.add_argument("--output", default="out.json", help="Output TEF json file")
    parser.add_argument("input", help="Path to the CTF file/directory")

    args = parser.parse_args()
    ctf_to_tef(args.input, args.output)
