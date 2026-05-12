# Vehicle Routing Problem (VRP)

## Problem Definition

Given a set of **buses**, each with their own depot and passenger capacity, and a set
of **requests**, each with an origin, a destination, and a number of people, find the
assignment of requests to buses and the visit ordering that **minimises the makespan**
(the time until the last person is delivered).

This is a capacitated VRP (CVRP) variant — NP-hard in general — solved here with a
two-phase heuristic: greedy assignment followed by 2-opt local search on each bus route.

---

## Why is this NP-Hard?

Even after fixing the assignment of requests to buses, finding the optimal visit order
for each bus is equivalent to TSP on its subset of stops — already NP-hard. The
assignment itself adds another combinatorial layer. The total search space grows
factorially with the number of requests.

---

## Pipeline

```
VRP.json
    │
    ├─ Parse buses (id, capacity, depot coordinates)
    ├─ Parse requests (id, origin, destination, people)
    │
    ▼
Quadtree (built from the real Multigraph node set)
    │
    ├─ nearest(depot.coords)       → real graph Vertex
    ├─ nearest(request.origin)     → real graph Vertex
    └─ nearest(request.destination)→ real graph Vertex
    │
    ▼
Distance Matrix  (A* or Dijkstra on the Multigraph, BUS mode only)
    │   Computed between every unique stop involved:
    │   depots + request origins + request destinations
    │   Symmetric → only N*(N-1)/2 shortest-path calls needed
    │
    ▼
Phase 1 — Greedy Assignment
    │   Sort requests by people descending.
    │   Assign each request to the bus with the most remaining
    │   capacity that can still fit it. Report infeasibility if
    │   total demand exceeds total capacity.
    │
    ▼
Phase 2 — 2-opt Local Search (per bus)
    │   For each bus, try all pairwise segment reversals in its
    │   stop sequence. Accept any reversal that reduces total
    │   travel time. Repeat until no improvement found.
    │
    ▼
Output
    ├─ Per-bus route:  depot → pickup_A → dropoff_A → pickup_B → ...
    ├─ Per-bus finish time
    ├─ Makespan = max(finish time across all buses)
    └─ Feasibility report (total demand vs total capacity)
```

---

## JSON Format

```json
{
  "buses": [
    {
      "id": "bus_1",
      "capacity": 40,
      "depot": { "x": 526800.0, "y": 4554900.0, "name": "Depot Boavista" }
    }
  ],
  "requests": [
    {
      "id":          "req_1",
      "origin":      { "x": 527200.0, "y": 4555100.0 },
      "destination": { "x": 532400.0, "y": 4558600.0 },
      "people":      15
    }
  ]
}
```

### Fields

| Field | Type | Description |
|---|---|---|
| `buses[].id` | string | Unique bus identifier |
| `buses[].capacity` | int | Max passengers this bus can carry |
| `buses[].depot.x/y` | double | UTM coordinates of this bus's starting depot |
| `requests[].id` | string | Unique request identifier |
| `requests[].origin.x/y` | double | UTM coordinates of pickup location |
| `requests[].destination.x/y` | double | UTM coordinates of dropoff location |
| `requests[].people` | int | Number of people in this request |

Coordinates are in **UTM metres**, matching the Porto graph coordinate system
(`EPSG:32629`). The Quadtree maps each coordinate to the nearest real bus stop
in the graph.

---

## Data Structures Used

| Structure | Role |
|---|---|
| `Quadtree` | Spatial index for O(log n) nearest-stop lookup |
| `Multigraph` | Underlying transport network |
| `FibonacciHeap` / `MutablePriorityQueue` | A* / Dijkstra for distance matrix |
| Distance matrix (`vector<vector<double>>`) | Pairwise travel times between all stops |

---

## Complexity

| Phase | Complexity |
|---|---|
| Quadtree build | O(n log n) |
| Quadtree query per point | O(log n) |
| Distance matrix (A* × pairs) | O(S² × (E + V log V)) where S = unique stops |
| Greedy assignment | O(R log R) where R = requests |
| 2-opt per bus | O(k²) per iteration where k = stops assigned to bus |

---

## Limitations & Future Work

- **No time windows** — requests have no latest pickup/dropoff time
- **No bus reuse** — each bus makes one tour and returns to depot
- **Homogeneous demand** — all people in a request board at the same stop
- A* heuristic uses metro speed as lower bound — see `Multigraph.cpp`
- A better solver (e.g. branch-and-bound, ILP) would find the true optimum
  for small instances