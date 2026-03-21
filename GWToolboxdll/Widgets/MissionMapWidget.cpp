#include "stdafx.h"

#include <GWCA/Context/GameplayContext.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/NPC.h>
#include <GWCA/GameEntities/Pathing.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Utilities/Hooker.h>

#include <ImGuiAddons.h>
#include <Modules/QuestModule.h>
#include <Widgets/Minimap/Minimap.h>
#include <Widgets/MissionMapWidget.h>
#include <Widgets/WorldMapWidget.h>

namespace {
    bool draw_all_terrain_lines = false;
    bool draw_all_minimap_lines = true;
    bool show_enemy_markers = true;
    bool show_exploration_overlay = true;
    bool show_vq_overlay = false; // master toggle for all VQ features on mission map

    // Enemy tracking for VQ assistance
    enum class EnemyState { Alive, Stale };
    struct TrackedEnemy {
        GW::GamePos pos;
        GW::Vec2f velocity = {0.0f, 0.0f}; // last known movement direction
        EnemyState state = EnemyState::Alive;
    };
    std::unordered_map<uint32_t, TrackedEnemy> tracked_enemies; // agent_id -> tracked enemy
    GW::Constants::MapID tracked_enemies_map_id = static_cast<GW::Constants::MapID>(0);
    GW::Constants::InstanceType tracked_enemies_instance_type = GW::Constants::InstanceType::Loading;

    constexpr float TWO_PI = 6.2831853f;
    constexpr float COMPASS_RANGE = 5000.0f;
    constexpr float STALE_CHECK_RANGE = COMPASS_RANGE * 0.9f;

    // Exploration tracking (fog of war)
    constexpr float EXPLORE_CELL_SIZE = 250.0f;
    bool* explored_cells = nullptr; // parallel to cached_walkable_grid, same dimensions
    GW::Constants::MapID explored_map_id = static_cast<GW::Constants::MapID>(0);
    GW::Constants::InstanceType explored_instance_type = GW::Constants::InstanceType::Loading;

    struct Polyline {
        std::vector<GW::GamePos> points;
        bool closed = false;
    };

    std::vector<Polyline> cached_border_polylines;

    bool* cached_walkable_grid = nullptr;
    int cached_walkable_grid_size = 0;

    int cached_grid_x0 = 0, cached_grid_y0 = 0, cached_grid_w = 0, cached_grid_h = 0;
    GW::Constants::MapID border_map_id = static_cast<GW::Constants::MapID>(0);
    constexpr float BORDER_CELL_SIZE = 300.0f;


    // Returns the flat index into cached_walkable_grid / explored_cells for
    // a given absolute grid coord, or -1 if out of bounds.
    int GetCellIndex(int gx, int gy)
    {
        const int lx = gx - cached_grid_x0;
        const int ly = gy - cached_grid_y0;
        if (lx < 0 || lx >= cached_grid_w || ly < 0 || ly >= cached_grid_h) return -1;
        return ly * cached_grid_w + lx;
    }

    bool IsCellExplored(int gx, int gy)
    {
        if (!show_exploration_overlay || !explored_cells) return false;
        const int idx = GetCellIndex(gx, gy);
        return idx >= 0 && explored_cells[idx];
    }

    void UpdateExploration(const GW::GamePos& player_pos)
    {
        const auto map_id = GW::Map::GetMapID();
        const auto instance_type = GW::Map::GetInstanceType();
        if (map_id != explored_map_id || instance_type != explored_instance_type) {
            delete[] explored_cells;
            explored_cells = nullptr;
            explored_map_id = map_id;
            explored_instance_type = instance_type;
        }

        if (instance_type != GW::Constants::InstanceType::Explorable) return;

        // Allocate parallel to walkable grid when first entering an explorable area
        if (!explored_cells && cached_walkable_grid_size > 0) {
            explored_cells = new bool[cached_walkable_grid_size](); // zero-init
        }
        if (!explored_cells) return;

        const int range_cells = static_cast<int>(COMPASS_RANGE / BORDER_CELL_SIZE) + 1;
        const int player_cx = static_cast<int>(floorf(player_pos.x / BORDER_CELL_SIZE));
        const int player_cy = static_cast<int>(floorf(player_pos.y / BORDER_CELL_SIZE));
        const float range_sq = COMPASS_RANGE * COMPASS_RANGE;

        for (int dy = -range_cells; dy <= range_cells; dy++) {
            for (int dx = -range_cells; dx <= range_cells; dx++) {
                const int gx = player_cx + dx;
                const int gy = player_cy + dy;
                const float cell_center_x = (gx + 0.5f) * BORDER_CELL_SIZE;
                const float cell_center_y = (gy + 0.5f) * BORDER_CELL_SIZE;
                const float ddx = cell_center_x - player_pos.x;
                const float ddy = cell_center_y - player_pos.y;
                if (ddx * ddx + ddy * ddy > range_sq) continue;
                const int idx = GetCellIndex(gx, gy);
                if (idx >= 0) explored_cells[idx] = true;
            }
        }
    }



    // Polyline smoothing utilities
    struct PosKey {
        int x, y;
        bool operator==(const PosKey& o) const { return x == o.x && y == o.y; }
    };
    struct PosKeyHash {
        size_t operator()(const PosKey& k) const {
            return std::hash<int>()(k.x) ^ (std::hash<int>()(k.y) << 16);
        }
    };

    PosKey ToPosKey(const GW::GamePos& p) {
        return {static_cast<int>(roundf(p.x)), static_cast<int>(roundf(p.y))};
    }

    // Chain disconnected segments into polylines by matching endpoints
    std::vector<Polyline> ChainSegments(const std::vector<std::pair<GW::GamePos, GW::GamePos>>& segments) {
        std::unordered_multimap<PosKey, std::pair<size_t, PosKey>, PosKeyHash> adj;
        for (size_t i = 0; i < segments.size(); i++) {
            auto k1 = ToPosKey(segments[i].first);
            auto k2 = ToPosKey(segments[i].second);
            adj.emplace(k1, std::make_pair(i, k2));
            adj.emplace(k2, std::make_pair(i, k1));
        }

        std::vector<bool> visited(segments.size(), false);
        std::vector<Polyline> result;

        for (size_t start = 0; start < segments.size(); start++) {
            if (visited[start]) continue;
            visited[start] = true;

            Polyline poly;
            poly.points.push_back(segments[start].first);
            poly.points.push_back(segments[start].second);

            for (auto current = ToPosKey(segments[start].second);;) {
                bool found = false;
                auto [it, end] = adj.equal_range(current);
                for (; it != end; ++it) {
                    if (visited[it->second.first]) continue;
                    visited[it->second.first] = true;
                    current = it->second.second;
                    const auto& seg = segments[it->second.first];
                    poly.points.push_back(ToPosKey(seg.first) == current ? seg.first : seg.second);
                    found = true;
                    break;
                }
                if (!found) break;
            }

            if (ToPosKey(poly.points.front()) == ToPosKey(poly.points.back())) {
                poly.closed = true;
                poly.points.pop_back();
            }
            result.push_back(std::move(poly));
        }
        return result;
    }

    // Chaikin corner-cutting subdivision
    void SmoothPolyline(Polyline& poly, int iterations) {
        for (int iter = 0; iter < iterations; iter++) {
            const int n = static_cast<int>(poly.points.size());
            if (n < 2) return;
            std::vector<GW::GamePos> smoothed;

            const int count = poly.closed ? n : n - 1;
            if (!poly.closed) smoothed.push_back(poly.points[0]);
            for (int i = 0; i < count; i++) {
                const auto& p0 = poly.points[i];
                const auto& p1 = poly.points[(i + 1) % n];
                smoothed.push_back({0.75f * p0.x + 0.25f * p1.x, 0.75f * p0.y + 0.25f * p1.y, 0});
                smoothed.push_back({0.25f * p0.x + 0.75f * p1.x, 0.25f * p0.y + 0.75f * p1.y, 0});
            }
            if (!poly.closed) smoothed.push_back(poly.points.back());
            poly.points = std::move(smoothed);
        }
    }

