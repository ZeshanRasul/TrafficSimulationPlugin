"""Render the congestion experiment CSV as an SVG for the demo video.

Standard library only, so it runs against whatever Python is to hand.

    python tools/plot_congestion.py docs/CongestionExperiment.csv docs/congestion_plot.svg

Two stacked panels share one time axis. Flow and stopped-vehicle counts are
plotted separately rather than on twin y-scales: a dual-axis chart lets the
author imply any correlation they like by sliding one scale against the other.

Each panel draws the raw samples faintly and a rolling mean on top. The raw
series alone is too noisy to read at video sizes; the rolling mean alone would
hide how much of the variation is real.
"""

import csv
import sys
from pathlib import Path

# Reference data-visualisation palette, light surface.
SURFACE = "#fcfcfb"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"
MUTED = "#898781"
INK_SECONDARY = "#52514e"
INK_PRIMARY = "#0b0b0b"
SERIES_FLOW = "#2a78d6"
SERIES_STOPPED = "#eb6834"
BAND_HIGHLIGHT = "#fdf3dd"
BAND_NEUTRAL = "#f4f3ef"

WIDTH = 1920
HEIGHT = 1080
MARGIN_LEFT = 140
MARGIN_RIGHT = 70
PANEL_TOP = 280
PANEL_HEIGHT = 285
PANEL_GAP = 110

# The stage names get their own row between the subtitle and the first panel
# header. They are positioned at each stage's start, so the leftmost one lands
# in the same column as the panel title and collides if they share a band.
STAGE_LABEL_Y = 180

FONT = 'system-ui, -apple-system, "Segoe UI", sans-serif'

# Roughly five seconds at the default half-second sampling interval.
SMOOTHING_WINDOW = 11

# Only the stage that induces the congestion is tinted; the rest alternate a
# barely-there neutral so the boundaries read without four competing colours.
HIGHLIGHT_STAGE = "Restricted"


def read_samples(csv_path):
    with open(csv_path, newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))

    samples = []

    for row in rows:
        try:
            samples.append(
                {
                    "time": float(row["TimeSeconds"]),
                    "stage": row["Stage"],
                    "flow": float(row["MeanSpeedFraction"]) * 100.0,
                    "stopped": float(row["StoppedVehicles"]),
                    "total": float(row["TotalVehicles"]),
                }
            )
        except (KeyError, ValueError):
            # A malformed trailing line should not lose the whole run.
            continue

    return samples


def rolling_mean(values, window):
    half = window // 2
    smoothed = []

    for index in range(len(values)):
        start = max(0, index - half)
        end = min(len(values), index + half + 1)
        chunk = values[start:end]
        smoothed.append(sum(chunk) / len(chunk))

    return smoothed


def stage_spans(samples):
    """Contiguous runs of one stage, as (name, start_time, end_time)."""
    spans = []

    for sample in samples:
        if spans and spans[-1][0] == sample["stage"]:
            spans[-1][2] = sample["time"]
        else:
            spans.append([sample["stage"], sample["time"], sample["time"]])

    return spans


