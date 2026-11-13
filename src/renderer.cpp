#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct ColorRGB {
    double r, g, b;
};

ColorRGB LerpColor(ColorRGB a, ColorRGB b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// Color ramp inspired by PULSE: Warm Amber/Gold -> Coral/Rose -> Magenta -> Violet -> Cyan
ColorRGB PulseSpectrum(double t) {
    t = std::clamp(t, 0.0, 1.0);
    const ColorRGB stops[] = {
        {0.99, 0.72, 0.34},  // 0.00: Warm amber gold
        {0.96, 0.37, 0.43},  // 0.25: Coral rose
        {0.82, 0.21, 0.78},  // 0.50: Neon magenta
        {0.44, 0.25, 0.96},  // 0.75: Deep electric violet
        {0.23, 0.80, 0.97},  // 1.00: Ice cyan
    };
    double scaled = t * 4.0;
    int idx = std::min(3, static_cast<int>(scaled));
    double frac = scaled - idx;
    return LerpColor(stops[idx], stops[idx + 1], frac);
}

ColorRGB ColorForScreenX(float norm_x) {
    return PulseSpectrum(std::clamp(static_cast<double>(norm_x - 0.06f) / 0.88, 0.0, 1.0));
}

void RoundedRectPath(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w, h) * 0.5);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -kPi * 0.5, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, kPi * 0.5);
    cairo_arc(cr, x + r, y + h - r, r, kPi * 0.5, kPi);
    cairo_arc(cr, x + r, y + r, r, kPi, kPi * 1.5);
    cairo_close_path(cr);
}

void DrawTrackedText(cairo_t* cr, double x, double y, const std::string& text,
                     double tracking, cairo_pattern_t* pat = nullptr) {
    double cur_x = x;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t char_len = 1;
        if ((c & 0xE0) == 0xC0) char_len = 2;
        else if ((c & 0xF0) == 0xE0) char_len = 3;
        else if ((c & 0xF8) == 0xF0) char_len = 4;
        if (i + char_len > text.size()) char_len = 1;

        std::string glyph = text.substr(i, char_len);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, glyph.c_str(), &ext);
        cairo_move_to(cr, cur_x, y);
        if (pat) cairo_set_source(cr, pat);
        cairo_show_text(cr, glyph.c_str());
        cur_x += ext.x_advance + tracking;
        i += char_len;
    }
}

void DrawGlowLine(cairo_t* cr, double x1, double y1, double x2, double y2,
                  ColorRGB c, double width, double alpha) {
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha * 0.16);
    cairo_set_line_width(cr, width * 3.8);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha * 0.40);
    cairo_set_line_width(cr, width * 1.9);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, std::min(1.0, c.r + 0.25), std::min(1.0, c.g + 0.25),
                          std::min(1.0, c.b + 0.25), alpha * 0.92);
    cairo_set_line_width(cr, width);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void DrawGlowDot(cairo_t* cr, double x, double y, double radius, ColorRGB c, double alpha) {
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    cairo_pattern_t* pat = cairo_pattern_create_radial(x, y, 0.0, x, y, radius);
    cairo_pattern_add_color_stop_rgba(pat, 0.0, 1.0, 0.96, 0.92, alpha);
    cairo_pattern_add_color_stop_rgba(pat, 0.30, c.r, c.g, c.b, alpha * 0.72);
    cairo_pattern_add_color_stop_rgba(pat, 0.68, c.r, c.g, c.b, alpha * 0.20);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, c.r, c.g, c.b, 0.0);
    cairo_set_source(cr, pat);
    cairo_arc(cr, x, y, radius, 0.0, 2.0 * kPi);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);
    cairo_restore(cr);
}

}  // namespace

Renderer::Renderer() {
    InitParticles();
}

Renderer::~Renderer() {
    if (cr_) cairo_destroy(cr_);
    if (surface_) cairo_surface_destroy(surface_);
    if (bg_surface_) cairo_surface_destroy(bg_surface_);
}

void Renderer::InitParticles() {
    std::mt19937 rng(424242);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    particles_.clear();
    particles_.reserve(110);
    for (int i = 0; i < 110; ++i) {
        Particle p;
        p.x = u01(rng);
        p.y = u01(rng);
        p.radius = 1.1f + u01(rng) * 3.0f;
        p.speed = 0.004f + u01(rng) * 0.012f;
        p.phase = u01(rng) * 2.0f * kPi;
        p.alpha = 0.14f + u01(rng) * 0.35f;
        particles_.push_back(p);
    }
}

void Renderer::EnsureSurface(int width, int height) {
    width = std::max(320, width);
    height = std::max(240, height);
    if (surface_ && width_ == width && height_ == height) return;

    if (cr_) cairo_destroy(cr_);
    if (surface_) cairo_surface_destroy(surface_);

    width_ = width;
    height_ = height;
    surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width_, height_);
    cr_ = cairo_create(surface_);
    BuildBackgroundCache(width_, height_);
}