    std::vector<Polyline> cached_frontier_polylines;
    size_t fog_cache_explored_count = SIZE_MAX;

    // Game-space vertex for GPU-projected geometry (D3DFVF_XYZ | D3DFVF_DIFFUSE)
    struct GameVertex { float x, y, z; DWORD color; };
    std::vector<GameVertex> cached_inaccessible_verts;
    std::vector<GameVertex> cached_unexplored_verts;

    void PushGameQuad(std::vector<GameVertex>& out, float x0, float y0, float x1, float y1, DWORD color) {
        out.push_back({x0, y0, 0, color});
        out.push_back({x1, y0, 0, color});
        out.push_back({x1, y1, 0, color});
        out.push_back({x0, y0, 0, color});
        out.push_back({x1, y1, 0, color});
        out.push_back({x0, y1, 0, color});
    }

    bool IsGridCellWalkable(int gx, int gy)
    {
        const int idx = GetCellIndex(gx, gy);
        return idx >= 0 && cached_walkable_grid[idx];
    }

    size_t CountExploredCells() {
        size_t count = 0;
        if (explored_cells) {
            for (int i = 0; i < cached_walkable_grid_size; i++) {
                if (explored_cells[i]) count++;
            }
        }
        return count;
    }

    void RebuildFogCache() {
        fog_cache_explored_count = CountExploredCells();
        cached_unexplored_verts.clear();
        std::vector<std::pair<GW::GamePos, GW::GamePos>> frontier_segments;

        constexpr DWORD FOG_UNEXPLORED = D3DCOLOR_ARGB(140, 0, 0, 0);

        for (int gy = 0; gy < cached_grid_h; gy++) {
            for (int gx = 0; gx < cached_grid_w; gx++) {
                const int abs_gx = cached_grid_x0 + gx;
                const int abs_gy = cached_grid_y0 + gy;
                const int idx = GetCellIndex(abs_gx, abs_gy);
                if (idx < 0 || !cached_walkable_grid[idx]) continue;
                if (IsCellExplored(abs_gx, abs_gy)) continue;

                const float x0 = abs_gx * BORDER_CELL_SIZE;
                const float y0 = abs_gy * BORDER_CELL_SIZE;
                PushGameQuad(cached_unexplored_verts, x0, y0, x0 + BORDER_CELL_SIZE, y0 + BORDER_CELL_SIZE, FOG_UNEXPLORED);

                const float x1 = x0 + BORDER_CELL_SIZE;
                const float y1 = y0 + BORDER_CELL_SIZE;
                auto check_neighbor = [&](int nx, int ny, const GW::GamePos& ep1, const GW::GamePos& ep2) {
                    if (!IsGridCellWalkable(cached_grid_x0 + nx, cached_grid_y0 + ny)) return;
                    if (!IsCellExplored(cached_grid_x0 + nx, cached_grid_y0 + ny)) return;
                    frontier_segments.push_back({ep1, ep2});
                };
                check_neighbor(gx, gy - 1, {x0, y0, 0}, {x1, y0, 0});
                check_neighbor(gx, gy + 1, {x0, y1, 0}, {x1, y1, 0});
                check_neighbor(gx - 1, gy, {x0, y0, 0}, {x0, y1, 0});
                check_neighbor(gx + 1, gy, {x1, y0, 0}, {x1, y1, 0});
            }
        }

        cached_frontier_polylines = ChainSegments(frontier_segments);
        for (auto& poly : cached_frontier_polylines) {
            SmoothPolyline(poly, 2);
        }
    }

    bool IsCellWalkableInTrapezoid(int gx, int gy, const GW::PathingTrapezoid& trap)
    {
        const float cx0 = gx * BORDER_CELL_SIZE;
        const float cy0 = gy * BORDER_CELL_SIZE;
        const float cx1 = cx0 + BORDER_CELL_SIZE;
        const float cy1 = cy0 + BORDER_CELL_SIZE;

        if (cy1 <= trap.YB || cy0 >= trap.YT) return false;
        const float y_lo = std::max(cy0, trap.YB);
        const float y_hi = std::min(cy1, trap.YT);
        const float height = trap.YT - trap.YB;
        if (height < 0.001f) return cx1 > std::min(trap.XBL, trap.XTL) && cx0 < std::max(trap.XBR, trap.XTR);

        const float t_lo = (y_lo - trap.YB) / height;
        const float t_hi = (y_hi - trap.YB) / height;
        const float left = std::min(trap.XBL + t_lo * (trap.XTL - trap.XBL), trap.XBL + t_hi * (trap.XTL - trap.XBL));
        const float right = std::max(trap.XBR + t_lo * (trap.XTR - trap.XBR), trap.XBR + t_hi * (trap.XTR - trap.XBR));
        return cx1 > left && cx0 < right;
    }