def nice_axis(value):
    """Return (ceiling, ticks) with round labels and no wasted headroom.

    Picking the next power-of-ten-ish bound leaves a peak of 63 sitting halfway
    up a 0-100 axis, which reads as though the series never got anywhere near
    the top. Choose the smallest round step that still fits in four intervals.
    """
    for step in (5, 10, 20, 25, 50, 100, 200, 250, 500, 1000):
        if value <= step * 4:
            ceiling = step * max(1, -(-int(value) // step))
            return float(ceiling), [
                float(t) for t in range(0, ceiling + 1, step)
            ]

    return float(int(value) + 1), None


def escape(text):
    return (
        str(text)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )


def build_svg(samples):
    if not samples:
        raise SystemExit("No usable rows in the CSV.")

    times = [s["time"] for s in samples]
    time_min, time_max = min(times), max(times)
    time_span = max(time_max - time_min, 1e-6)

    plot_width = WIDTH - MARGIN_LEFT - MARGIN_RIGHT

    def x_of(time_value):
        return MARGIN_LEFT + (time_value - time_min) / time_span * plot_width

    stopped_max, stopped_ticks = nice_axis(
        max(s["stopped"] for s in samples))

    panels = [
        {
            "title": "Network flow",
            "subtitle": "mean speed as a share of desired speed",
            "key": "flow",
            "colour": SERIES_FLOW,
            "top": PANEL_TOP,
            "max": 100.0,
            "suffix": "%",
            "ticks": [0, 25, 50, 75, 100],
        },
        {
            "title": "Stopped vehicles",
            "subtitle": f"of {int(samples[0]['total'])} in the network",
            "key": "stopped",
            "colour": SERIES_STOPPED,
            "top": PANEL_TOP + PANEL_HEIGHT + PANEL_GAP,
            "max": stopped_max,
            "suffix": "",
            "ticks": stopped_ticks,
        },
    ]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" '
        f'height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}" '
        f'font-family=\'{FONT}\'>',
        f'<rect width="{WIDTH}" height="{HEIGHT}" fill="{SURFACE}"/>',
        f'<text x="{MARGIN_LEFT}" y="86" font-size="46" font-weight="600" '
        f'fill="{INK_PRIMARY}">Congestion response to a signal restriction</text>',
        f'<text x="{MARGIN_LEFT}" y="132" font-size="26" fill="{INK_SECONDARY}">'
        f'Faint line is each sample; solid line is a rolling mean.</text>',
    ]

    spans = stage_spans(samples)

    for panel in panels:
        top = panel["top"]
        bottom = top + PANEL_HEIGHT
        value_max = panel["max"]

        def y_of(value, _top=top, _bottom=bottom, _max=value_max):
            return _bottom - (value / _max) * (_bottom - _top)

        # Stage bands sit behind everything else in the panel.
        for index, (name, start, end) in enumerate(spans):
            fill = (
                BAND_HIGHLIGHT
                if name == HIGHLIGHT_STAGE
                else (BAND_NEUTRAL if index % 2 else SURFACE)
            )

            parts.append(
                f'<rect x="{x_of(start):.1f}" y="{top}" '
                f'width="{max(x_of(end) - x_of(start), 0):.1f}" '
                f'height="{PANEL_HEIGHT}" fill="{fill}"/>'
            )

        ticks = panel["ticks"] or [
            value_max * fraction for fraction in (0, 0.25, 0.5, 0.75, 1.0)
        ]

        for tick in ticks:
            y = y_of(tick)

            parts.append(
                f'<line x1="{MARGIN_LEFT}" y1="{y:.1f}" '
                f'x2="{WIDTH - MARGIN_RIGHT}" y2="{y:.1f}" '
                f'stroke="{GRID}" stroke-width="1"/>'
            )

            label = f"{tick:.0f}{panel['suffix']}"

            parts.append(
                f'<text x="{MARGIN_LEFT - 18}" y="{y + 9:.1f}" '
                f'font-size="24" fill="{MUTED}" text-anchor="end" '
                f'style="font-variant-numeric: tabular-nums">{label}</text>'
            )

        raw = [s[panel["key"]] for s in samples]
        smooth = rolling_mean(raw, SMOOTHING_WINDOW)

        raw_points = " ".join(
            f"{x_of(s['time']):.1f},{y_of(v):.1f}"
            for s, v in zip(samples, raw)
        )

        smooth_points = " ".join(
            f"{x_of(s['time']):.1f},{y_of(v):.1f}"
            for s, v in zip(samples, smooth)
        )

        parts.append(
            f'<polyline points="{raw_points}" fill="none" '
            f'stroke="{panel["colour"]}" stroke-width="1.5" opacity="0.28"/>'
        )

        parts.append(
            f'<polyline points="{smooth_points}" fill="none" '
            f'stroke="{panel["colour"]}" stroke-width="4" '
            f'stroke-linejoin="round" stroke-linecap="round"/>'
        )

        parts.append(
            f'<line x1="{MARGIN_LEFT}" y1="{bottom}" '
            f'x2="{WIDTH - MARGIN_RIGHT}" y2="{bottom}" '
            f'stroke="{AXIS}" stroke-width="1.5"/>'
        )

        # One series per panel, so the title names it and no legend is needed.
        parts.append(
            f'<text x="{MARGIN_LEFT}" y="{top - 46}" font-size="32" '
            f'font-weight="600" fill="{INK_PRIMARY}">'
            f'{escape(panel["title"])}</text>'
        )

        parts.append(
            f'<text x="{MARGIN_LEFT}" y="{top - 18}" font-size="23" '
            f'fill="{MUTED}">{escape(panel["subtitle"])}</text>'
        )

    # Stage boundaries and labels, drawn once across both panels.
    first_top = panels[0]["top"]
    last_bottom = panels[-1]["top"] + PANEL_HEIGHT

    for name, start, end in spans:
        x_start = x_of(start)

        if start > time_min:
            parts.append(
                f'<line x1="{x_start:.1f}" y1="{first_top - 8}" '
                f'x2="{x_start:.1f}" y2="{last_bottom}" '
                f'stroke="{AXIS}" stroke-width="1.5" '
                f'stroke-dasharray="6 6"/>'
            )

        parts.append(
            f'<text x="{x_start + 14:.1f}" y="{STAGE_LABEL_Y}" '
            f'font-size="26" font-weight="600" fill="{INK_SECONDARY}">'
            f'{escape(name)}</text>'
        )

    for fraction in (0, 0.25, 0.5, 0.75, 1.0):
        time_value = time_min + fraction * time_span

        parts.append(
            f'<text x="{x_of(time_value):.1f}" y="{last_bottom + 46}" '
            f'font-size="24" fill="{MUTED}" text-anchor="middle" '
            f'style="font-variant-numeric: tabular-nums">'
            f'{time_value:.0f}s</text>'
        )

    parts.append(
        f'<text x="{WIDTH / 2:.0f}" y="{last_bottom + 92}" font-size="25" '
        f'fill="{INK_SECONDARY}" text-anchor="middle">'
        f'Time since experiment start</text>'
    )

    parts.append("</svg>")

    return "\n".join(parts)


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 \
        else Path("docs/CongestionExperiment.csv")

    target = Path(sys.argv[2]) if len(sys.argv) > 2 \
        else Path("docs/congestion_plot.svg")

    samples = read_samples(source)

    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(build_svg(samples), encoding="utf-8")

    spans = stage_spans(samples)

    print(f"Wrote {target} from {len(samples)} samples.")

    for name, start, end in spans:
        window = [s for s in samples if start <= s["time"] <= end]
        mean_flow = sum(s["flow"] for s in window) / len(window)
        mean_stopped = sum(s["stopped"] for s in window) / len(window)

        print(
            f"  {name:<11s} {start:6.1f}-{end:6.1f}s  "
            f"flow {mean_flow:5.1f}%  stopped {mean_stopped:4.1f}"
        )


if __name__ == "__main__":
    main()
