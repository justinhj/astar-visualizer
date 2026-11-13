#include "graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <random>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kScaleUnits = 1000.0f;  // Scale [0,1] coords to readable cost units

float DistSq(const GraphNode& a, const GraphNode& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float SampleWeightFactor(std::mt19937& rng) {
    std::uniform_real_distribution<float> bucket(0.0f, 1.0f);
    float r = bucket(rng);
    if (r < 0.30f) {
        std::uniform_real_distribution<float> d(1.0f, 1.25f);
        return d(rng);
    }
    if (r < 0.75f) {
        std::uniform_real_distribution<float> d(1.40f, 2.30f);
        return d(rng);
    }
    std::uniform_real_distribution<float> d(2.60f, 4.20f);
    return d(rng);
}

void FinalizePolarCoords(std::vector<GraphNode>& nodes) {
    for (auto& n : nodes) {
        float dx = n.x - 0.5f;
        float dy = n.y - 0.5f;
        n.angle = std::atan2(dy, dx);
        n.radius_norm = std::sqrt(dx * dx + dy * dy) * 2.0f;
    }
}

}  // namespace

void WeightedGraph::Generate(int num_nodes, GraphLayout layout, uint32_t seed) {
    num_nodes = std::max(8, std::min(260, num_nodes));
    nodes_.clear();
    edges_.clear();

    switch (layout) {
        case GraphLayout::RadialPulse:
            BuildRadialPulse(num_nodes, seed);
            break;
        case GraphLayout::OrganicConstellation:
            BuildOrganicConstellation(num_nodes, seed);
            break;
        case GraphLayout::WarpedMesh:
            BuildWarpedMesh(num_nodes, seed);
            break;
    }

    FinalizePolarCoords(nodes_);
    ConnectGraph(seed ^ 0x9e3779b9u);
}

void WeightedGraph::BuildRadialPulse(int num_nodes, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitter(-0.025f, 0.025f);
    std::uniform_real_distribution<float> angle_jitter(-0.18f, 0.18f);

    nodes_.reserve(num_nodes);

    // Center hub node
    GraphNode center;
    center.id = 0;
    center.x = 0.5f;
    center.y = 0.5f;
    nodes_.push_back(center);

    int remaining = num_nodes - 1;
    int rings = std::max(2, static_cast<int>(std::round(std::sqrt(static_cast<float>(num_nodes)) * 0.55f)));
    rings = std::min(rings, 6);

    // Distribute nodes across concentric elliptical rings
    std::vector<int> ring_counts(rings, 0);
    int total_weight = 0;
    for (int r = 0; r < rings; ++r) {
        total_weight += (r + 1);
    }
    int assigned = 0;
    for (int r = 0; r < rings; ++r) {
        int count = (r == rings - 1) ? (remaining - assigned)
                                     : std::max(4, (remaining * (r + 1)) / total_weight);
        count = std::min(count, remaining - assigned);
        ring_counts[r] = count;
        assigned += count;
    }

    const float rx_max = 0.42f;
    const float ry_max = 0.37f;

    for (int r = 0; r < rings; ++r) {
        int count = ring_counts[r];
        if (count <= 0) continue;
        float t = static_cast<float>(r + 1) / static_cast<float>(rings);
        float rx = 0.08f + (rx_max - 0.08f) * t;
        float ry = 0.07f + (ry_max - 0.07f) * t;
        float base_offset = (r % 2 == 1) ? (kPi / static_cast<float>(count)) : 0.0f;

        for (int i = 0; i < count; ++i) {
            float step = (2.0f * kPi) / static_cast<float>(count);
            float theta = base_offset + i * step + angle_jitter(rng) * step;
            float r_scale = 1.0f + jitter(rng) * 1.6f;

            GraphNode n;
            n.id = static_cast<int>(nodes_.size());
            n.x = std::clamp(0.5f + rx * r_scale * std::cos(theta) + jitter(rng) * 0.35f, 0.06f, 0.94f);
            n.y = std::clamp(0.5f + ry * r_scale * std::sin(theta) + jitter(rng) * 0.35f, 0.11f, 0.85f);
            nodes_.push_back(n);
        }
    }
}