    void RebuildMapBorder()
    {
        cached_border_polylines.clear();
        cached_inaccessible_verts.clear();
        fog_cache_explored_count = SIZE_MAX;
        delete[] cached_walkable_grid;
        cached_walkable_grid = nullptr;
        cached_walkable_grid_size = 0;
        delete[] explored_cells;
        explored_cells = nullptr;
        explored_map_id = static_cast<GW::Constants::MapID>(0);

        const auto pathing_map = GW::Map::GetPathingMap();
        if (!pathing_map) return;

        std::vector<const GW::PathingTrapezoid*> traps;
        float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
        for (size_t p = 0; p < pathing_map->size(); p++) {
            const auto& plane = pathing_map->at(p);
            for (uint32_t t = 0; t < plane.trapezoid_count; t++) {
                const auto& trap = plane.trapezoids[t];
                traps.push_back(&trap);
                min_x = std::min({min_x, trap.XTL, trap.XTR, trap.XBL, trap.XBR});
                max_x = std::max({max_x, trap.XTL, trap.XTR, trap.XBL, trap.XBR});
                min_y = std::min(min_y, trap.YB);
                max_y = std::max(max_y, trap.YT);
            }
        }
        if (traps.empty()) return;

        cached_grid_x0 = static_cast<int>(floorf(min_x / BORDER_CELL_SIZE)) - 1;
        cached_grid_y0 = static_cast<int>(floorf(min_y / BORDER_CELL_SIZE)) - 1;
        cached_grid_w = static_cast<int>(ceilf(max_x / BORDER_CELL_SIZE)) + 1 - cached_grid_x0;
        cached_grid_h = static_cast<int>(ceilf(max_y / BORDER_CELL_SIZE)) + 1 - cached_grid_y0;

        cached_walkable_grid_size = cached_grid_w * cached_grid_h;
        cached_walkable_grid = new bool[cached_walkable_grid_size]();

        for (const auto* trap : traps) {
            const int ty0 = std::max(0, static_cast<int>(floorf(trap->YB / BORDER_CELL_SIZE)) - cached_grid_y0);
            const int ty1 = std::min(cached_grid_h - 1, static_cast<int>(ceilf(trap->YT / BORDER_CELL_SIZE)) - cached_grid_y0);
            const int tx0 = std::max(0, static_cast<int>(floorf(std::min({trap->XTL, trap->XBL}) / BORDER_CELL_SIZE)) - cached_grid_x0);
            const int tx1 = std::min(cached_grid_w - 1, static_cast<int>(ceilf(std::max({trap->XTR, trap->XBR}) / BORDER_CELL_SIZE)) - cached_grid_x0);
            for (int cy = ty0; cy <= ty1; cy++) {
                for (int cx = tx0; cx <= tx1; cx++) {
                    const int abs_gx = cached_grid_x0 + cx;
                    const int abs_gy = cached_grid_y0 + cy;
                    const int idx = GetCellIndex(abs_gx, abs_gy);
                    if (idx < 0 || cached_walkable_grid[idx]) continue;
                    if (IsCellWalkableInTrapezoid(abs_gx, abs_gy, *trap)) 
                        cached_walkable_grid[idx] = true;
                }
            }
        }

        // Build inaccessible cell vertex buffer in game coordinates
        {
            constexpr DWORD INACCESSIBLE_COLOR = D3DCOLOR_ARGB(190, 0, 0, 0);

            // 4 large strips covering everything outside the grid (scissor rect clips to viewport)
            constexpr float INF = 1e6f;
            const float gx0 = cached_grid_x0 * BORDER_CELL_SIZE;
            const float gy0 = cached_grid_y0 * BORDER_CELL_SIZE;
            const float gx1 = (cached_grid_x0 + cached_grid_w) * BORDER_CELL_SIZE;
            const float gy1 = (cached_grid_y0 + cached_grid_h) * BORDER_CELL_SIZE;
            PushGameQuad(cached_inaccessible_verts, -INF, -INF, INF, gy0, INACCESSIBLE_COLOR);
            PushGameQuad(cached_inaccessible_verts, -INF, gy1, INF, INF, INACCESSIBLE_COLOR);
            PushGameQuad(cached_inaccessible_verts, -INF, gy0, gx0, gy1, INACCESSIBLE_COLOR);
            PushGameQuad(cached_inaccessible_verts, gx1, gy0, INF, gy1, INACCESSIBLE_COLOR);

            // Non-walkable cells within the grid
            for (int cy = 0; cy < cached_grid_h; cy++) {
                for (int cx = 0; cx < cached_grid_w; cx++) {
                    const int idx = GetCellIndex(cached_grid_x0 + cx, cached_grid_y0 + cy);
                    if (idx < 0 || cached_walkable_grid[idx]) continue;
                    const float x0 = (cached_grid_x0 + cx) * BORDER_CELL_SIZE;
                    const float y0 = (cached_grid_y0 + cy) * BORDER_CELL_SIZE;
                    PushGameQuad(cached_inaccessible_verts, x0, y0, x0 + BORDER_CELL_SIZE, y0 + BORDER_CELL_SIZE, INACCESSIBLE_COLOR);
                }
            }
        }

        std::vector<std::pair<GW::GamePos, GW::GamePos>> raw_segments;
        for (int cy = 0; cy < cached_grid_h; cy++) {
            for (int cx = 0; cx < cached_grid_w; cx++) {
                const int idx = GetCellIndex(cached_grid_x0 + cx, cached_grid_y0 + cy);
                if (idx < 0 || !cached_walkable_grid[idx]) continue;

                const float x0 = (cached_grid_x0 + cx) * BORDER_CELL_SIZE;
                const float y0 = (cached_grid_y0 + cy) * BORDER_CELL_SIZE;
                const float x1 = x0 + BORDER_CELL_SIZE;
                const float y1 = y0 + BORDER_CELL_SIZE;

                if (!IsGridCellWalkable(cached_grid_x0 + cx, cached_grid_y0 + cy - 1)) raw_segments.push_back({{x0, y0, 0}, {x1, y0, 0}});
                if (!IsGridCellWalkable(cached_grid_x0 + cx, cached_grid_y0 + cy + 1)) raw_segments.push_back({{x0, y1, 0}, {x1, y1, 0}});
                if (!IsGridCellWalkable(cached_grid_x0 + cx - 1, cached_grid_y0 + cy)) raw_segments.push_back({{x0, y0, 0}, {x0, y1, 0}});
                if (!IsGridCellWalkable(cached_grid_x0 + cx + 1, cached_grid_y0 + cy)) raw_segments.push_back({{x1, y0, 0}, {x1, y1, 0}});
            }
        }

        cached_border_polylines = ChainSegments(raw_segments);
        for (auto& poly : cached_border_polylines) {
            SmoothPolyline(poly, 2);
        }
    }

    void ClearQuestMarker()
    {
        GW::GameThread::Enqueue([] {
            QuestModule::SetCustomQuestMarker({0, 0});
        });
    }

    bool nav_active = false;
    GW::GamePos nav_target_pos;
    GW::GamePos nav_marker_pos;
    bool nav_marker_set = false;
    bool nav_marker_hidden = false;

    constexpr float MARKER_UPDATE_DIST = 500.0f;

    void StopNavigating()
    {
        nav_active = false;
        nav_target_pos = {};
        nav_marker_pos = {};
        nav_marker_set = false;
        nav_marker_hidden = false;
        ClearQuestMarker();
    }

    void SetNavTarget(const GW::GamePos& target)
    {
        nav_active = true;
        nav_target_pos = target;

        const auto player = GW::Agents::GetControlledCharacter();
        if (player) {
            const float dx = target.x - player->pos.x;
            const float dy = target.y - player->pos.y;
            constexpr float HIDE_RANGE = COMPASS_RANGE * 0.5f;
            const bool in_compass = dx * dx + dy * dy < HIDE_RANGE * HIDE_RANGE;

            if (in_compass && !nav_marker_hidden) {
                nav_marker_hidden = true;
                ClearQuestMarker();
                return;
            }
            if (in_compass) return;

            if (nav_marker_hidden) {
                nav_marker_hidden = false;
                nav_marker_set = false;
            }
        }

        const float dx = target.x - nav_marker_pos.x;
        const float dy = target.y - nav_marker_pos.y;
        if (nav_marker_set && dx * dx + dy * dy < MARKER_UPDATE_DIST * MARKER_UPDATE_DIST) return;

        nav_marker_pos = target;
        nav_marker_set = true;
        GW::Vec2f world_pos;
        if (WorldMapWidget::GamePosToWorldMap(target, world_pos)) {
            GW::GameThread::Enqueue([world_pos] {
                QuestModule::SetCustomQuestMarker(world_pos, true);
            });
        }
    }

    void NavigateToClosestEnemy()
    {
        const auto player = GW::Agents::GetControlledCharacter();
        if (!player) return;

        const GW::GamePos player_pos = player->pos;
        float best_dist_sq = FLT_MAX;
        GW::GamePos best_pos = {};
        bool found = false;

        for (const auto& [agent_id, enemy] : tracked_enemies) {
            const float dx = enemy.pos.x - player_pos.x;
            const float dy = enemy.pos.y - player_pos.y;
            const float dist_sq = dx * dx + dy * dy;
            if (dist_sq < best_dist_sq) {
                best_dist_sq = dist_sq;
                best_pos = enemy.pos;
                found = true;
            }
        }

        if (!found) {
            if (!nav_marker_hidden) {
                nav_marker_hidden = true;
                ClearQuestMarker();
            }
            return;
        }

        SetNavTarget(best_pos);
    }

    void UpdateEnemyTracking()
    {
        const auto map_id = GW::Map::GetMapID();
        const auto instance_type = GW::Map::GetInstanceType();
        if (map_id != tracked_enemies_map_id || instance_type != tracked_enemies_instance_type) {
            tracked_enemies.clear();
            tracked_enemies_map_id = map_id;
            tracked_enemies_instance_type = instance_type;
            if (nav_active) StopNavigating();
        }

        if (instance_type != GW::Constants::InstanceType::Explorable) return;

        const auto player = GW::Agents::GetControlledCharacter();
        if (!player) return;

        const auto agents = GW::Agents::GetAgentArray();
        if (!agents) return;

        std::unordered_set<uint32_t> visible_enemy_ids;

        for (const auto agent : *agents) {
            if (!agent) continue;
            const auto living = agent->GetAsAgentLiving();
            if (!living) continue;
            if (living->allegiance != GW::Constants::Allegiance::Enemy) continue;
            const auto npc = GW::Agents::GetNPCByID(living->player_number);
            if (npc && (npc->IsSpirit() || npc->IsMinion())) continue;

            visible_enemy_ids.insert(agent->agent_id);

            if (!living->GetIsAlive()) {
                tracked_enemies.erase(agent->agent_id);
                continue;
            }

            auto& tracked = tracked_enemies[agent->agent_id];
            tracked.pos = living->pos;
            tracked.velocity = living->velocity;
            tracked.state = EnemyState::Alive;
        }

        const auto* player_living = player->GetAsAgentLiving();
        static bool was_dead = false;
        const bool is_dead = !player_living || !player_living->GetIsAlive();
        if (is_dead) {
            was_dead = true;
            return;
        }
        if (was_dead) {
            was_dead = false;
            return;
        }

        const GW::GamePos player_pos = player->pos;
        for (auto& [agent_id, enemy] : tracked_enemies) {
            if (visible_enemy_ids.contains(agent_id)) continue;
            if (enemy.state != EnemyState::Alive) continue;

            const float dx = enemy.pos.x - player_pos.x;
            const float dy = enemy.pos.y - player_pos.y;
            if (dx * dx + dy * dy < STALE_CHECK_RANGE * STALE_CHECK_RANGE) {
                enemy.state = EnemyState::Stale;
            }
        }

        if (nav_active) NavigateToClosestEnemy();
    }

