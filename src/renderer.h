#pragma once

#include "astar_runner.h"
#include "graph.h"

#include <cairo/cairo.h>
#include <cstdint>
#include <string>
#include <vector>

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool Contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct UiState {
    bool is_playing = true;
    int target_node_count = 64;
    GraphLayout layout = GraphLayout::RadialPulse;
    float steps_per_sec = 8.0f;
    bool show_weights = true;
    int hovered_node = -1;
    bool dragging_size_slider = false;
    bool dragging_speed_slider = false;
    bool click_assigns_goal_next = false;
};

struct UiHitboxes {
    RectF play_button;
    RectF step_button;
    RectF layout_buttons[3];
    RectF size_slider_track;
    RectF speed_slider_track;
    RectF weights_button;
    RectF new_graph_button;
    RectF reset_button;
};

class Renderer {
   public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Render(int width, int height, float time_sec,
                const WeightedGraph& graph,
                const AStarRunner& runner,
                const UiState& ui);

    const uint8_t* pixels() const;
    int stride() const;
    int width() const { return width_; }
    int height() const { return height_; }
    const UiHitboxes& hitboxes() const { return hitboxes_; }

    int HitTestNode(const WeightedGraph& graph, float px, float py) const;
    bool SavePNG(const std::string& path) const;

   private:
    struct Particle {
        float x;
        float y;
        float radius;
        float speed;
        float phase;
        float alpha;
    };

    void EnsureSurface(int width, int height);
    void BuildBackgroundCache(int width, int height);
    void InitParticles();

    void MapNodeToScreen(const GraphNode& node, float& sx, float& sy) const;
    void DrawAmbientParticles(cairo_t* cr, float time_sec) const;
    void DrawDecorativePulseRing(cairo_t* cr, float time_sec, const AStarRunner& runner) const;
    void DrawEdges(cairo_t* cr, float time_sec, const WeightedGraph& graph,
                   const AStarRunner& runner, const UiState& ui) const;
    void DrawNodes(cairo_t* cr, float time_sec, const WeightedGraph& graph,
                   const AStarRunner& runner, const UiState& ui) const;
    void DrawHud(cairo_t* cr, float time_sec, const WeightedGraph& graph,
                 const AStarRunner& runner, const UiState& ui);

    int width_ = 0;
    int height_ = 0;
    cairo_surface_t* surface_ = nullptr;
    cairo_t* cr_ = nullptr;

    cairo_surface_t* bg_surface_ = nullptr;
    int bg_w_ = 0;
    int bg_h_ = 0;

    std::vector<Particle> particles_;
    UiHitboxes hitboxes_;
};