void Renderer::BuildBackgroundCache(int width, int height) {
    if (bg_surface_) cairo_surface_destroy(bg_surface_);
    bg_w_ = width;
    bg_h_ = height;
    bg_surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* bcr = cairo_create(bg_surface_);

    double cx = width * 0.5;
    double cy = height * 0.48;
    double max_r = std::hypot(width * 0.65, height * 0.65);

    // Deep cosmic background gradient
    cairo_pattern_t* bg_grad = cairo_pattern_create_radial(cx, cy, 10.0, cx, cy, max_r);
    cairo_pattern_add_color_stop_rgb(bg_grad, 0.00, 0.13, 0.07, 0.22);
    cairo_pattern_add_color_stop_rgb(bg_grad, 0.38, 0.07, 0.04, 0.14);
    cairo_pattern_add_color_stop_rgb(bg_grad, 0.78, 0.04, 0.02, 0.08);
    cairo_pattern_add_color_stop_rgb(bg_grad, 1.00, 0.02, 0.01, 0.05);
    cairo_set_source(bcr, bg_grad);
    cairo_paint(bcr);
    cairo_pattern_destroy(bg_grad);

    // Warm left & cool right subtle atmospheric washes
    cairo_save(bcr);
    cairo_set_operator(bcr, CAIRO_OPERATOR_ADD);
    cairo_pattern_t* warm = cairo_pattern_create_radial(width * 0.22, cy, 10.0,
                                                        width * 0.22, cy, width * 0.48);
    cairo_pattern_add_color_stop_rgba(warm, 0.0, 0.35, 0.11, 0.08, 0.16);
    cairo_pattern_add_color_stop_rgba(warm, 1.0, 0.0, 0.0, 0.0, 0.0);
    cairo_set_source(bcr, warm);
    cairo_paint(bcr);
    cairo_pattern_destroy(warm);

    cairo_pattern_t* cool = cairo_pattern_create_radial(width * 0.78, cy, 10.0,
                                                        width * 0.78, cy, width * 0.48);
    cairo_pattern_add_color_stop_rgba(cool, 0.0, 0.06, 0.16, 0.36, 0.16);
    cairo_pattern_add_color_stop_rgba(cool, 1.0, 0.0, 0.0, 0.0, 0.0);
    cairo_set_source(bcr, cool);
    cairo_paint(bcr);
    cairo_pattern_destroy(cool);
    cairo_restore(bcr);

    // Subtle concentric polar reference rings
    double base_r = std::min(width, height) * 0.14;
    for (int i = 1; i <= 5; ++i) {
        double r = base_r * i * 0.78;
        cairo_set_source_rgba(bcr, 0.65, 0.55, 0.85, (i == 2) ? 0.09 : 0.045);
        cairo_set_line_width(bcr, 1.0);
        cairo_arc(bcr, cx, cy, r, 0.0, 2.0 * kPi);
        cairo_stroke(bcr);
    }

    // Fine radial spoke hints
    cairo_set_line_width(bcr, 0.8);
    for (int i = 0; i < 36; ++i) {
        double ang = (2.0 * kPi * i) / 36.0;
        double r0 = base_r * 0.75;
        double r1 = base_r * 3.85;
        cairo_set_source_rgba(bcr, 0.58, 0.46, 0.82, (i % 3 == 0) ? 0.05 : 0.022);
        cairo_move_to(bcr, cx + std::cos(ang) * r0, cy + std::sin(ang) * r0);
        cairo_line_to(bcr, cx + std::cos(ang) * r1, cy + std::sin(ang) * r1);
        cairo_stroke(bcr);
    }

    cairo_destroy(bcr);
}

void Renderer::MapNodeToScreen(const GraphNode& node, float& sx, float& sy) const {
    float pad_x = 40.0f;
    float pad_top = 68.0f;
    float pad_bot = 104.0f;
    float usable_w = std::max(100.0f, static_cast<float>(width_) - 2.0f * pad_x);
    float usable_h = std::max(100.0f, static_cast<float>(height_) - pad_top - pad_bot);
    sx = pad_x + node.x * usable_w;
    sy = pad_top + node.y * usable_h;
}

int Renderer::HitTestNode(const WeightedGraph& graph, float px, float py) const {
    const auto& nodes = graph.nodes();
    int best_id = -1;
    float best_d2 = 22.0f * 22.0f;
    for (const auto& n : nodes) {
        float sx = 0.0f, sy = 0.0f;
        MapNodeToScreen(n, sx, sy);
        float dx = px - sx;
        float dy = py - sy;
        float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best_id = n.id;
        }
    }
    return best_id;
}

void Renderer::DrawAmbientParticles(cairo_t* cr, float time_sec) const {
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    for (const auto& p : particles_) {
        float px = std::fmod(p.x + time_sec * p.speed * 0.35f, 1.0f);
        float py = p.y + std::sin(time_sec * 0.6f + p.phase) * 0.012f;
        ColorRGB col = ColorForScreenX(px);
        double sx = px * width_;
        double sy = py * height_;
        double twinkle = 0.65 + 0.35 * std::sin(time_sec * 1.8f + p.phase);

        cairo_pattern_t* pat = cairo_pattern_create_radial(sx, sy, 0.0, sx, sy, p.radius * 3.2);
        cairo_pattern_add_color_stop_rgba(pat, 0.0, col.r, col.g, col.b, p.alpha * twinkle);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, col.r, col.g, col.b, 0.0);
        cairo_set_source(cr, pat);
        cairo_arc(cr, sx, sy, p.radius * 3.2, 0.0, 2.0 * kPi);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
    }
    cairo_restore(cr);
}