    GW::Vec2f mission_map_top_left;
    GW::Vec2f mission_map_bottom_right;
    GW::Vec2f mission_map_scale;
    float mission_map_zoom;

    GW::Vec2f mission_map_center_pos;
    GW::Vec2f mission_map_last_click_location;
    GW::Vec2f current_pan_offset;

    GW::Vec2f mission_map_screen_pos;
    GW::Vec2f world_map_click_pos;

    MinimapRenderContext mission_map_render_context;

    bool right_clicking = false;

    GW::UI::Frame* mission_map_frame = nullptr;

    bool IsScreenPosOnMissionMap(const GW::Vec2f& screen_pos)
    {
        if (!(mission_map_frame && mission_map_frame->IsVisible())) return false;
        return (screen_pos.x >= mission_map_top_left.x && screen_pos.x <= mission_map_bottom_right.x && screen_pos.y >= mission_map_top_left.y && screen_pos.y <= mission_map_bottom_right.y);
    }

    GW::Vec2f GetMissionMapScreenCenterPos()
    {
        return mission_map_top_left + (mission_map_bottom_right - mission_map_top_left) / 2;
    }

    bool WorldMapCoordsToMissionMapScreenPos(const GW::Vec2f& world_map_position, GW::Vec2f& screen_coords)
    {
        const auto offset = (world_map_position - current_pan_offset);
        const auto scaled_offset = GW::Vec2f(offset.x * mission_map_scale.x, offset.y * mission_map_scale.y);
        screen_coords = (scaled_offset * mission_map_zoom + mission_map_screen_pos);
        return true;
    }

    GW::Vec2f ScreenPosToMissionMapCoords(const GW::Vec2f screen_position)
    {
        GW::Vec2f unscaled_offset = (screen_position - mission_map_screen_pos) / mission_map_zoom;
        GW::Vec2f offset(unscaled_offset.x / mission_map_scale.x, unscaled_offset.y / mission_map_scale.y);
        return offset + current_pan_offset;
    }

    // Used for screen-space geometry (lines, enemy markers, viewport culling)
    bool GamePosToMissionMapScreenPos(const GW::GamePos& game_map_position, GW::Vec2f& screen_coords)
    {
        GW::Vec2f world_map_pos;
        return WorldMapWidget::GamePosToWorldMap(game_map_position, world_map_pos) && WorldMapCoordsToMissionMapScreenPos(world_map_pos, screen_coords);
    }

    void Draw(IDirect3DDevice9*);

    std::vector<GW::UI::UIMessage> messages_hit;
    GW::UI::UIInteractionCallback OnMissionMap_UICallback_Func = nullptr, OnMissionMap_UICallback_Ret = nullptr;

    void OnMissionMap_UICallback(GW::UI::InteractionMessage* message, void* wparam, void* lparam)
    {
        GW::Hook::EnterHook();
        if (message->message_id == GW::UI::UIMessage::kDestroyFrame) mission_map_frame = nullptr;
        OnMissionMap_UICallback_Ret(message, wparam, lparam);
        if (message->message_id == GW::UI::UIMessage::kInitFrame) mission_map_frame = GW::UI::GetFrameById(message->frame_id);
        GW::Hook::LeaveHook();
    }

    bool HookMissionMapFrame()
    {
        if (OnMissionMap_UICallback_Func) return true;

        const auto mission_map_context = GW::Map::GetMissionMapContext();
        mission_map_frame = mission_map_context ? GW::UI::GetFrameById(mission_map_context->frame_id) : nullptr;
        if (!(mission_map_frame && mission_map_frame->frame_callbacks[0].callback)) return false;
        OnMissionMap_UICallback_Func = mission_map_frame->frame_callbacks[0].callback;
        GW::Hook::CreateHook((void**)&OnMissionMap_UICallback_Func, OnMissionMap_UICallback, (void**)&OnMissionMap_UICallback_Ret);
        GW::Hook::EnableHooks(OnMissionMap_UICallback_Func);
        return true;
    }

    bool InitializeMissionMapParameters()
    {
        const auto gameplay_context = GW::GetGameplayContext();
        const auto mission_map_context = GW::Map::GetMissionMapContext();
        if (!(gameplay_context && mission_map_frame && mission_map_frame->IsVisible())) return false;

        const auto root = GW::UI::GetRootFrame();
        mission_map_top_left = mission_map_frame->position.GetContentTopLeft(root);
        mission_map_bottom_right = mission_map_frame->position.GetContentBottomRight(root);
        mission_map_scale = mission_map_frame->position.GetViewportScale(root);
        mission_map_zoom = gameplay_context->mission_map_zoom;
        mission_map_center_pos = mission_map_context->player_mission_map_pos;
        mission_map_last_click_location = mission_map_context->last_mouse_location;
        current_pan_offset = mission_map_context->h003c->mission_map_pan_offset;
        mission_map_screen_pos = GetMissionMapScreenCenterPos();
        return true;
    }

