#!/usr/bin/env python3
"""Measure game progress from KOS timings, independently of Flycast's display FPS."""
import argparse
import json
import re
from pathlib import Path


def measure(path, start_frame, end_frame, window_frames=None):
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    if any(re.search(r"Fatal:|PVR raster failed:|DC VI:|decode-failures=[1-9]", line) for line in lines):
        raise ValueError("the run contains a graphics or target failure")
    if sum("KallistiOS v" in line for line in lines) > 1:
        raise ValueError("the target restarted during the run")
    progress = {}
    samples = {}
    latest = None
    renders = {}
    for line in lines:
        match = re.fullmatch(r"Graphics progress: frames=(\d+) lists=(\d+)", line)
        if match:
            frame, lists = map(int, match.groups())
            if frame in progress:
                raise ValueError("repeated graphics progress")
            progress[frame] = lists
            latest = frame
        match = re.fullmatch(r"Game perf: frames=(\d+) total-us=(\d+) raster-us=(\d+) convert-us=(\d+) present-us=(\d+)", line)
        if match:
            frame, *timings = map(int, match.groups())
            if frame in samples or latest != frame:
                raise ValueError("repeated or unpaired game timing")
            if samples and (frame <= max(samples) or any(a < b for a, b in zip(timings, samples[max(samples)]))):
                raise ValueError("nonmonotonic game timing")
            if sum(timings[1:]) > timings[0]:
                raise ValueError("measured phases exceed total time")
            samples[frame] = timings
        match = re.fullmatch(r"Game renders: frames=(\d+) rendered-lists=(\d+) skipped-lists=(\d+) new-renders=(\d+) frameskip=(\d+)", line)
        if match:
            frame, *counts = map(int, match.groups())
            if frame in renders or frame not in samples or counts[0]+counts[1] != progress[frame] or counts[2]>frame or counts[2]>counts[0] or counts[3]>7:
                raise ValueError("invalid, repeated or unpaired rendering counts")
            if renders and (frame <= max(renders) or counts[3]!=renders[max(renders)][3] or any(a<b for a,b in zip(counts[:3],renders[max(renders)][:3]))):
                raise ValueError("nonmonotonic rendering counts")
            renders[frame] = counts
    if start_frame not in samples or end_frame not in samples or end_frame <= start_frame:
        raise ValueError("the requested complete timing interval is missing")
    total, raster, convert, present = [b-a for a, b in zip(samples[start_frame], samples[end_frame])]
    lists = progress[end_frame] - progress[start_frame]
    if total <= 0 or lists < 0:
        raise ValueError("invalid timing or display-list interval")
    seconds = total / 1_000_000
    result = {
        "log": str(path), "start_frame": start_frame, "end_frame": end_frame,
        "seconds": seconds, "presented_vi_fps": (end_frame-start_frame)/seconds,
        "display_lists": lists, "display_lists_per_second": lists/seconds,
        "raster_ms_per_vi": raster/1000/(end_frame-start_frame),
        "convert_ms_per_vi": convert/1000/(end_frame-start_frame),
        "present_ms_per_vi": present/1000/(end_frame-start_frame),
        "other_ms_per_vi": (total-raster-convert-present)/1000/(end_frame-start_frame),
    }
    if renders:
        if start_frame not in renders or end_frame not in renders or renders[start_frame][3]!=renders[end_frame][3]:
            raise ValueError("missing or changed frameskip interval")
        drawn, skipped, fresh = [b-a for a,b in zip(renders[start_frame][:3],renders[end_frame][:3])]
        result.update(frameskip=renders[end_frame][3],
                      frameskip_policy={6:"alternating-2-3",7:"steady-2-3-3-3"}.get(renders[end_frame][3],"fixed"), rendered_lists=drawn, skipped_lists=skipped,
                      rendered_lists_per_second=drawn/seconds, new_render_presentations_per_second=fresh/seconds)
    if window_frames is not None:
        if window_frames<=0 or (end_frame-start_frame)%window_frames:
            raise ValueError("the requested interval must contain complete positive windows")
        frames=list(range(start_frame,end_frame+1,window_frames))
        if any(frame not in samples for frame in frames):
            raise ValueError("a steadiness timing sample is missing")
        windows=[]
        for first,last in zip(frames,frames[1:]):
            elapsed=(samples[last][0]-samples[first][0])/1_000_000
            if elapsed<=0:
                raise ValueError("invalid steadiness interval")
            sample=dict(start_frame=first,end_frame=last,presented_vi_fps=window_frames/elapsed)
            if renders:
                if first not in renders or last not in renders:
                    raise ValueError("a steadiness rendering sample is missing")
                sample["new_render_presentations_per_second"]=(renders[last][2]-renders[first][2])/elapsed
            windows.append(sample)
        result["windows"]=windows
        result["min_window_presented_vi_fps"]=min(w["presented_vi_fps"] for w in windows)
        if renders:
            result["min_window_new_render_presentations_per_second"]=min(w["new_render_presentations_per_second"] for w in windows)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--start-frame", type=int, default=60)
    parser.add_argument("--end-frame", type=int, default=120)
    parser.add_argument("--min-fps", type=float)
    parser.add_argument("--metric", choices=("vi", "new-renders"), default="vi",
                        help="VI progression or presentations following new rendered display lists")
    parser.add_argument("--window-frames", type=int, help="report complete windows to check steadiness")
    parser.add_argument("--min-window-fps", type=float, help="require every window to meet the selected metric (default window: 60)")
    args = parser.parse_args()
    try:
        result = measure(args.log, args.start_frame, args.end_frame, args.window_frames if args.window_frames is not None else 60 if args.min_window_fps is not None else None)
    except ValueError as error:
        parser.exit(1, f"Benchmark invalid: {error}\n")
    print(json.dumps(result, indent=2))
    metric = "presented_vi_fps" if args.metric=="vi" else "new_render_presentations_per_second"
    if metric not in result:
        parser.exit(1, "Benchmark invalid: rendering counts are missing\n")
    if args.min_fps is not None and result[metric] < args.min_fps:
        parser.exit(1, f"FPS target not reached ({args.metric}): {result[metric]:.3f} < {args.min_fps:.3f}\n")

    if args.min_window_fps is not None:
        minimum=result.get("min_window_"+metric)
        if minimum is None:
            parser.exit(1,"Benchmark invalid: steadiness counts are missing\n")
        if minimum<args.min_window_fps:
            parser.exit(1,f"Steady FPS target not reached ({args.metric}): {minimum:.3f} < {args.min_window_fps:.3f}\n")


if __name__ == "__main__":
    main()