void Renderer::DrawDecorativePulseRing(cairo_t* cr, float time_sec,
                                       const AStarRunner& runner) const {
    double cx = width_ * 0.5;
    double cy = height_ * 0.48;
    double ring_r = std::min(width_, height_) * 0.39;
    const int num_bars = 96;

    double energy = 0.25;
    if (runner.state() == RunnerState::Searching) energy = 0.65;
    if (runner.state() == RunnerState::Succeeded) energy = 0.90;

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width(cr, 1.8);

    for (int i = 0; i < num_bars; ++i) {
        double ang = (2.0 * kPi * i) / num_bars;
        double wave = 0.5 + 0.5 * std::sin(ang * 5.0 - time_sec * 2.2 +
                                           runner.step_count() * 0.25);
        double bar_len = 6.0 + energy * (10.0 + 18.0 * wave);
        double t = 0.5 + 0.5 * std::cos(ang + kPi);
        ColorRGB c = PulseSpectrum(t);

        double x0 = cx + std::cos(ang) * ring_r;
        double y0 = cy + std::sin(ang) * ring_r;
        double x1 = cx + std::cos(ang) * (ring_r + bar_len);
        double y1 = cy + std::sin(ang) * (ring_r + bar_len);

        cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.13);
        cairo_move_to(cr, x0, y0);
        cairo_line_to(cr, x1, y1);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

void Renderer::DrawEdges(cairo_t* cr, float time_sec, const WeightedGraph& graph,
                         const AStarRunner& runner, const UiState& ui) const {
    const auto& nodes = graph.nodes();
    const auto& edges = graph.edges();
    const auto& vis = runner.node_vis();

    auto is_tree_edge = [&](int u, int v) -> bool {
        if (u < static_cast<int>(vis.size()) && vis[u].parent_id == v &&
            vis[u].status != NodeStatus::Unvisited) {
            return true;
        }
        if (v < static_cast<int>(vis.size()) && vis[v].parent_id == u &&
            vis[v].status != NodeStatus::Unvisited) {
            return true;
        }
        return false;
    };

    // Pass 1: Base graph edges (styled by weight factor)
    for (const auto& e : edges) {
        float x1, y1, x2, y2;
        MapNodeToScreen(nodes[e.u], x1, y1);
        MapNodeToScreen(nodes[e.v], x2, y2);

        double w_norm = std::clamp((e.weight_factor - 1.0f) / 3.2f, 0.0f, 1.0f);
        float mid_x = 0.5f * (nodes[e.u].x + nodes[e.v].x);
        ColorRGB base_col = ColorForScreenX(mid_x);

        cairo_save(cr);
        if (e.weight_factor > 2.4f) {
            double dashes[2] = {4.0, 5.0};
            cairo_set_dash(cr, dashes, 2, 0.0);
            cairo_set_line_width(cr, 1.1);
            cairo_set_source_rgba(cr, base_col.r * 0.65, base_col.g * 0.55, base_col.b * 0.75, 0.22);
        } else {
            double thickness = 2.1 - 0.9 * w_norm;
            double alpha = 0.36 - 0.14 * w_norm;
            cairo_set_line_width(cr, thickness);
            cairo_set_source_rgba(cr, base_col.r, base_col.g, base_col.b, alpha);
        }
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    // Pass 2: Explored search tree edges
    for (const auto& e : edges) {
        if (!is_tree_edge(e.u, e.v)) continue;
        float x1, y1, x2, y2;
        MapNodeToScreen(nodes[e.u], x1, y1);
        MapNodeToScreen(nodes[e.v], x2, y2);
        float mid_x = 0.5f * (nodes[e.u].x + nodes[e.v].x);
        ColorRGB col = ColorForScreenX(mid_x);
        DrawGlowLine(cr, x1, y1, x2, y2, col, 1.6, 0.65);
    }

    // Pass 3: Current expanding node's relaxed edges
    int exp_id = runner.current_expanding();
    if (exp_id >= 0 && exp_id < graph.NodeCount()) {
        float x1, y1;
        MapNodeToScreen(nodes[exp_id], x1, y1);
        for (int nb : runner.active_neighbors()) {
            float x2, y2;
            MapNodeToScreen(nodes[nb], x2, y2);
            ColorRGB hot{0.99, 0.65, 0.35};
            DrawGlowLine(cr, x1, y1, x2, y2, hot, 2.3, 0.95);
        }
    }

    // Pass 4: Final Optimal Solution Path with animated energy pulse
    const auto& path = runner.solution_path();
    if (path.size() >= 2) {
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            int u = path[i];
            int v = path[i + 1];
            float x1, y1, x2, y2;
            MapNodeToScreen(nodes[u], x1, y1);
            MapNodeToScreen(nodes[v], x2, y2);

            double seg_t = static_cast<double>(i) / static_cast<double>(path.size() - 1);
            ColorRGB c = PulseSpectrum(seg_t);
            DrawGlowLine(cr, x1, y1, x2, y2, c, 3.2, 1.0);

            // Animated traveling pulse orb along the path
            double travel = std::fmod(time_sec * 1.4 - seg_t * 1.5 + 10.0, 1.0);
            if (travel >= 0.0 && travel <= 1.0) {
                double px = x1 + (x2 - x1) * travel;
                double py = y1 + (y2 - y1) * travel;
                DrawGlowDot(cr, px, py, 9.0, c, 0.85);
            }
        }
    }

    // Pass 5: Edge weight badges (when toggled on, or adjacent to hovered node)
    if (ui.show_weights || ui.hovered_node >= 0) {
        cairo_select_font_face(cr, "Menlo", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9.5);
        for (const auto& e : edges) {
            bool near_hover = (ui.hovered_node >= 0 && (e.u == ui.hovered_node || e.v == ui.hovered_node));
            if (!ui.show_weights && !near_hover) continue;
            // For dense graphs (>110 nodes), show weights only near hovered/path/active edges
            if (graph.NodeCount() > 110 && !near_hover && !is_tree_edge(e.u, e.v)) continue;

            float x1, y1, x2, y2;
            MapNodeToScreen(nodes[e.u], x1, y1);
            MapNodeToScreen(nodes[e.v], x2, y2);
            double mx = 0.5 * (x1 + x2);
            double my = 0.5 * (y1 + y2);

            char label[16];
            std::snprintf(label, sizeof(label), "%.1fx", e.weight_factor);

            cairo_text_extents_t ext;
            cairo_text_extents(cr, label, &ext);
            double bw = ext.width + 8.0;
            double bh = 14.0;

            RoundedRectPath(cr, mx - bw * 0.5, my - bh * 0.5, bw, bh, 5.0);
            cairo_set_source_rgba(cr, 0.05, 0.03, 0.11, near_hover ? 0.92 : 0.72);
            cairo_fill_preserve(cr);

            if (e.weight_factor < 1.35f) {
                cairo_set_source_rgba(cr, 0.25, 0.85, 0.95, near_hover ? 0.9 : 0.45);
            } else if (e.weight_factor > 2.5f) {
                cairo_set_source_rgba(cr, 0.98, 0.42, 0.38, near_hover ? 0.9 : 0.45);
            } else {
                cairo_set_source_rgba(cr, 0.72, 0.52, 0.92, near_hover ? 0.9 : 0.32);
            }
            cairo_set_line_width(cr, 0.8);
            cairo_stroke(cr);

            cairo_set_source_rgba(cr, 0.92, 0.90, 0.98, near_hover ? 1.0 : 0.78);
            cairo_move_to(cr, mx - ext.width * 0.5, my + ext.height * 0.48);
            cairo_show_text(cr, label);
        }
    }
}

void Renderer::DrawNodes(cairo_t* cr, float time_sec, const WeightedGraph& graph,
                         const AStarRunner& runner, const UiState& ui) const {
    const auto& nodes = graph.nodes();
    const auto& vis = runner.node_vis();
    float density_scale = std::clamp(1.15f - graph.NodeCount() * 0.0028f, 0.62f, 1.15f);

    for (const auto& n : nodes) {
        float sx, sy;
        MapNodeToScreen(n, sx, sy);

        NodeStatus st = (n.id < static_cast<int>(vis.size())) ? vis[n.id].status : NodeStatus::Unvisited;
        bool is_start = (n.id == runner.start_id());
        bool is_goal = (n.id == runner.goal_id());
        bool is_hover = (n.id == ui.hovered_node);

        ColorRGB col = ColorForScreenX(n.x);
        double base_r = 6.2 * density_scale;

        if (st == NodeStatus::Closed) {
            DrawGlowDot(cr, sx, sy, base_r * 2.5, col, 0.48);
            cairo_set_source_rgba(cr, col.r, col.g, col.b, 0.85);
            cairo_arc(cr, sx, sy, base_r * 0.85, 0.0, 2.0 * kPi);
            cairo_fill(cr);
        } else if (st == NodeStatus::Open) {
            double pulse = 1.0 + 0.22 * std::sin(time_sec * 4.5 + vis[n.id].pulse_phase);
            DrawGlowDot(cr, sx, sy, base_r * 3.1 * pulse, ColorRGB{0.96, 0.40, 0.65}, 0.75);
            cairo_set_source_rgba(cr, 0.98, 0.78, 0.90, 0.95);
            cairo_arc(cr, sx, sy, base_r * 0.95, 0.0, 2.0 * kPi);
            cairo_fill(cr);

            cairo_set_source_rgba(cr, 0.99, 0.56, 0.42, 0.78);
            cairo_set_line_width(cr, 1.4);
            cairo_arc(cr, sx, sy, base_r * 1.65 * pulse, 0.0, 2.0 * kPi);
            cairo_stroke(cr);
        } else if (st == NodeStatus::CurrentExpanding) {
            double pulse = 1.0 + 0.18 * std::sin(time_sec * 8.0);
            DrawGlowDot(cr, sx, sy, base_r * 5.2 * pulse, ColorRGB{0.99, 0.74, 0.35}, 1.0);
            cairo_set_source_rgba(cr, 1.0, 0.98, 0.94, 1.0);
            cairo_arc(cr, sx, sy, base_r * 1.25, 0.0, 2.0 * kPi);
            cairo_fill(cr);
        } else if (st == NodeStatus::OnPath) {
            DrawGlowDot(cr, sx, sy, base_r * 3.6, col, 0.92);
            cairo_set_source_rgba(cr, 1.0, 0.97, 0.95, 1.0);
            cairo_arc(cr, sx, sy, base_r * 1.08, 0.0, 2.0 * kPi);
            cairo_fill(cr);
        } else {
            // Unvisited node
            cairo_set_source_rgba(cr, 0.07, 0.04, 0.14, 0.92);
            cairo_arc(cr, sx, sy, base_r * 0.85, 0.0, 2.0 * kPi);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, col.r, col.g, col.b, 0.55);
            cairo_set_line_width(cr, 1.3);
            cairo_arc(cr, sx, sy, base_r * 0.85, 0.0, 2.0 * kPi);
            cairo_stroke(cr);
        }

        if (is_hover) {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
            cairo_set_line_width(cr, 1.6);
            cairo_arc(cr, sx, sy, base_r * 2.1, 0.0, 2.0 * kPi);
            cairo_stroke(cr);
        }

        if (is_start || is_goal) {
            ColorRGB beacon = is_start ? ColorRGB{0.99, 0.72, 0.34} : ColorRGB{0.23, 0.85, 0.98};
            double ring_r = base_r * 2.15 + 1.5 * std::sin(time_sec * 3.2);
            DrawGlowDot(cr, sx, sy, base_r * 4.8, beacon, 0.95);

            cairo_set_source_rgba(cr, beacon.r, beacon.g, beacon.b, 0.95);
            cairo_set_line_width(cr, 2.2);
            cairo_arc(cr, sx, sy, ring_r, 0.0, 2.0 * kPi);
            cairo_stroke(cr);

            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
            cairo_arc(cr, sx, sy, base_r * 1.15, 0.0, 2.0 * kPi);
            cairo_fill(cr);

            // Floating tag
            const char* tag = is_start ? "START" : "GOAL";
            cairo_select_font_face(cr, "Menlo", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 10.0);
            cairo_text_extents_t ext;
            cairo_text_extents(cr, tag, &ext);
            double tw = ext.width + 12.0;
            double th = 17.0;
            double tx = sx - tw * 0.5;
            double ty = sy - ring_r - th - 5.0;
            if (ty < 56.0) ty = sy + ring_r + 6.0;

            RoundedRectPath(cr, tx, ty, tw, th, 8.5);
            cairo_set_source_rgba(cr, 0.06, 0.03, 0.14, 0.90);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, beacon.r, beacon.g, beacon.b, 0.85);
            cairo_set_line_width(cr, 1.1);
            cairo_stroke(cr);

            cairo_set_source_rgba(cr, beacon.r, beacon.g, beacon.b, 1.0);
            cairo_move_to(cr, tx + 6.0, ty + 12.2);
            cairo_show_text(cr, tag);
        }
    }
}