    bool MissionMapContextMenu(void*)
    {
        if (!(mission_map_frame && mission_map_frame->IsVisible())) return false;
        const auto c = ImGui::GetCurrentContext();
        auto viewport_offset = c->CurrentViewport->Pos;
        viewport_offset.x *= -1;
        viewport_offset.y *= -1;

        ImGui::Text("%.2f, %.2f", world_map_click_pos.x, world_map_click_pos.y);
#ifdef _DEBUG
        GW::GamePos game_pos;
        if (WorldMapWidget::WorldMapToGamePos(world_map_click_pos, game_pos)) {
            ImGui::Text("%.2f, %.2f", game_pos.x, game_pos.y);
        }
#endif
        if (ImGui::Button("Place Marker")) {
            nav_active = false;
            nav_target_pos = {};
            nav_marker_pos = {};
            GW::GameThread::Enqueue([] {
                QuestModule::SetCustomQuestMarker(world_map_click_pos, true);
            });
            return false;
        }
        if (QuestModule::GetCustomQuestMarker() && !nav_active) {
            if (ImGui::Button("Remove Marker")) {
                ClearQuestMarker();
                return false;
            }
        }
        if (show_enemy_markers) {
            if (nav_active) {
                if (ImGui::Button("Stop navigating")) {
                    StopNavigating();
                    return false;
                }
            }
            else {
                if (ImGui::Button("Navigate to closest enemy")) {
                    nav_active = true;
                    NavigateToClosestEnemy();
                    return false;
                }
            }
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // Matrix helpers
    // ---------------------------------------------------------------------------

    // Screen-space ortho: maps pixel coords [0,w]x[0,h] -> NDC [-1,1]x[1,-1]
    D3DMATRIX MakeOrthoProjection(float w, float h)
    {
        // clang-format off
        D3DMATRIX m = {{
             2.f/w,   0.f,  0.f, 0.f,
              0.f,  -2.f/h, 0.f, 0.f,
              0.f,   0.f,  1.f, 0.f,
             -1.f,   1.f,  0.f, 1.f
        }};
        // clang-format on
        return m;
    }

    const D3DMATRIX IDENTITY_MATRIX = {{1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f}};

    struct GameToScreenBasis {
        float ox, oy;
        float ax, ay;
        float bx, by;
        bool valid = false;

        void Rebuild()
        {
            valid = false;

            // Sample at three game positions that are guaranteed to be close together.
            // We use absolute game coords 0,0 for the anchor but measure the basis
            // vectors using a step size large enough to avoid float precision issues
            // at the world->screen transform level, then normalise back to per-unit.
            constexpr float STEP = 1000.f; // large enough for precision, small enough to stay in map

            GW::Vec2f s00, s10, s01;
            if (!GamePosToMissionMapScreenPos({0.f, 0.f, 0}, s00) || !GamePosToMissionMapScreenPos({STEP, 0.f, 0}, s10) || !GamePosToMissionMapScreenPos({0.f, STEP, 0}, s01)) return;

            ox = s00.x;
            oy = s00.y;
            // Divide by step to get per-unit basis vectors
            ax = (s10.x - s00.x) / STEP;
            ay = (s10.y - s00.y) / STEP;
            bx = (s01.x - s00.x) / STEP;
            by = (s01.y - s00.y) / STEP;
            valid = true;
        }

        void Project(float gx, float gy, float& sx, float& sy) const
        {
            sx = ox + gx * ax + gy * bx;
            sy = oy + gx * ay + gy * by;
        }
    } g2s;

    // Build a world transform that maps raw game coords (x, y) directly to screen
    // pixels, so VQ geometry can be built in game space with no per-vertex projection.
    //
    // The full chain is:
    //   game -> world_map  (WorldMapWidget::GamePosToWorldMap — linear scale+offset)
    //   world_map -> screen (pan, scale, zoom as in WorldMapCoordsToMissionMapScreenPos)
    //
    // We sample three anchor points to recover the affine coefficients, then pack
    // them into a D3D world matrix. The ortho projection handles the final
    // screen->NDC step as before.
    //
    // Returns false if the world-map transform is unavailable this frame.
    bool BuildGameToScreenWorldMatrix(D3DMATRIX& out)
    {
        // Sample the transform at the origin and one unit along each axis
        const GW::GamePos g00 = {0.f, 0.f, 0};
        const GW::GamePos g10 = {1.f, 0.f, 0};
        const GW::GamePos g01 = {0.f, 1.f, 0};

        GW::Vec2f s00, s10, s01;
        if (!GamePosToMissionMapScreenPos(g00, s00) || !GamePosToMissionMapScreenPos(g10, s10) || !GamePosToMissionMapScreenPos(g01, s01)) return false;

        // Basis vectors: screen delta per 1 game unit along each axis
        const float ax = s10.x - s00.x; // d(screen.x)/d(game.x)
        const float ay = s10.y - s00.y; // d(screen.y)/d(game.x)
        const float bx = s01.x - s00.x; // d(screen.x)/d(game.y)
        const float by = s01.y - s00.y; // d(screen.y)/d(game.y)

        // Row-major D3D world matrix (applied as: screen_pos = game_pos * M):
        //   row0 = (ax, ay, 0, 0)    <- game.x contribution
        //   row1 = (bx, by, 0, 0)    <- game.y contribution
        //   row2 = (0,  0,  1, 0)    <- z pass-through (unused for 2D overlay)
        //   row3 = (tx, ty, 0, 1)    <- translation (screen pos of game origin)
        // clang-format off
        out = {{
            ax,     ay,    0.f, 0.f,
            bx,     by,    0.f, 0.f,
            0.f,    0.f,   1.f, 0.f,
            s00.x,  s00.y, 0.f, 1.f
        }};
        // clang-format on
        return true;
    }
    void Draw(IDirect3DDevice9* dx_device)
    {
        if (!HookMissionMapFrame()) return;
        if (!InitializeMissionMapParameters()) return;
        g2s.Rebuild();

#if 0
        mission_map_render_context.top_left = mission_map_top_left;
        mission_map_render_context.bottom_right = mission_map_bottom_right;
        mission_map_render_context.base_scale = mission_map_scale.x * 104.f * mission_map_zoom;
        mission_map_render_context.zoom_scale = 1.f;
        mission_map_render_context.foreground_color = D3DCOLOR_ARGB(100, 0xe0, 0xe0, 0xe0);
        GW::Vec2f player_screen_pos;
        WorldMapCoordsToMissionMapScreenPos(mission_map_center_pos, player_screen_pos);
        mission_map_render_context.anchor_point = {player_screen_pos.x, player_screen_pos.y};
        Minimap::Render(dx_device, mission_map_render_context);
#endif

        struct Vertex {
            float x, y, z, w;
            DWORD color;
        };

        // -----------------------------------------------------------------------
        // Frame-scoped vertex arena — single static allocation, no STL overhead.
        // Sections are carved out in order: fog | border | lines | enemies
        // -----------------------------------------------------------------------
        constexpr int MAX_VERTICES = 1 << 20; // 1M verts (~20MB) — adjust if needed
        static Vertex vertex_arena[MAX_VERTICES];
        int arena_pos = 0;

        // Returns a pointer to `count` contiguous verts in the arena, or nullptr if full.
        const auto arena_alloc = [&](int count) -> Vertex* {
            if (arena_pos + count > MAX_VERTICES) return nullptr;
            Vertex* ptr = vertex_arena + arena_pos;
            arena_pos += count;
            return ptr;
        };

        // Section markers — each section is a contiguous slice of vertex_arena.
        int fog_start = 0, fog_count = 0;
        int border_start = 0, border_count = 0;
        int line_start = 0, line_count = 0;
        int enemy_start = 0, enemy_count = 0;

        // -----------------------------------------------------------------------
        // push_thick_line — writes 18 verts directly into the arena.
        // out_start: base index of the current section; out_count: running count.
        // -----------------------------------------------------------------------
        const auto push_thick_line = [&](int& out_count, const GW::Vec2f& s1, const GW::Vec2f& s2, float half_thickness, DWORD color) {
            const float dx = s2.x - s1.x, dy = s2.y - s1.y;
            const float len_sq = dx * dx + dy * dy;
            if (len_sq < 0.000001f) return;
            const float inv_len = 1.0f / sqrtf(len_sq);
            const float nx = -dy * inv_len, ny = dx * inv_len;
            constexpr float FRINGE = 1.0f;
            const float ix = nx * half_thickness, iy = ny * half_thickness;
            const float ox = nx * (half_thickness + FRINGE), oy = ny * (half_thickness + FRINGE);
            const DWORD t = color & 0x00FFFFFF;

            Vertex* v = arena_alloc(18);
            if (!v) return;

            const float s1px = s1.x + ix, s1py = s1.y + iy;
            const float s1mx = s1.x - ix, s1my = s1.y - iy;
            const float s1opx = s1.x + ox, s1opy = s1.y + oy;
            const float s1omx = s1.x - ox, s1omy = s1.y - oy;
            const float s2px = s2.x + ix, s2py = s2.y + iy;
            const float s2mx = s2.x - ix, s2my = s2.y - iy;
            const float s2opx = s2.x + ox, s2opy = s2.y + oy;
            const float s2omx = s2.x - ox, s2omy = s2.y - oy;

            v[0] = {s1opx, s1opy, 0.f, 1.f, t};
            v[1] = {s1px, s1py, 0.f, 1.f, color};
            v[2] = {s2px, s2py, 0.f, 1.f, color};
            v[3] = {s1opx, s1opy, 0.f, 1.f, t};
            v[4] = {s2px, s2py, 0.f, 1.f, color};
            v[5] = {s2opx, s2opy, 0.f, 1.f, t};
            v[6] = {s1px, s1py, 0.f, 1.f, color};
            v[7] = {s1mx, s1my, 0.f, 1.f, color};
            v[8] = {s2mx, s2my, 0.f, 1.f, color};
            v[9] = {s1px, s1py, 0.f, 1.f, color};
            v[10] = {s2mx, s2my, 0.f, 1.f, color};
            v[11] = {s2px, s2py, 0.f, 1.f, color};
            v[12] = {s1mx, s1my, 0.f, 1.f, color};
            v[13] = {s1omx, s1omy, 0.f, 1.f, t};
            v[14] = {s2omx, s2omy, 0.f, 1.f, t};
            v[15] = {s1mx, s1my, 0.f, 1.f, color};
            v[16] = {s2omx, s2omy, 0.f, 1.f, t};
            v[17] = {s2mx, s2my, 0.f, 1.f, color};

            out_count += 18;
        };

        // push_thick_line_game — projects game coords via g2s then writes 18 verts.
        const auto push_thick_line_game = [&](int& out_count, float x1, float y1, float x2, float y2, float half_thickness, DWORD color) {
            if (!g2s.valid) return;
            float sx1, sy1, sx2, sy2;
            g2s.Project(x1, y1, sx1, sy1);
            g2s.Project(x2, y2, sx2, sy2);
            push_thick_line(out_count, {sx1, sy1}, {sx2, sy2}, half_thickness, color);
        };

        // push_game_quad — projects 4 game-coord corners, writes 6 verts (2 tris).
        const auto push_game_quad = [&](int& out_count, float x0, float y0, float x1, float y1, DWORD color) {
            if (!g2s.valid) return;
            Vertex* v = arena_alloc(6);
            if (!v) return;
            float sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3;
            g2s.Project(x0, y0, sx0, sy0); // TL
            g2s.Project(x1, y0, sx1, sy1); // TR
            g2s.Project(x1, y1, sx2, sy2); // BR
            g2s.Project(x0, y1, sx3, sy3); // BL
            v[0] = {sx0, sy0, 0.f, 1.f, color};
            v[1] = {sx1, sy1, 0.f, 1.f, color};
            v[2] = {sx2, sy2, 0.f, 1.f, color};
            v[3] = {sx0, sy0, 0.f, 1.f, color};
            v[4] = {sx2, sy2, 0.f, 1.f, color};
            v[5] = {sx3, sy3, 0.f, 1.f, color};
            out_count += 6;
        };

        // -----------------------------------------------------------------------
        // Custom renderer lines (screen-space, projected per-vertex)
        // -----------------------------------------------------------------------
        const auto& lines = Minimap::Instance().custom_renderer.GetLines();
        const auto map_id = GW::Map::GetMapID();

        line_start = arena_pos;
        constexpr float LINE_HALF_THICKNESS = 1.5f;

        for (const auto& line : lines) {
            if (!line->visible) continue;
            if (!line->draw_on_mission_map && !(draw_all_minimap_lines && line->draw_on_minimap) && !(draw_all_terrain_lines && line->draw_on_terrain)) continue;
            if (line->map != map_id) continue;

            GW::Vec2f projected_p1, projected_p2;
            if (!GamePosToMissionMapScreenPos(line->p1, projected_p1)) continue;
            if (!GamePosToMissionMapScreenPos(line->p2, projected_p2)) continue;

            push_thick_line(line_count, projected_p1, projected_p2, LINE_HALF_THICKNESS, static_cast<DWORD>(line->color));
        }

        // -----------------------------------------------------------------------
        // VQ overlay — fog, border, compass circle, frontier edges
        // -----------------------------------------------------------------------
        const bool in_explorable = GW::Map::GetInstanceType() == GW::Constants::InstanceType::Explorable;

        if (show_vq_overlay && in_explorable) {
            if (map_id != border_map_id) {
                border_map_id = map_id;
                RebuildMapBorder();
            }
            if (CountExploredCells() != fog_cache_explored_count) {
                RebuildFogCache();
            }

            // Compute visible game-coord range for culling
            GW::GamePos viewport_tl_game, viewport_br_game;
            const GW::Vec2f wm_tl = ScreenPosToMissionMapCoords(mission_map_top_left);
            const GW::Vec2f wm_br = ScreenPosToMissionMapCoords(mission_map_bottom_right);

            if (!WorldMapWidget::WorldMapToGamePos(wm_tl, viewport_tl_game) || !WorldMapWidget::WorldMapToGamePos(wm_br, viewport_br_game)) goto draw; // transform unavailable this frame, skip VQ geometry

            {
                const float vis_min_x = std::min(viewport_tl_game.x, viewport_br_game.x);
                const float vis_max_x = std::max(viewport_tl_game.x, viewport_br_game.x);
                const float vis_min_y = std::min(viewport_tl_game.y, viewport_br_game.y);
                const float vis_max_y = std::max(viewport_tl_game.y, viewport_br_game.y);

                constexpr float MAX_VALID_COORD = 1e6f;
                if (fabsf(vis_min_x) > MAX_VALID_COORD || fabsf(vis_min_y) > MAX_VALID_COORD || fabsf(vis_max_x) > MAX_VALID_COORD || fabsf(vis_max_y) > MAX_VALID_COORD) goto draw;

                fog_start = arena_pos;

                // Compass range circle
                {
                    const auto player = GW::Agents::GetControlledCharacter();
                    if (player) {
                        constexpr int CIRCLE_SEGMENTS = 64;
                        constexpr DWORD CIRCLE_COLOR = D3DCOLOR_ARGB(100, 180, 220, 255);
                        constexpr float CIRCLE_THICKNESS = 0.5f;
                        const float px = player->pos.x, py = player->pos.y;
                        for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
                            const float a1 = static_cast<float>(i) / CIRCLE_SEGMENTS * TWO_PI;
                            const float a2 = static_cast<float>(i + 1) / CIRCLE_SEGMENTS * TWO_PI;
                            push_thick_line_game(fog_count, px + COMPASS_RANGE * cosf(a1), py + COMPASS_RANGE * sinf(a1), px + COMPASS_RANGE * cosf(a2), py + COMPASS_RANGE * sinf(a2), CIRCLE_THICKNESS, CIRCLE_COLOR);
                        }
                    }
                }

                // Frontier edges — cached polylines, smoothed with Chaikin subdivision
                constexpr DWORD FRONTIER_COLOR = D3DCOLOR_ARGB(200, 255, 200, 50);
                constexpr float FRONTIER_HALF_THICKNESS = 1.5f;

                for (const auto& poly : cached_frontier_polylines) {
                    for (size_t i = 0; i + 1 < poly.points.size(); i++) {
                        push_thick_line_game(fog_count, poly.points[i].x, poly.points[i].y, poly.points[i + 1].x, poly.points[i + 1].y, FRONTIER_HALF_THICKNESS, FRONTIER_COLOR);
                    }
                    if (poly.closed && poly.points.size() >= 2) {
                        push_thick_line_game(fog_count, poly.points.back().x, poly.points.back().y, poly.points.front().x, poly.points.front().y, FRONTIER_HALF_THICKNESS, FRONTIER_COLOR);
                    }
                }

                // Map border — cached polylines, smoothed with Chaikin subdivision
                border_start = arena_pos;
                constexpr DWORD BORDER_COLOR = D3DCOLOR_ARGB(160, 200, 220, 255);
                constexpr float HALF_THICKNESS = 0.5f;

                for (const auto& poly : cached_border_polylines) {
                    for (size_t i = 0; i + 1 < poly.points.size(); i++) {
                        const auto& p1 = poly.points[i];
                        const auto& p2 = poly.points[i + 1];
                        if (std::max(p1.x, p2.x) < vis_min_x || std::min(p1.x, p2.x) > vis_max_x) continue;
                        if (std::max(p1.y, p2.y) < vis_min_y || std::min(p1.y, p2.y) > vis_max_y) continue;
                        push_thick_line_game(border_count, p1.x, p1.y, p2.x, p2.y, HALF_THICKNESS, BORDER_COLOR);
                    }
                    if (poly.closed && poly.points.size() >= 2) {
                        push_thick_line_game(border_count, poly.points.back().x, poly.points.back().y, poly.points.front().x, poly.points.front().y, HALF_THICKNESS, BORDER_COLOR);
                    }
                }
            }
        }

        // -----------------------------------------------------------------------
        // Enemy markers — screen space
        // -----------------------------------------------------------------------
        enemy_start = arena_pos;
        if (show_vq_overlay && in_explorable && show_enemy_markers) {
            constexpr float MARKER_SIZE = 9.0f;
            constexpr float OUTLINE_SIZE = MARKER_SIZE + 2.0f;
            constexpr float HALO_SIZE = MARKER_SIZE + 8.0f;
            constexpr DWORD COLOR_OUTLINE = D3DCOLOR_ARGB(200, 0, 0, 0);
            constexpr DWORD COLOR_ALIVE = D3DCOLOR_ARGB(255, 70, 130, 255);
            constexpr DWORD COLOR_STALE = D3DCOLOR_ARGB(180, 255, 180, 50);

            const auto push_diamond = [&](float cx, float cy, float size, DWORD color) {
                Vertex* v = arena_alloc(6);
                if (!v) return;
                v[0] = {cx, cy - size, 0.f, 1.f, color}; // top
                v[1] = {cx + size, cy, 0.f, 1.f, color}; // right
                v[2] = {cx, cy + size, 0.f, 1.f, color}; // bottom
                v[3] = {cx, cy + size, 0.f, 1.f, color}; // bottom
                v[4] = {cx - size, cy, 0.f, 1.f, color}; // left
                v[5] = {cx, cy - size, 0.f, 1.f, color}; // top
                enemy_count += 6;
            };

            constexpr int HALO_SEGMENTS = 12;
            const auto push_halo = [&](float cx, float cy, float radius, DWORD center_color) {
                const DWORD edge_color = center_color & 0x00FFFFFF;
                Vertex* v = arena_alloc(HALO_SEGMENTS * 3);
                if (!v) return;
                for (int i = 0; i < HALO_SEGMENTS; i++) {
                    const float a1 = static_cast<float>(i) / HALO_SEGMENTS * TWO_PI;
                    const float a2 = static_cast<float>(i + 1) / HALO_SEGMENTS * TWO_PI;
                    v[i * 3 + 0] = {cx, cy, 0.f, 1.f, center_color};
                    v[i * 3 + 1] = {cx + radius * cosf(a1), cy + radius * sinf(a1), 0.f, 1.f, edge_color};
                    v[i * 3 + 2] = {cx + radius * cosf(a2), cy + radius * sinf(a2), 0.f, 1.f, edge_color};
                }
                enemy_count += HALO_SEGMENTS * 3;
            };

            constexpr float ARROW_LENGTH = 28.0f;
            constexpr float ARROW_HALF_WIDTH = 6.0f;
            constexpr DWORD COLOR_ARROW = D3DCOLOR_ARGB(240, 255, 220, 50);
            constexpr DWORD COLOR_ARROW_OUTLINE = D3DCOLOR_ARGB(240, 0, 0, 0);

            const auto push_velocity_arrow = [&](float cx, float cy, const TrackedEnemy& enemy) {
                const float vlen_sq = enemy.velocity.x * enemy.velocity.x + enemy.velocity.y * enemy.velocity.y;
                if (vlen_sq < 1.0f) return;

                GW::GamePos offset_pos = enemy.pos;
                const float vlen = sqrtf(vlen_sq);
                offset_pos.x += enemy.velocity.x / vlen * 500.0f;
                offset_pos.y += enemy.velocity.y / vlen * 500.0f;
                GW::Vec2f screen_offset;
                if (!GamePosToMissionMapScreenPos(offset_pos, screen_offset)) return;

                float dx = screen_offset.x - cx;
                float dy = screen_offset.y - cy;
                const float slen = sqrtf(dx * dx + dy * dy);
                if (slen < 0.1f) return;
                dx /= slen;
                dy /= slen;

                const float tip_x = cx + dx * ARROW_LENGTH;
                const float tip_y = cy + dy * ARROW_LENGTH;
                const float base_x = cx + dx * (MARKER_SIZE * 0.5f);
                const float base_y = cy + dy * (MARKER_SIZE * 0.5f);
                const float nx = -dy, ny = dx;
                constexpr float OL = 2.5f;

                Vertex* v = arena_alloc(6);
                if (!v) return;
                v[0] = {tip_x + dx * OL, tip_y + dy * OL, 0.f, 1.f, COLOR_ARROW_OUTLINE};
                v[1] = {base_x + nx * (ARROW_HALF_WIDTH + OL), base_y + ny * (ARROW_HALF_WIDTH + OL), 0.f, 1.f, COLOR_ARROW_OUTLINE};
                v[2] = {base_x - nx * (ARROW_HALF_WIDTH + OL), base_y - ny * (ARROW_HALF_WIDTH + OL), 0.f, 1.f, COLOR_ARROW_OUTLINE};
                v[3] = {tip_x, tip_y, 0.f, 1.f, COLOR_ARROW};
                v[4] = {base_x + nx * ARROW_HALF_WIDTH, base_y + ny * ARROW_HALF_WIDTH, 0.f, 1.f, COLOR_ARROW};
                v[5] = {base_x - nx * ARROW_HALF_WIDTH, base_y - ny * ARROW_HALF_WIDTH, 0.f, 1.f, COLOR_ARROW};
                enemy_count += 6;
            };

            const auto draw_enemy = [&](const TrackedEnemy& enemy) {
                GW::Vec2f screen_pos;
                if (!GamePosToMissionMapScreenPos(enemy.pos, screen_pos)) return;
                const bool is_stale = enemy.state == EnemyState::Stale;
                const DWORD color = is_stale ? COLOR_STALE : COLOR_ALIVE;
                const DWORD halo_color = is_stale ? D3DCOLOR_ARGB(60, 255, 180, 50) : D3DCOLOR_ARGB(80, 70, 130, 255);
                const float cx = screen_pos.x, cy = screen_pos.y;
                push_halo(cx, cy, HALO_SIZE, halo_color);
                push_diamond(cx, cy, OUTLINE_SIZE, COLOR_OUTLINE);
                push_diamond(cx, cy, MARKER_SIZE, color);
                if (is_stale) push_velocity_arrow(cx, cy, enemy);
            };

            for (const auto& [agent_id, enemy] : tracked_enemies) {
                if (enemy.state == EnemyState::Stale) draw_enemy(enemy);
            }
            for (const auto& [agent_id, enemy] : tracked_enemies) {
                if (enemy.state == EnemyState::Alive) draw_enemy(enemy);
            }
        }

        // -----------------------------------------------------------------------
        // VQ toggle button (ImGui)
        // -----------------------------------------------------------------------
        if (in_explorable) {
            constexpr float PADDING = 4.0f;
            const float btn_size = ImGui::GetTextLineHeight() + PADDING * 2;
            const ImVec2 btn_pos(mission_map_top_left.x + PADDING, mission_map_bottom_right.y - btn_size - PADDING);

            ImGui::SetNextWindowPos(btn_pos);
            ImGui::SetNextWindowSize({0, 0});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {2, 2});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {0, 0});
            if (ImGui::Begin("##vq_toggle", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                if (show_vq_overlay) {
                    if (ImGui::Button(ICON_FA_SKULL "##vq_off")) show_vq_overlay = false;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("VQ overlay active. Click to hide.");
                }
                else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    if (ImGui::Button(ICON_FA_SKULL "##vq_on")) show_vq_overlay = true;
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("VQ overlay hidden. Click to show.");
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
        }

    draw:
        const bool has_fog_quads = !cached_inaccessible_verts.empty() || !cached_unexplored_verts.empty();
        if (!fog_count && !border_count && !line_count && !enemy_count && !has_fog_quads) return;

        // -----------------------------------------------------------------------
        // Render state setup
        // -----------------------------------------------------------------------
        DWORD oldAlphaBlend, oldSrcBlend, oldDestBlend, oldScissorTest, oldFVF;
        RECT oldScissorRect;
        dx_device->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
        dx_device->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
        dx_device->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);
        dx_device->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissorTest);
        dx_device->GetScissorRect(&oldScissorRect);
        dx_device->GetFVF(&oldFVF);