void WeightedGraph::BuildOrganicConstellation(int num_nodes, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist_x(0.06f, 0.94f);
    std::uniform_real_distribution<float> dist_y(0.11f, 0.85f);

    nodes_.reserve(num_nodes);
    const int candidates_per_point = 24;

    for (int i = 0; i < num_nodes; ++i) {
        float best_x = dist_x(rng);
        float best_y = dist_y(rng);
        float best_min_d2 = -1.0f;

        for (int c = 0; c < candidates_per_point; ++c) {
            float cx = dist_x(rng);
            float cy = dist_y(rng);
            // Soft elliptical bias to echo the central workspace
            float ex = (cx - 0.5f) / 0.45f;
            float ey = (cy - 0.48f) / 0.38f;
            if (ex * ex + ey * ey > 1.15f && c < candidates_per_point - 4) {
                continue;
            }

            float min_d2 = std::numeric_limits<float>::max();
            for (const auto& existing : nodes_) {
                float dx = cx - existing.x;
                float dy = cy - existing.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < min_d2) min_d2 = d2;
            }

            if (nodes_.empty() || min_d2 > best_min_d2) {
                best_min_d2 = min_d2;
                best_x = cx;
                best_y = cy;
            }
        }

        GraphNode n;
        n.id = i;
        n.x = best_x;
        n.y = best_y;
        nodes_.push_back(n);
    }
}

void WeightedGraph::BuildWarpedMesh(int num_nodes, uint32_t seed) {
    std::mt19937 rng(seed);

    float aspect = 1.65f;
    int cols = std::max(3, static_cast<int>(std::ceil(std::sqrt(num_nodes * aspect))));
    int rows = std::max(3, static_cast<int>(std::ceil(static_cast<float>(num_nodes) / cols)));
    while (cols * rows < num_nodes) {
        ++cols;
    }

    float min_x = 0.07f, max_x = 0.93f;
    float min_y = 0.12f, max_y = 0.84f;
    float dx = (cols > 1) ? (max_x - min_x) / static_cast<float>(cols - 1) : 0.0f;
    float dy = (rows > 1) ? (max_y - min_y) / static_cast<float>(rows - 1) : 0.0f;

    std::uniform_real_distribution<float> jx(-dx * 0.28f, dx * 0.28f);
    std::uniform_real_distribution<float> jy(-dy * 0.28f, dy * 0.28f);

    std::vector<GraphNode> grid_candidates;
    grid_candidates.reserve(cols * rows);
    for (int r = 0; r < rows; ++r) {
        float stagger = (r % 2 == 1) ? dx * 0.22f : -dx * 0.10f;
        for (int c = 0; c < cols; ++c) {
            GraphNode n;
            n.x = std::clamp(min_x + c * dx + stagger + jx(rng), 0.06f, 0.94f);
            n.y = std::clamp(min_y + r * dy + jy(rng), 0.11f, 0.85f);
            grid_candidates.push_back(n);
        }
    }

    // Subsample evenly if grid has extra cells
    if (static_cast<int>(grid_candidates.size()) > num_nodes) {
        std::shuffle(grid_candidates.begin(), grid_candidates.end(), rng);
        grid_candidates.resize(num_nodes);
    }

    nodes_.reserve(num_nodes);
    for (int i = 0; i < static_cast<int>(grid_candidates.size()); ++i) {
        grid_candidates[i].id = i;
        nodes_.push_back(grid_candidates[i]);
    }
}

bool WeightedGraph::HasEdge(int u, int v) const {
    for (const auto& e : nodes_[u].edges) {
        if (e.to == v) return true;
    }
    return false;
}

void WeightedGraph::AddUndirectedEdge(int u, int v, float weight_factor) {
    if (u == v || HasEdge(u, v)) return;
    float dist = EuclideanDistance(u, v);
    float cost = dist * weight_factor;

    nodes_[u].edges.push_back(GraphEdge{v, dist, weight_factor, cost});
    nodes_[v].edges.push_back(GraphEdge{u, dist, weight_factor, cost});
    edges_.push_back(EdgeRecord{std::min(u, v), std::max(u, v), dist, weight_factor, cost});
}