void Renderer::DrawHud(cairo_t* cr, float time_sec, const WeightedGraph& graph,
                       const AStarRunner& runner, const UiState& ui) {
    // 1. Top-left title & subtitle (PULSE aesthetic)
    {
        cairo_select_font_face(cr, "Helvetica Neue Light", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 34.0);
        cairo_pattern_t* title_grad = cairo_pattern_create_linear(28.0, 0.0, 260.0, 0.0);
        cairo_pattern_add_color_stop_rgb(title_grad, 0.0, 0.99, 0.72, 0.34);
        cairo_pattern_add_color_stop_rgb(title_grad, 0.55, 0.93, 0.35, 0.56);
        cairo_pattern_add_color_stop_rgb(title_grad, 1.0, 0.56, 0.42, 0.96);
        DrawTrackedText(cr, 28.0, 44.0, "ASTAR", 18.0, title_grad);
        cairo_pattern_destroy(title_grad);

        cairo_select_font_face(cr, "Menlo", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10.5);
        cairo_set_source_rgba(cr, 0.78, 0.75, 0.88, 0.78);
        DrawTrackedText(cr, 30.0, 64.0,
                        "WEIGHTED GRAPH PATHFINDER  ·  STLASTAR ENGINE  ·  C++17", 1.8);
    }

    // 2. Top-right telemetry glass card with vertical gradient meters
    {
        double card_w = 278.0;
        double card_h = 104.0;
        double card_x = width_ - card_w - 24.0;
        double card_y = 20.0;

        RoundedRectPath(cr, card_x, card_y, card_w, card_h, 14.0);
        cairo_set_source_rgba(cr, 0.07, 0.04, 0.14, 0.82);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.72, 0.58, 0.92, 0.22);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        cairo_select_font_face(cr, "Menlo", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10.5);

        const char* state_str = "READY";
        ColorRGB state_col{0.75, 0.75, 0.92};
        if (runner.state() == RunnerState::Searching) {
            state_str = "SEARCHING";
            state_col = {0.99, 0.70, 0.35};
        } else if (runner.state() == RunnerState::Succeeded) {
            state_str = "OPTIMAL";
            state_col = {0.26, 0.86, 0.98};
        } else if (runner.state() == RunnerState::Failed) {
            state_str = "NO PATH";
            state_col = {0.98, 0.35, 0.42};
        }

        char buf_nodes[32], buf_step[32], buf_cost[32];
        std::snprintf(buf_nodes, sizeof(buf_nodes), "%03d / %03dE",
                      graph.NodeCount(), graph.EdgeCount());
        std::snprintf(buf_step, sizeof(buf_step), "%03d", runner.step_count());
        if (runner.state() == RunnerState::Succeeded) {
            std::snprintf(buf_cost, sizeof(buf_cost), "%.1f", runner.solution_cost());
        } else {
            std::snprintf(buf_cost, sizeof(buf_cost), "---");
        }

        const char* labels[4] = {"GRAPH", "STEP", "STATE", "COST"};
        const char* vals[4] = {buf_nodes, buf_step, state_str, buf_cost};
        for (int i = 0; i < 4; ++i) {
            double ry = card_y + 24.0 + i * 20.0;
            cairo_set_source_rgba(cr, 0.58, 0.54, 0.72, 0.80);
            cairo_move_to(cr, card_x + 16.0, ry);
            cairo_show_text(cr, labels[i]);

            if (i == 2) {
                cairo_set_source_rgba(cr, state_col.r, state_col.g, state_col.b, 0.95);
            } else {
                cairo_set_source_rgba(cr, 0.94, 0.92, 0.98, 0.95);
            }
            cairo_move_to(cr, card_x + 76.0, ry);
            cairo_show_text(cr, vals[i]);
        }

        // 3 vertical meters: OPN, CLS, PTH
        double total_n = std::max(1, graph.NodeCount());
        double ratios[3] = {
            std::clamp(runner.open_count() / total_n * 2.2, 0.08, 1.0),
            std::clamp(runner.closed_count() / total_n * 1.3, 0.08, 1.0),
            runner.solution_path().empty()
                ? 0.08
                : std::clamp(runner.solution_path().size() / (total_n * 0.45), 0.15, 1.0),
        };
        const char* m_labels[3] = {"OPN", "CLS", "PTH"};
        ColorRGB m_top[3] = {{0.99, 0.72, 0.34}, {0.86, 0.25, 0.78}, {0.25, 0.85, 0.98}};
        ColorRGB m_bot[3] = {{0.95, 0.32, 0.45}, {0.42, 0.22, 0.92}, {0.35, 0.45, 0.98}};

        for (int m = 0; m < 3; ++m) {
            double mx = card_x + 194.0 + m * 24.0;
            double my = card_y + 18.0;
            double mw = 8.0;
            double mh = 56.0;

            RoundedRectPath(cr, mx, my, mw, mh, 4.0);
            cairo_set_source_rgba(cr, 0.16, 0.11, 0.26, 0.85);
            cairo_fill(cr);

            double fill_h = mh * ratios[m];
            double fy = my + (mh - fill_h);
            RoundedRectPath(cr, mx, fy, mw, fill_h, 4.0);
            cairo_pattern_t* pat = cairo_pattern_create_linear(mx, my, mx, my + mh);
            cairo_pattern_add_color_stop_rgb(pat, 0.0, m_top[m].r, m_top[m].g, m_top[m].b);
            cairo_pattern_add_color_stop_rgb(pat, 1.0, m_bot[m].r, m_bot[m].g, m_bot[m].b);
            cairo_set_source(cr, pat);
            cairo_fill(cr);
            cairo_pattern_destroy(pat);

            cairo_set_font_size(cr, 8.5);
            cairo_set_source_rgba(cr, 0.65, 0.60, 0.78, 0.85);
            cairo_move_to(cr, mx - 4.0, card_y + 90.0);
            cairo_show_text(cr, m_labels[m]);
        }
    }

    // 3. Bottom-left node inspector & phase bar
    {
        cairo_select_font_face(cr, "Menlo", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10.5);
        char info[128];
        if (ui.hovered_node >= 0 && ui.hovered_node < graph.NodeCount()) {
            const auto& nv = runner.node_vis()[ui.hovered_node];
            if (nv.status == NodeStatus::Unvisited) {
                std::snprintf(info, sizeof(info), "NODE #%02d  ·  UNVISITED  ·  DEG %zu",
                              ui.hovered_node, graph.nodes()[ui.hovered_node].edges.size());
            } else {
                std::snprintf(info, sizeof(info),
                              "NODE #%02d  ·  g:%.0f  h:%.0f  f:%.0f",
                              ui.hovered_node, nv.g, nv.h, nv.f);
            }
        } else {
            std::snprintf(info, sizeof(info), "CLICK NODE: START  ·  RIGHT-CLICK: GOAL");
        }
        cairo_set_source_rgba(cr, 0.78, 0.75, 0.90, 0.82);
        cairo_move_to(cr, 28.0, height_ - 34.0);
        cairo_show_text(cr, info);

        // Segmented progress bar under bottom-left label
        double progress = (runner.state() == RunnerState::Succeeded)
                              ? 1.0
                              : std::clamp(static_cast<double>(runner.closed_count()) /
                                               std::max(1, graph.NodeCount()),
                                           0.05, 0.95);
        for (int seg = 0; seg < 6; ++seg) {
            double sx = 28.0 + seg * 26.0;
            double sy = height_ - 22.0;
            RoundedRectPath(cr, sx, sy, 22.0, 3.0, 1.5);
            double seg_t = static_cast<double>(seg) / 5.0;
            if (seg_t <= progress) {
                ColorRGB sc = PulseSpectrum(seg_t);
                cairo_set_source_rgba(cr, sc.r, sc.g, sc.b, 0.92);
            } else {
                cairo_set_source_rgba(cr, 0.30, 0.25, 0.45, 0.45);
            }
            cairo_fill(cr);
        }
    }

    // 4. Bottom-center floating pill dock
    {
        double dock_w = std::min(790.0, width_ - 40.0);
        double dock_h = 54.0;
        double dock_x = (width_ - dock_w) * 0.5;
        double dock_y = height_ - dock_h - 18.0;

        RoundedRectPath(cr, dock_x, dock_y, dock_w, dock_h, 27.0);
        cairo_set_source_rgba(cr, 0.06, 0.03, 0.13, 0.88);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.72, 0.56, 0.92, 0.26);
        cairo_set_line_width(cr, 1.1);
        cairo_stroke(cr);

        double cx = dock_x + 28.0;
        double cy = dock_y + dock_h * 0.5;

        // Play / Pause circle button
        {
            double r = 19.0;
            hitboxes_.play_button = {static_cast<float>(cx - r), static_cast<float>(cy - r),
                                     static_cast<float>(r * 2.0), static_cast<float>(r * 2.0)};

            cairo_pattern_t* ring = cairo_pattern_create_linear(cx - r, cy - r, cx + r, cy + r);
            cairo_pattern_add_color_stop_rgb(ring, 0.0, 0.99, 0.70, 0.35);
            cairo_pattern_add_color_stop_rgb(ring, 0.5, 0.88, 0.28, 0.68);
            cairo_pattern_add_color_stop_rgb(ring, 1.0, 0.24, 0.82, 0.98);

            cairo_set_source_rgba(cr, 0.12, 0.08, 0.22, 0.95);
            cairo_arc(cr, cx, cy, r, 0.0, 2.0 * kPi);
            cairo_fill(cr);

            cairo_set_source(cr, ring);
            cairo_set_line_width(cr, 2.0);
            cairo_arc(cr, cx, cy, r, 0.0, 2.0 * kPi);
            cairo_stroke(cr);
            cairo_pattern_destroy(ring);

            cairo_set_source_rgba(cr, 0.98, 0.96, 1.0, 0.95);
            if (ui.is_playing && runner.state() == RunnerState::Searching) {
                // Pause bars
                RoundedRectPath(cr, cx - 5.0, cy - 6.0, 3.5, 12.0, 1.0);
                cairo_fill(cr);
                RoundedRectPath(cr, cx + 1.5, cy - 6.0, 3.5, 12.0, 1.0);
                cairo_fill(cr);
            } else {
                // Play triangle
                cairo_move_to(cr, cx - 4.0, cy - 6.5);
                cairo_line_to(cr, cx + 6.5, cy);
                cairo_line_to(cr, cx - 4.0, cy + 6.5);
                cairo_close_path(cr);
                cairo_fill(cr);
            }
        }

        // Step button
        double x_cursor = dock_x + 56.0;
        {
            double bw = 34.0, bh = 28.0;
            double by = cy - bh * 0.5;
            hitboxes_.step_button = {static_cast<float>(x_cursor), static_cast<float>(by),
                                     static_cast<float>(bw), static_cast<float>(bh)};
            RoundedRectPath(cr, x_cursor, by, bw, bh, 14.0);
            cairo_set_source_rgba(cr, 0.14, 0.10, 0.24, 0.9);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.72, 0.58, 0.90, 0.28);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            // Step icon (>|)
            cairo_set_source_rgba(cr, 0.92, 0.90, 0.98, 0.92);
            cairo_move_to(cr, x_cursor + 10.0, cy - 4.5);
            cairo_line_to(cr, x_cursor + 18.0, cy);
            cairo_line_to(cr, x_cursor + 10.0, cy + 4.5);
            cairo_close_path(cr);
            cairo_fill(cr);
            cairo_rectangle(cr, x_cursor + 20.0, cy - 4.5, 2.2, 9.0);
            cairo_fill(cr);
            x_cursor += bw + 10.0;
        }

        // 3 Layout pills (RADIAL, ORGANIC, MESH)
        const char* layout_names[3] = {"RADIAL", "ORGANIC", "MESH"};
        cairo_select_font_face(cr, "Menlo", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 9.5);
        for (int i = 0; i < 3; ++i) {
            double bw = 66.0, bh = 28.0;
            double by = cy - bh * 0.5;
            hitboxes_.layout_buttons[i] = {static_cast<float>(x_cursor), static_cast<float>(by),
                                           static_cast<float>(bw), static_cast<float>(bh)};
            bool active = (static_cast<int>(ui.layout) == i);

            RoundedRectPath(cr, x_cursor, by, bw, bh, 14.0);
            if (active) {
                cairo_pattern_t* pill = cairo_pattern_create_linear(x_cursor, by, x_cursor + bw, by);
                cairo_pattern_add_color_stop_rgb(pill, 0.0, 0.99, 0.65, 0.34);
                cairo_pattern_add_color_stop_rgb(pill, 1.0, 0.92, 0.34, 0.58);
                cairo_set_source(cr, pill);
                cairo_fill(cr);
                cairo_pattern_destroy(pill);
            } else {
                cairo_set_source_rgba(cr, 0.13, 0.09, 0.22, 0.70);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 0.65, 0.55, 0.85, 0.18);
                cairo_set_line_width(cr, 1.0);
                cairo_stroke(cr);
            }

            cairo_text_extents_t ext;
            cairo_text_extents(cr, layout_names[i], &ext);
            if (active) {
                cairo_set_source_rgba(cr, 0.08, 0.03, 0.12, 0.96);
            } else {
                cairo_set_source_rgba(cr, 0.78, 0.75, 0.88, 0.82);
            }
            cairo_move_to(cr, x_cursor + (bw - ext.width) * 0.5, cy + ext.height * 0.45);
            cairo_show_text(cr, layout_names[i]);
            x_cursor += bw + 6.0;
        }

        // Divider
        x_cursor += 4.0;
        cairo_set_source_rgba(cr, 0.65, 0.55, 0.85, 0.20);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, x_cursor, cy - 13.0);
        cairo_line_to(cr, x_cursor, cy + 13.0);
        cairo_stroke(cr);
        x_cursor += 12.0;

        // Graph Size Slider (12..220)
        {
            double sw = 98.0;
            hitboxes_.size_slider_track = {static_cast<float>(x_cursor), static_cast<float>(cy - 10.0),
                                           static_cast<float>(sw), 24.0f};
            double t = std::clamp((ui.target_node_count - 12.0) / (220.0 - 12.0), 0.0, 1.0);

            cairo_set_font_size(cr, 8.5);
            cairo_set_source_rgba(cr, 0.74, 0.70, 0.86, 0.85);
            char sz_lbl[24];
            std::snprintf(sz_lbl, sizeof(sz_lbl), "SIZE %d", ui.target_node_count);
            cairo_move_to(cr, x_cursor, cy - 6.0);
            cairo_show_text(cr, sz_lbl);

            double track_y = cy + 6.0;
            RoundedRectPath(cr, x_cursor, track_y - 2.0, sw, 4.0, 2.0);
            cairo_set_source_rgba(cr, 0.22, 0.16, 0.36, 0.90);
            cairo_fill(cr);

            RoundedRectPath(cr, x_cursor, track_y - 2.0, std::max(4.0, sw * t), 4.0, 2.0);
            cairo_pattern_t* sg = cairo_pattern_create_linear(x_cursor, track_y, x_cursor + sw, track_y);
            cairo_pattern_add_color_stop_rgb(sg, 0.0, 0.99, 0.72, 0.34);
            cairo_pattern_add_color_stop_rgb(sg, 1.0, 0.92, 0.32, 0.68);
            cairo_set_source(cr, sg);
            cairo_fill(cr);
            cairo_pattern_destroy(sg);

            double knob_x = x_cursor + sw * t;
            DrawGlowDot(cr, knob_x, track_y, 9.0, ColorRGB{0.98, 0.65, 0.45}, 0.75);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.98);
            cairo_arc(cr, knob_x, track_y, 5.2, 0.0, 2.0 * kPi);
            cairo_fill(cr);
            x_cursor += sw + 14.0;
        }

        // Speed Slider (1..40 steps/s)
        {
            double sw = 84.0;
            hitboxes_.speed_slider_track = {static_cast<float>(x_cursor), static_cast<float>(cy - 10.0),
                                            static_cast<float>(sw), 24.0f};
            double t = std::clamp((ui.steps_per_sec - 1.0) / 39.0, 0.0, 1.0);

            cairo_set_font_size(cr, 8.5);
            cairo_set_source_rgba(cr, 0.74, 0.70, 0.86, 0.85);
            char sp_lbl[24];
            std::snprintf(sp_lbl, sizeof(sp_lbl), "SPD %.0f/s", ui.steps_per_sec);
            cairo_move_to(cr, x_cursor, cy - 6.0);
            cairo_show_text(cr, sp_lbl);

            double track_y = cy + 6.0;
            RoundedRectPath(cr, x_cursor, track_y - 2.0, sw, 4.0, 2.0);
            cairo_set_source_rgba(cr, 0.22, 0.16, 0.36, 0.90);
            cairo_fill(cr);

            RoundedRectPath(cr, x_cursor, track_y - 2.0, std::max(4.0, sw * t), 4.0, 2.0);
            cairo_pattern_t* sg = cairo_pattern_create_linear(x_cursor, track_y, x_cursor + sw, track_y);
            cairo_pattern_add_color_stop_rgb(sg, 0.0, 0.76, 0.28, 0.86);
            cairo_pattern_add_color_stop_rgb(sg, 1.0, 0.24, 0.82, 0.98);
            cairo_set_source(cr, sg);
            cairo_fill(cr);
            cairo_pattern_destroy(sg);

            double knob_x = x_cursor + sw * t;
            DrawGlowDot(cr, knob_x, track_y, 9.0, ColorRGB{0.34, 0.78, 0.98}, 0.75);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.98);
            cairo_arc(cr, knob_x, track_y, 5.2, 0.0, 2.0 * kPi);
            cairo_fill(cr);
            x_cursor += sw + 12.0;
        }

        // Action buttons: WEIGHTS, NEW, RESET
        struct BtnSpec {
            const char* label;
            RectF* box;
            bool highlight;
            double w;
        } btns[3] = {
            {"WGTS", &hitboxes_.weights_button, ui.show_weights, 48.0},
            {"NEW", &hitboxes_.new_graph_button, false, 44.0},
            {"RST", &hitboxes_.reset_button, false, 44.0},
        };

        cairo_set_font_size(cr, 9.0);
        for (const auto& b : btns) {
            double bh = 28.0;
            double by = cy - bh * 0.5;
            *b.box = {static_cast<float>(x_cursor), static_cast<float>(by),
                      static_cast<float>(b.w), static_cast<float>(bh)};
            RoundedRectPath(cr, x_cursor, by, b.w, bh, 14.0);
            if (b.highlight) {
                cairo_set_source_rgba(cr, 0.26, 0.58, 0.85, 0.35);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 0.25, 0.82, 0.98, 0.65);
            } else {
                cairo_set_source_rgba(cr, 0.14, 0.10, 0.24, 0.88);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 0.72, 0.58, 0.90, 0.25);
            }
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            cairo_text_extents_t ext;
            cairo_text_extents(cr, b.label, &ext);
            cairo_set_source_rgba(cr, 0.92, 0.90, 0.98, 0.92);
            cairo_move_to(cr, x_cursor + (b.w - ext.width) * 0.5, cy + ext.height * 0.45);
            cairo_show_text(cr, b.label);
            x_cursor += b.w + 6.0;
        }
    }

    // 5. Bottom-right compact shortcuts legend
    if (width_ >= 1180) {
        cairo_select_font_face(cr, "Menlo", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9.5);
        cairo_set_source_rgba(cr, 0.64, 0.58, 0.78, 0.70);
        cairo_move_to(cr, width_ - 250.0, height_ - 38.0);
        cairo_show_text(cr, "SPACE PLAY/PAUSE  ·  S STEP");
        cairo_move_to(cr, width_ - 250.0, height_ - 22.0);
        cairo_show_text(cr, "R NEW GRAPH  ·  W WEIGHTS");
    }

    (void)time_sec;
}

void Renderer::Render(int width, int height, float time_sec,
                      const WeightedGraph& graph,
                      const AStarRunner& runner,
                      const UiState& ui) {
    EnsureSurface(width, height);

    cairo_set_source_surface(cr_, bg_surface_, 0.0, 0.0);
    cairo_paint(cr_);

    DrawAmbientParticles(cr_, time_sec);
    DrawDecorativePulseRing(cr_, time_sec, runner);
    DrawEdges(cr_, time_sec, graph, runner, ui);
    DrawNodes(cr_, time_sec, graph, runner, ui);
    DrawHud(cr_, time_sec, graph, runner, ui);

    cairo_surface_flush(surface_);
}

const uint8_t* Renderer::pixels() const {
    return surface_ ? cairo_image_surface_get_data(surface_) : nullptr;
}

int Renderer::stride() const {
    return surface_ ? cairo_image_surface_get_stride(surface_) : 0;
}

bool Renderer::SavePNG(const std::string& path) const {
    if (!surface_) return false;
    return cairo_surface_write_to_png(surface_, path.c_str()) == CAIRO_STATUS_SUCCESS;
}