        dx_device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        dx_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dx_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dx_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dx_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

        RECT scissorRect;
        scissorRect.left = static_cast<LONG>(mission_map_top_left.x);
        scissorRect.top = static_cast<LONG>(mission_map_top_left.y);
        scissorRect.right = static_cast<LONG>(mission_map_bottom_right.x);
        scissorRect.bottom = static_cast<LONG>(mission_map_bottom_right.y);
        dx_device->SetScissorRect(&scissorRect);

        // -----------------------------------------------------------------------
        // Draw GPU-projected fog quads (cached in game coordinates)
        // -----------------------------------------------------------------------
        if (has_fog_quads) {
            D3DMATRIX game_to_screen;
            if (BuildGameToScreenWorldMatrix(game_to_screen)) {
                D3DMATRIX saved_world, saved_view, saved_proj;
                dx_device->GetTransform(D3DTS_WORLD, &saved_world);
                dx_device->GetTransform(D3DTS_VIEW, &saved_view);
                dx_device->GetTransform(D3DTS_PROJECTION, &saved_proj);

                D3DVIEWPORT9 vp;
                dx_device->GetViewport(&vp);
                const auto ortho = MakeOrthoProjection(static_cast<float>(vp.Width), static_cast<float>(vp.Height));

                dx_device->SetTransform(D3DTS_WORLD, &game_to_screen);
                dx_device->SetTransform(D3DTS_VIEW, &IDENTITY_MATRIX);
                dx_device->SetTransform(D3DTS_PROJECTION, &ortho);

                dx_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
                dx_device->SetRenderState(D3DRS_ZENABLE, FALSE);
                dx_device->SetRenderState(D3DRS_LIGHTING, FALSE);

                if (!cached_inaccessible_verts.empty())
                    dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, cached_inaccessible_verts.size() / 3, cached_inaccessible_verts.data(), sizeof(GameVertex));
                if (!cached_unexplored_verts.empty())
                    dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, cached_unexplored_verts.size() / 3, cached_unexplored_verts.data(), sizeof(GameVertex));