void WeightedGraph::ConnectGraph(uint32_t seed) {
    const int n = static_cast<int>(nodes_.size());
    if (n < 2) return;

    std::mt19937 rng(seed);

    // Build Gabriel-like local neighborhood graph + short extra chords
    struct CandidatePair {
        int u;
        int v;
        float d2;
    };
    std::vector<CandidatePair> pairs;
    pairs.reserve(n * (n - 1) / 2);

    for (int u = 0; u < n; ++u) {
        for (int v = u + 1; v < n; ++v) {
            pairs.push_back({u, v, DistSq(nodes_[u], nodes_[v])});
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const CandidatePair& a, const CandidatePair& b) {
        return a.d2 < b.d2;
    });

    // Pass 1: Gabriel graph edges (no third point lies inside the diametral circle of u-v)
    for (const auto& p : pairs) {
        float mx = 0.5f * (nodes_[p.u].x + nodes_[p.v].x);
        float my = 0.5f * (nodes_[p.u].y + nodes_[p.v].y);
        float radius_sq = 0.25f * p.d2;

        bool blocked = false;
        for (int k = 0; k < n; ++k) {
            if (k == p.u || k == p.v) continue;
            float dx = nodes_[k].x - mx;
            float dy = nodes_[k].y - my;
            if (dx * dx + dy * dy < radius_sq * 0.96f) {
                blocked = true;
                break;
            }
        }
        if (!blocked) {
            AddUndirectedEdge(p.u, p.v, SampleWeightFactor(rng));
        }
    }

    // Pass 2: Add a few short non-excessive local chords so A* has rich route choices
    const int target_min_deg = 3;
    for (int u = 0; u < n; ++u) {
        if (static_cast<int>(nodes_[u].edges.size()) >= target_min_deg) continue;
        std::vector<std::pair<float, int>> neighbors;
        neighbors.reserve(n - 1);
        for (int v = 0; v < n; ++v) {
            if (u == v || HasEdge(u, v)) continue;
            neighbors.push_back({DistSq(nodes_[u], nodes_[v]), v});
        }
        std::sort(neighbors.begin(), neighbors.end());
        for (const auto& nb : neighbors) {
            if (static_cast<int>(nodes_[u].edges.size()) >= target_min_deg) break;
            if (static_cast<int>(nodes_[nb.second].edges.size()) >= 6) continue;
            AddUndirectedEdge(u, nb.second, SampleWeightFactor(rng));
        }
    }

    // Pass 3: Ensure single connected component via BFS + shortest bridge
    while (true) {
        std::vector<bool> visited(n, false);
        std::queue<int> q;
        visited[0] = true;
        q.push(0);
        int count = 0;
        while (!q.empty()) {
            int cur = q.front();
            q.pop();
            ++count;
            for (const auto& e : nodes_[cur].edges) {
                if (!visited[e.to]) {
                    visited[e.to] = true;
                    q.push(e.to);
                }
            }
        }
        if (count == n) break;

        int best_u = -1;
        int best_v = -1;
        float best_d2 = std::numeric_limits<float>::max();
        for (int u = 0; u < n; ++u) {
            if (!visited[u]) continue;
            for (int v = 0; v < n; ++v) {
                if (visited[v]) continue;
                float d2 = DistSq(nodes_[u], nodes_[v]);
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best_u = u;
                    best_v = v;
                }
            }
        }
        if (best_u >= 0 && best_v >= 0) {
            AddUndirectedEdge(best_u, best_v, SampleWeightFactor(rng));
        } else {
            break;
        }
    }
}

float WeightedGraph::EuclideanDistance(int u, int v) const {
    float dx = (nodes_[u].x - nodes_[v].x) * kScaleUnits;
    float dy = (nodes_[u].y - nodes_[v].y) * kScaleUnits;
    return std::sqrt(dx * dx + dy * dy);
}

float WeightedGraph::GetEdgeCost(int u, int v) const {
    for (const auto& e : nodes_[u].edges) {
        if (e.to == v) return e.cost;
    }
    return EuclideanDistance(u, v) * 100.0f;
}

float WeightedGraph::GetEdgeWeightFactor(int u, int v) const {
    for (const auto& e : nodes_[u].edges) {
        if (e.to == v) return e.weight_factor;
    }
    return 1.0f;
}

std::pair<int, int> WeightedGraph::PickDefaultStartGoal() const {
    const int n = static_cast<int>(nodes_.size());
    if (n < 2) return {0, 0};

    // Pick a warm left-side outer node as Start and a cool right-side outer node as Goal
    int best_start = 0;
    float best_start_score = std::numeric_limits<float>::max();
    int best_goal = n - 1;
    float best_goal_score = -std::numeric_limits<float>::max();

    for (int i = 0; i < n; ++i) {
        float score = nodes_[i].x - 0.15f * std::abs(nodes_[i].y - 0.5f);
        if (score < best_start_score) {
            best_start_score = score;
            best_start = i;
        }
        float gscore = nodes_[i].x + 0.15f * (1.0f - std::abs(nodes_[i].y - 0.5f));
        if (gscore > best_goal_score) {
            best_goal_score = gscore;
            best_goal = i;
        }
    }

    if (best_start == best_goal) {
        best_goal = (best_start + 1) % n;
    }
    return {best_start, best_goal};
}
