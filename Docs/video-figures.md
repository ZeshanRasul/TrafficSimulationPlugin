# Video figures and copy

Source text for the demo video's title cards. Figures come from the runs in
this folder — nothing here is estimated.

---

## Benchmark

Measured on a 4×4 junction grid, 16 signalised junctions.

**Simulation time is measured directly**, not inferred from frame time: with a
frame rate cap in play the engine idles to hit its deadline, so frame time says
nothing about how expensive the simulation is.

| Vehicles | Simulation / frame | µs per vehicle | Network flow |
|---:|---:|---:|---:|
| 50 | 0.18 ms | 3.6 | 60% |
| 200 | 0.94 ms | 4.7 | 38% |
| 500 | 3.36 ms | 6.7 | 23% |

### The headline

> **500 vehicles across 16 signalised junctions — 3.4 ms of simulation per frame.**

### The scaling claim

Cost fits `T = 3.23 µs·N + 7.0 × 10⁻⁶ ms·N²`, within **1.5%** across the
measured range.

- The linear term dominates below ~460 vehicles
- Above that the quadratic term — the neighbour search — takes over
- Extrapolating: ~10 ms at 1000 vehicles

Flow percentage is quoted beside every timing because a saturated network is a
different workload from a free-flowing one. A timing figure without it is not
interpretable, and these runs deliberately loaded the network to saturation.

**Reproduce:** `ATrafficBenchmarkRunner` → `Saved/TrafficSim/Benchmark.csv`

---

## Congestion experiment

Signal plan is swung to starve one axis, then biased the other way to work the
backlog off. Nothing about the vehicles changes; only the signal timing.

| Stage | Signal split | Mean flow | Mean stopped | Peak stopped |
|---|---|---:|---:|---:|
| Baseline | 8s / 8s | 73.7% | 21.0 | 37 |
| Restricted | 22s / 3s | 53.3% | 39.0 | 63 |
| Clearing | 4s / 16s | 61.1% | 30.4 | 58 |
| Recovered | 8s / 8s | 65.9% | 26.1 | 39 |

*100 vehicles on the 4×4 grid, all 16 junctions on the same plan, 210 seconds.
Plot: `congestion_plot.svg`. Raw run: `CongestionExperiment.csv`.*

Vehicles spawn evenly spaced and free-flowing, so the first ~30 seconds of
Baseline is a spin-up transient from 100% rather than a steady state. Baseline
settles at **67.8%** over its last 30 seconds — that is the figure the other
stages should be read against, not the 73.7% stage mean.

### The headline numbers

- Flow falls from **68% to 45%** at the deepest point of the restriction
- **63 of 100 vehicles stopped at once** at peak queue
- Recovery to **66%** — back to the settled baseline

### The point to make on camera

Returning to a balanced plan does **not** immediately clear a backlog. Flow at
the start of Clearing is still falling — it bottoms out at 36% eleven seconds
*after* the restriction is lifted, because the queue that has already formed
keeps discharging into junctions that traffic from the favoured pair is still
arriving at. The congested axis has to be given extra green before it recovers.
That is what a signal engineer would do, and the queue visibly works off when it
happens.

None of this is scripted. The queue forms because vehicles yield, follow, and
refuse to enter a junction they cannot clear.

### Known artefact

Every junction is handed the same plan at the same instant, so the grid runs in
lockstep with no inter-junction offset. That is what produces the ~29-second
sawtooth in the flow trace during the restricted stage: the whole network
discharges at once each cycle. Real coordinated grids stagger offsets to form a
green wave. Worth saying out loud if the oscillation is remarked on.

---

## Roadmap

Ordered by how much each would improve the simulation.

**1 · Congestion-aware routing**
Vehicles currently pick a random successor at each junction with no knowledge
of what lies ahead, so a blocked route stays blocked. Per-lane cost and a
successor choice that avoids congestion is the single largest improvement
available.

**2 · Lane changing and overtaking**
Vehicles hold their lane for the length of a road, so one slow lorry holds up
everything behind it. Also a precondition for routing to be fully useful.

**3 · Spatial partitioning**
The neighbour search is O(N²) and becomes dominant past ~460 vehicles. A
uniform grid lifts the practical ceiling beyond a thousand.

**4 · Editor tooling**
Junction placement and signal-phase authoring in the viewport rather than the
details panel.

**5 · Multi-lane approaches**
Turn-restricted lanes at junction approaches.

---

## Closing line

> Actor-based, configured in the details panel, and inspectable when something
> looks wrong — for the many projects that need believable street traffic
> rather than city-scale crowds.

---

## Regenerating

```bash
python tools/plot_congestion.py Docs/CongestionExperiment.csv Docs/congestion_plot.svg
```

Prints per-stage means as it goes, so the table above can be updated from the
same run that produced the plot.