                dx_device->SetTransform(D3DTS_WORLD, &saved_world);
                dx_device->SetTransform(D3DTS_VIEW, &saved_view);
                dx_device->SetTransform(D3DTS_PROJECTION, &saved_proj);
            }
        }

        // -----------------------------------------------------------------------
        // Draw — all from one contiguous static array, no heap involvement
        // -----------------------------------------------------------------------
        dx_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        if (fog_count) dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, fog_count / 3, vertex_arena + fog_start, sizeof(Vertex));
        if (border_count) dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, border_count / 3, vertex_arena + border_start, sizeof(Vertex));
        if (line_count) dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, line_count / 3, vertex_arena + line_start, sizeof(Vertex));
        if (enemy_count) dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, enemy_count / 3, vertex_arena + enemy_start, sizeof(Vertex));

        // -----------------------------------------------------------------------
        // Restore render states
        // -----------------------------------------------------------------------
        dx_device->SetFVF(oldFVF);
        dx_device->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
        dx_device->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
        dx_device->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
        dx_device->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissorTest);
        dx_device->SetScissorRect(&oldScissorRect);

        // -----------------------------------------------------------------------
        // Enemy count label (ImGui)
        // -----------------------------------------------------------------------
        if (show_vq_overlay && in_explorable && show_enemy_markers) {
            int alive_count = 0, stale_count = 0;
            for (const auto& [id, enemy] : tracked_enemies) {
                if (enemy.state == EnemyState::Alive)
                    alive_count++;
                else if (enemy.state == EnemyState::Stale)
                    stale_count++;
            }

            const uint32_t foes_remaining = GW::Map::GetFoesToKill();
            const bool has_vq_data = foes_remaining > 0 || GW::Map::GetFoesKilled() > 0;

            if (alive_count > 0 || stale_count > 0 || (has_vq_data && foes_remaining > 0)) {
                char label[128];
                int pos = 0;
                if (alive_count > 0 || stale_count > 0) {
                    pos += snprintf(label + pos, sizeof(label) - pos, "%d", alive_count);
                    if (stale_count > 0) pos += snprintf(label + pos, sizeof(label) - pos, " (+%d?)", stale_count);
                    if (has_vq_data) pos += snprintf(label + pos, sizeof(label) - pos, " / %u remaining", foes_remaining);
                }
                else {
                    pos += snprintf(label + pos, sizeof(label) - pos, "%u remaining", foes_remaining);
                }

                constexpr float PADDING = 8.0f;
                const float btn_size = ImGui::GetTextLineHeight() + 12.0f;
                const ImVec2 text_pos(mission_map_top_left.x + PADDING + btn_size, mission_map_bottom_right.y - ImGui::GetTextLineHeight() - PADDING);

                auto* draw_list = ImGui::GetBackgroundDrawList();
                draw_list->AddText({text_pos.x + 1, text_pos.y + 1}, IM_COL32(0, 0, 0, 220), label);
                draw_list->AddText({text_pos.x - 1, text_pos.y - 1}, IM_COL32(0, 0, 0, 220), label);
                draw_list->AddText({text_pos.x + 1, text_pos.y - 1}, IM_COL32(0, 0, 0, 220), label);
                draw_list->AddText({text_pos.x - 1, text_pos.y + 1}, IM_COL32(0, 0, 0, 220), label);
                draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), label);
            }
        }
    }
} // namespace

void MissionMapWidget::LoadSettings(ToolboxIni* ini)
{
    ToolboxWidget::LoadSettings(ini);
    LOAD_BOOL(draw_all_terrain_lines);
    LOAD_BOOL(draw_all_minimap_lines);
    LOAD_BOOL(show_enemy_markers);
    LOAD_BOOL(show_exploration_overlay);
    LOAD_BOOL(show_vq_overlay);
}

void MissionMapWidget::SaveSettings(ToolboxIni* ini)
{
    ToolboxWidget::SaveSettings(ini);
    SAVE_BOOL(draw_all_terrain_lines);
    SAVE_BOOL(draw_all_minimap_lines);
    SAVE_BOOL(show_enemy_markers);
    SAVE_BOOL(show_exploration_overlay);
    SAVE_BOOL(show_vq_overlay);
}

void MissionMapWidget::Draw(IDirect3DDevice9* dx_device)
{
    if (show_vq_overlay) {
        // Throttle tracking updates — no need to run every render frame.
        // Enemy positions update at ~10Hz in GW anyway.
        static DWORD last_tracking_tick = 0;
        const DWORD now = GetTickCount();
        if (now - last_tracking_tick >= 100) { // 10Hz
            last_tracking_tick = now;
            UpdateEnemyTracking();
            if (const auto player = GW::Agents::GetControlledCharacter()) {
                UpdateExploration(player->pos);
            }
        }
    }
    if (visible) ::Draw(dx_device);
    HookMissionMapFrame();
}

void MissionMapWidget::DrawSettingsInternal()
{
    ImGui::Checkbox("Draw all terrain lines", &draw_all_terrain_lines);
    ImGui::Checkbox("Draw all minimap lines", &draw_all_minimap_lines);
    ImGui::Checkbox("Show enemy markers on mission map", &show_enemy_markers);
    ImGui::ShowHelp("Tracks enemy positions as they enter compass range.\nBlue = alive, Orange = last known (moved away).\nArrows on orange markers show last movement direction.");
    ImGui::Checkbox("Show exploration overlay on mission map", &show_exploration_overlay);
    ImGui::ShowHelp("Highlights areas you've explored during this session on the mission map.");
}

bool MissionMapWidget::WndProc(const UINT Message, WPARAM, LPARAM lParam)
{
    switch (Message) {
        case WM_GW_RBUTTONCLICK:
            GW::Vec2f cursor_pos = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (!IsScreenPosOnMissionMap(cursor_pos)) break;
            if (GW::UI::GetCurrentTooltip()) break;
            world_map_click_pos = ScreenPosToMissionMapCoords(cursor_pos);
            ImGui::SetContextMenu(MissionMapContextMenu);
            break;
    }
    return false;
}

void MissionMapWidget::Terminate()
{
    ToolboxWidget::Terminate();
    if (OnMissionMap_UICallback_Func) GW::Hook::RemoveHook(OnMissionMap_UICallback_Func);
    OnMissionMap_UICallback_Func = nullptr;
    delete[] cached_walkable_grid;
    cached_walkable_grid = nullptr;
    delete[] explored_cells;
    explored_cells = nullptr;
}
