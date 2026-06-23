#include "renderer.h"
#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/System/Angle.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>
#include <algorithm>
#include <cmath>
#include <string>

// ============================================================
//  USE_MAP_CACHE  --  Map rendering optimization toggle
// ============================================================
//  1 = (default) render the entire map (base + decorations) into an
//      off-screen RenderTexture once per map, then blit that cached
//      texture every frame. Very large FPS win on Course B.
//  0 = old path: draw every tile every frame. Use this if you suspect
//      the cache is misbehaving visually.
// ============================================================
#define USE_MAP_CACHE 1


namespace bestdriver {

    // Penalty reason text helper
    static std::string toKoreanReason(const std::string& eng) {
        if (eng.find("WrongGear") != std::string::npos) return "Gear Error";
        if (eng.find("Speeding") != std::string::npos) return "Speeding";
        if (eng.find("CenterLine") != std::string::npos) return "Center Line Cross";
        if (eng.find("CenterLineCross") != std::string::npos) return "Center Line Cross";
        if (eng.find("StopLineCross") != std::string::npos) return "Stop Line Violation";
        if (eng.find("Crosswalk on Red") != std::string::npos) return "Red Light Crosswalk (DQ)";
        if (eng.find("ObstacleCollision") != std::string::npos) return "Obstacle Collision";
        if (eng.find("NoSeatbelt") != std::string::npos) return "No Seatbelt";
        if (eng.find("SpeedBump") != std::string::npos) return "Speed Bump Violation";
        if (eng.find("SuddenBrake") != std::string::npos) return "Sudden Brake";
        if (eng.find("SuddenAccel") != std::string::npos) return "Sudden Accel";
        return eng;
    }

    namespace {
        enum class MenuStep {
            MainMenu,
            CourseSelect,
            ParkingSelect
        };

        static MenuStep g_currentStep = MenuStep::MainMenu;
        static int g_localSelectedCourse = 0;
        static int g_localSelectedParking = 0;

        float radToDeg(float radians) {
            return radians * 180.0f / 3.14159265358979323846f;
        }

        ImVec4 HSLtoRGB(float h, float s, float l, float a = 1.0f) {
            auto hue2rgb = [](float p, float q, float t) {
                if (t < 0.0f) t += 1.0f;
                if (t > 1.0f) t -= 1.0f;
                if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
                if (t < 1.0f / 2.0f) return q;
                if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
                return p;
                };
            float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
            float p = 2.0f * l - q;
            return ImVec4(hue2rgb(p, q, h + 1.0f / 3.0f), hue2rgb(p, q, h), hue2rgb(p, q, h - 1.0f / 3.0f), a);
        }
    }

    Renderer::Renderer() : initialized_(false), zoom_(0.50f), pixelsPerCell_(48.0f),
                           mapCacheValid_(false), mapCacheW_(0), mapCacheH_(0), mapCacheOwner_(nullptr),
                           mapCacheFingerprint_(0ULL) {}

    Renderer::~Renderer() {
        if (initialized_) {
            ImGui::SFML::Shutdown();
            initialized_ = false;
        }
    }

    bool Renderer::init() {
        if (initialized_) return true;
        window_.create(sf::VideoMode::getDesktopMode(), "BestDriver Arcade", sf::Style::Default, sf::State::Fullscreen);
        window_.setFramerateLimit(60);
        window_.requestFocus();
        if (!ImGui::SFML::Init(window_)) return false;
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowRounding = 12.0f;
        ImGui::GetStyle().FrameRounding = 8.0f;
        initialized_ = true;
        return true;
    }

    bool Renderer::isOpen() const { return window_.isOpen(); }

    bool Renderer::processEvents() {
        while (const auto event = window_.pollEvent()) {
            ImGui::SFML::ProcessEvent(window_, *event);
            if (event->is<sf::Event::Closed>()) {
                window_.close();
                return false;
            }
            // Mouse wheel zoom (skip if ImGui wants the mouse, e.g. slider)
            if (const auto* scroll = event->getIf<sf::Event::MouseWheelScrolled>()) {
                if (!ImGui::GetIO().WantCaptureMouse) {
                    float step = 0.05f;
                    if (scroll->delta > 0) zoom_ = std::clamp(zoom_ + step, 0.15f, 4.0f);
                    else if (scroll->delta < 0) zoom_ = std::clamp(zoom_ - step, 0.15f, 4.0f);
                }
            }
        }
        return window_.isOpen();
    }

    void Renderer::beginFrame(sf::Time dt) {
        ImGui::SFML::Update(window_, dt);
        window_.clear(sf::Color(10, 12, 18));
    }

    void Renderer::endFrame() {
        resetUiView();
        ImGui::SFML::Render(window_);
        window_.display();
    }

    sf::RenderWindow& Renderer::window() { return window_; }
    const sf::RenderWindow& Renderer::window() const { return window_; }
    float Renderer::getZoom() const { return zoom_; }
    void Renderer::setZoom(float zoom) { zoom_ = std::clamp(zoom, 0.15f, 4.0f); }

    float Renderer::recommendZoom(const MapSystem& map, float fillRatio) const {
        if (map.getWidth() <= 0 || map.getHeight() <= 0) return 1.0f;
        const sf::Vector2u size = window_.getSize();
        const float zoomX = (static_cast<float>(size.x) * fillRatio) / (map.getWidth() * pixelsPerCell_);
        const float zoomY = (static_cast<float>(size.y) * fillRatio) / (map.getHeight() * pixelsPerCell_);
        return std::clamp(std::min(zoomX, zoomY), 0.15f, 4.0f);
    }

    void Renderer::applyWorldView(const MapSystem& map, const VehicleState& focus) {
        sf::View view = window_.getDefaultView();
        const sf::Vector2u size = window_.getSize();
        view.setSize({ static_cast<float>(size.x) / zoom_,
                       static_cast<float>(size.y) / zoom_ });

        sf::Vector2f center = worldToPixels(focus.position);
        const float mapPW = static_cast<float>(map.getWidth()) * pixelsPerCell_;
        const float mapPH = static_cast<float>(map.getHeight()) * pixelsPerCell_;
        const float halfW = view.getSize().x * 0.5f;
        const float halfH = view.getSize().y * 0.5f;

        center.x = (mapPW > view.getSize().x)
            ? std::clamp(center.x, halfW, mapPW - halfW) : mapPW * 0.5f;
        center.y = (mapPH > view.getSize().y)
            ? std::clamp(center.y, halfH, mapPH - halfH) : mapPH * 0.5f;

        view.setCenter(center);
        window_.setView(view);
    }

    void Renderer::resetUiView() { window_.setView(window_.getDefaultView()); }

    sf::Vector2f Renderer::worldToPixels(const Vec2& p) const {
        return sf::Vector2f(static_cast<float>(p.x * pixelsPerCell_), static_cast<float>(p.y * pixelsPerCell_));
    }


    sf::Color Renderer::colorForRawCell(int rawCell) {
        switch (rawCell) {
        case RC_ROAD:     return sf::Color(62, 66, 78);
        case RC_CLINE:    return sf::Color(228, 186, 62);
        case RC_CURB:     return sf::Color(160, 70, 70);
        case RC_GRASS:    return sf::Color(58, 115, 72);
        case RC_PARK:     return sf::Color(90, 94, 110);
        case RC_PARKLINE: return sf::Color(145, 145, 155);
        case RC_STOP:     return sf::Color(210, 210, 210);
        case RC_XWALK:    return sf::Color(215, 215, 215);
        case RC_BUMP:     return sf::Color(240, 154, 60);
        case RC_BLDG:     return sf::Color(90, 100, 120);
        case RC_YCAR:     return sf::Color(235, 196, 49);
        case RC_WCAR:     return sf::Color(245, 245, 245);
        case RC_CWINDOW:  return sf::Color(115, 175, 220);
        default:          return sf::Color(34, 36, 44);
        }
    }

    void Renderer::drawMapCellsLegacy(const MapSystem& map) {
        sf::RectangleShape tile;
        tile.setSize({ pixelsPerCell_ + 1.0f, pixelsPerCell_ + 1.0f });
        for (int y = 0; y < map.getHeight(); ++y) {
            for (int x = 0; x < map.getWidth(); ++x) {
                const int raw = map.getRawCell(x, y);
                tile.setPosition({ x * pixelsPerCell_, y * pixelsPerCell_ });
                tile.setFillColor(colorForRawCell(raw));
                window_.draw(tile);
                if (raw == RC_CLINE) {
                    sf::RectangleShape centerLine;
                    centerLine.setSize({ pixelsPerCell_ * 0.7f, pixelsPerCell_ * 0.12f });
                    centerLine.setOrigin({ centerLine.getSize().x * 0.5f, centerLine.getSize().y * 0.5f });
                    centerLine.setPosition({ x * pixelsPerCell_ + pixelsPerCell_ * 0.5f,
                                            y * pixelsPerCell_ + pixelsPerCell_ * 0.5f });
                    centerLine.setFillColor(sf::Color(255, 210, 50));
                    window_.draw(centerLine);
                }
                else if (raw == RC_STOP) {
                    sf::RectangleShape stopLine;
                    stopLine.setSize({ pixelsPerCell_, pixelsPerCell_ * 0.18f });
                    stopLine.setPosition({ x * pixelsPerCell_, y * pixelsPerCell_ + pixelsPerCell_ * 0.41f });
                    stopLine.setFillColor(sf::Color(246, 246, 246));
                    window_.draw(stopLine);
                }
                else if (raw == RC_XWALK) {
                    for (int stripe = 0; stripe < 3; ++stripe) {
                        sf::RectangleShape zebra;
                        zebra.setSize({ pixelsPerCell_, pixelsPerCell_ * 0.14f });
                        zebra.setPosition({ x * pixelsPerCell_, y * pixelsPerCell_ + pixelsPerCell_ * (0.15f + stripe * 0.24f) });
                        zebra.setFillColor(sf::Color(255, 255, 255, 220));
                        window_.draw(zebra);
                    }
                }
                else if (raw == RC_BUMP) {
                    sf::RectangleShape bump;
                    bump.setSize({ pixelsPerCell_, pixelsPerCell_ * 0.24f });
                    bump.setPosition({ x * pixelsPerCell_, y * pixelsPerCell_ + pixelsPerCell_ * 0.38f });
                    bump.setFillColor(sf::Color(250, 170, 55));
                    window_.draw(bump);
                }
                else if (raw == RC_PARKLINE) {
                    sf::RectangleShape line;
                    line.setSize({ pixelsPerCell_, pixelsPerCell_ * 0.08f });
                    line.setPosition({ x * pixelsPerCell_, y * pixelsPerCell_ + pixelsPerCell_ * 0.46f });
                    line.setFillColor(sf::Color(235, 235, 235));
                    window_.draw(line);
                }
            }
        }
    }

    void Renderer::invalidateMapCache() {
        mapCacheValid_ = false;
        mapCacheOwner_ = nullptr;
    }

    void Renderer::rebuildMapCache(const MapSystem& map) {
        const int w = map.getWidth();
        const int h = map.getHeight();
        if (w <= 0 || h <= 0) {
            mapCacheValid_ = false;
            return;
        }

        const unsigned int texW = static_cast<unsigned int>((float)w * pixelsPerCell_);
        const unsigned int texH = static_cast<unsigned int>((float)h * pixelsPerCell_);

        // (Re)allocate the cache texture only when dimensions change.
        if (!mapCacheValid_ || mapCacheW_ != w || mapCacheH_ != h) {
            if (!mapCache_.resize({ texW, texH })) {
                // Allocation failed (driver/GPU limits). Fall back to uncached.
                mapCacheValid_ = false;
                return;
            }
            mapCacheW_ = w;
            mapCacheH_ = h;
        }

        // Render the full tile art (base + decorations) into the cache.
        // This uses the Into-variants which route every draw call through
        // the given RenderTarget instead of window_, letting us bake the
        // entire map into a single texture once.
        // Explicitly set the texture's view to its default so drawing is
        // done in texture-local pixel coordinates (0,0) to (texW, texH).
        // Without this, a stale view from a previous use of mapCache_ could
        // misalign tiles or clip them entirely.
        mapCache_.setView(mapCache_.getDefaultView());
        mapCache_.clear(sf::Color(22, 24, 30));
        drawMapCellsInto(mapCache_, map);
        mapCache_.display();

        mapCacheValid_ = true;
        mapCacheOwner_ = static_cast<const void*>(&map);
    }

    void Renderer::drawMapCached(const MapSystem& map) {
#if USE_MAP_CACHE
        const int w = map.getWidth();
        const int h = map.getHeight();

        // Compute a lightweight fingerprint that changes when map contents
        // change, even if the MapSystem object's address stays the same
        // (e.g. session.map regenerated with a new layout).
        unsigned long long fp = 0ULL;
        if (w > 0 && h > 0) {
            // Mix spawn position and a handful of tile samples at fixed
            // grid positions. Cheap to compute yet very likely to differ
            // between Course A / B / parallel parking / T parking maps.
            Vec2 sp = map.getSpawnPosition();
            fp ^= static_cast<unsigned long long>(static_cast<int>(sp.x * 1000.0f)) * 2654435761ULL;
            fp ^= static_cast<unsigned long long>(static_cast<int>(sp.y * 1000.0f)) * 40503ULL;
            fp ^= static_cast<unsigned long long>(w) * 73856093ULL;
            fp ^= static_cast<unsigned long long>(h) * 19349663ULL;
            const int sampleCount = 9;
            for (int i = 0; i < sampleCount; ++i) {
                int sx = (w * (i + 1)) / (sampleCount + 1);
                int sy = (h * (i + 1)) / (sampleCount + 1);
                int tile = static_cast<int>(map.getTileId(sx, sy));
                fp = fp * 1315423911ULL + static_cast<unsigned long long>(tile);
            }
        }

        // Decide whether we need to (re)build the cache this frame.
        const bool needRebuild = !mapCacheValid_
            || mapCacheOwner_ != static_cast<const void*>(&map)
            || mapCacheW_ != w
            || mapCacheH_ != h
            || mapCacheFingerprint_ != fp;

        if (needRebuild) {
            rebuildMapCache(map);
            mapCacheFingerprint_ = fp;
        }

        if (!mapCacheValid_) {
            // Cache unavailable (e.g. allocation failed). Fall back to the
            // original direct-drawing path so the map still renders correctly.
            drawMapCells(map);
            return;
        }

        // Fast path: one textured quad for the entire map.
        sf::Sprite sprite(mapCache_.getTexture());
        window_.draw(sprite);
#else
        // Toggle off: render tiles the old way every frame.
        drawMapCells(map);
#endif
    }

    // ============================================================
    //  Target-agnostic drawing helpers
    // ------------------------------------------------------------
    //  These are exact copies of drawMapCells / drawMapCellsLegacy /
    //  drawTile, but every draw call routes through the given
    //  'target' instead of window_. Used by rebuildMapCache to bake
    //  the whole map (base + decorations) into an off-screen texture.
    //  Keep these in sync with the originals if the originals change.
    // ============================================================

// Generated Into-variants:

    void Renderer::drawMapCellsLegacyInto(sf::RenderTarget& target, const MapSystem& map) {
        sf::RectangleShape tile;
        tile.setSize({ pixelsPerCell_ + 1.0f, pixelsPerCell_ + 1.0f });
        for (int y = 0; y < map.getHeight(); ++y) {
            for (int x = 0; x < map.getWidth(); ++x) {
                const int raw = map.getRawCell(x, y);
                tile.setPosition({ x * pixelsPerCell_, y * pixelsPerCell_ });
                tile.setFillColor(colorForRawCell(raw));
                target.draw(tile);
                if (raw == RC_CLINE) {
                    sf::RectangleShape centerLine;
                    centerLine.setSize({ pixelsPerCell_ * 0.7f, pixelsPerCell_ * 0.12f });
                    centerLine.setOrigin({ centerLine.getSize().x * 0.5f, centerLine.getSize().y * 0.5f });
                    centerLine.setPosition({ x * pixelsPerCell_ + pixelsPerCell_ * 0.5f,
                                            y * pixelsPerCell_ + pixelsPerCell_ * 0.5f });
                    centerLine.setFillColor(sf::Color(255, 210, 50));
                    target.draw(centerLine);
                }
                else if (raw == RC_STOP) {
                    sf::RectangleShape stopLine;
                    stopLine.setSize({ pixelsPerCell_, pixelsPerCell_ * 0.18f });
                    stopLine.setPosition({ x * pixelsPerCell_, y * pixelsPerCell_ + pixelsPerCell_ * 0.41f });
                    stopLine.setFillColor(sf::Color(246, 246, 246));
                    target.draw(stopLine);
                }
                else if (raw == RC_XWALK) {
                    for (int stripe = 0; stripe < 3; ++stripe) {
                        sf::RectangleShape zebra;
                        zebra.setSize({ pixelsPerCell_, pixelsPerCell_ * 0.14f });
                        zebra.setPosition({ x * pixelsPerCell_, y * pixelsPerCell_ + pixelsPerCell_ * (0.15f + stripe * 0.24f) });
                        zebra.setFillColor(sf::Color(255, 255, 255, 220));
                        target.draw(zebra);
                    }
                }
                else if (raw == RC_BUMP) {
                    sf::RectangleShape bump;
                    bump.setSize({ pixelsPerCell_, pixelsPerCell_ * 0.24f });
                    bump.setPosition({ x * pixelsPerCell_, y * pixelsPerCell_ + pixelsPerCell_ * 0.38f });
                    bump.setFillColor(sf::Color(250, 170, 55));
                    target.draw(bump);
                }
                else if (raw == RC_PARKLINE) {
                    sf::RectangleShape line;
                    line.setSize({ pixelsPerCell_, pixelsPerCell_ * 0.08f });
                    line.setPosition({ x * pixelsPerCell_, y * pixelsPerCell_ + pixelsPerCell_ * 0.46f });
                    line.setFillColor(sf::Color(235, 235, 235));
                    target.draw(line);
                }
            }
        }
    }

    void Renderer::drawMapCellsInto(sf::RenderTarget& target, const MapSystem& map) {
        if (map.hasTileGrid()) {
            const float h = pixelsPerCell_;

            for (int y = 0; y < map.getHeight(); ++y) {
                for (int x = 0; x < map.getWidth(); ++x) {
                    TileID id = map.getTileId(x, y);
                    float px = (float)x * h;
                    float py = (float)y * h;

                    // Horizontal dashes: position the dash line next to the
                    // adjacent center-line tile so they visually align.
                    if (id == RD_DASH_H) {
                        sf::RectangleShape bg({ h, h });
                        bg.setFillColor({ 68,68,72 });
                        bg.setPosition({ px, py });
                        target.draw(bg);

                        float lineY = h * 0.5f;
                        if (y > 0 && map.getTileId(x, y - 1) == RD_LANE_H)
                            lineY = h * 0.82f;
                        else if (y + 1 < map.getHeight() && map.getTileId(x, y + 1) == RD_LANE_H)
                            lineY = h * 0.18f;

                        const int dashCount = 6;
                        const float sideMargin = 4.f;
                        const float gap = 4.f;
                        const float dashW = (h - sideMargin * 2.f - gap * (dashCount - 1)) / dashCount;

                        for (int i = 0; i < dashCount; ++i) {
                            float xPos = sideMargin + i * (dashW + gap);
                            sf::RectangleShape dash({ dashW, 2.f });
                            dash.setFillColor({ 230,230,235 });
                            dash.setPosition({ px + xPos, py + lineY - 1.f });
                            target.draw(dash);
                        }
                        continue;
                    }

                    // Vertical dashes: same logic, rotated.
                    if (id == RD_DASH_V) {
                        sf::RectangleShape bg({ h, h });
                        bg.setFillColor({ 68,68,72 });
                        bg.setPosition({ px, py });
                        target.draw(bg);

                        float lineX = h * 0.5f;
                        if (x > 0 && map.getTileId(x - 1, y) == RD_LANE_V)
                            lineX = h * 0.82f;
                        else if (x + 1 < map.getWidth() && map.getTileId(x + 1, y) == RD_LANE_V)
                            lineX = h * 0.18f;

                        const int dashCount = 6;
                        const float sideMargin = 4.f;
                        const float gap = 4.f;
                        const float dashH = (h - sideMargin * 2.f - gap * (dashCount - 1)) / dashCount;

                        for (int i = 0; i < dashCount; ++i) {
                            float yPos = sideMargin + i * (dashH + gap);
                            sf::RectangleShape dash({ 2.f, dashH });
                            dash.setFillColor({ 230,230,235 });
                            dash.setPosition({ px + lineX - 1.f, py + yPos });
                            target.draw(dash);
                        }
                        continue;
                    }

                    drawTileInto(target, id, px, py);
                }
            }
        }
        else {
            drawMapCellsLegacyInto(target, map);
        }
    }

    void Renderer::drawTileInto(sf::RenderTarget& target, TileID id, float x, float y) {
        const float h = pixelsPerCell_;
        auto rectFull = [&](sf::Color c) {
            sf::RectangleShape s({ h, h });
            s.setFillColor(c);
            s.setPosition({ x, y });
            target.draw(s);
            };
        auto rect = [&](sf::Color c, float ox, float oy, float w, float ht) {
            sf::RectangleShape s({ w, ht });
            s.setFillColor(c);
            s.setPosition({ x + ox, y + oy });
            target.draw(s);
            };
        auto circ = [&](sf::Color c, float cx, float cy, float r) {
            sf::CircleShape s(r);
            s.setFillColor(c);
            s.setOrigin({ r, r });
            s.setPosition({ x + cx, y + cy });
            target.draw(s);
            };
        switch (id) {
        case RD_PLAIN:    rectFull({ 68,68,72 }); break;
        case RD_LANE_H:   rectFull({ 68,68,72 }); rect({ 230,195,45 }, 0, h / 2 - 2, h, 4); rect({ 230,195,45 }, 0, h / 2 + 2, h, 2); break;
        case RD_LANE_V:   rectFull({ 68,68,72 }); rect({ 230,195,45 }, h / 2 - 2, 0, 4, h); rect({ 230,195,45 }, h / 2 + 2, 0, 2, h); break;
        case RD_DASH_H:   rectFull({ 68,68,72 }); for (int i = 0; i < 6; i++) rect({ 230,230,235 }, i * h / 6.f, h / 2 - 1, h / 6 - 2.f, 2.f); break;
        case RD_DASH_V:   rectFull({ 68,68,72 }); for (int i = 0; i < 6; i++) rect({ 230,230,235 }, h / 2 - 1, i * h / 6.f, 2.f, h / 6 - 2.f); break;
        case RD_CROSS:    rectFull({ 68,68,72 }); break;
        case RD_CWALK_H:  rectFull({ 80,80,85 }); for (int i = 0; i < 4; i++) rect({ 240,240,245 }, 2, i * h / 4.f + 2, h - 4, h / 4 - 4.f); break;
        case RD_CWALK_V:  rectFull({ 80,80,85 }); for (int i = 0; i < 4; i++) rect({ 240,240,245 }, i * h / 4.f + 2, 2, h / 4 - 4.f, h - 4); break;
        case RD_SHOULDER: rectFull({ 58,60,65 }); rect({ 78,80,85 }, 0, 0, h, 2); break;
        case SW_PLAIN:    rectFull({ 185,178,165 }); rect({ 165,158,145 }, 0, 0, h, 1); rect({ 165,158,145 }, 0, h / 2, h, 1); rect({ 165,158,145 }, 0, 0, 1, h); rect({ 165,158,145 }, h / 2, 0, 1, h); break;
        case SW_CURB:     rectFull({ 140,135,125 }); rect({ 160,155,145 }, 1, 1, h - 2, h - 2); break;
        case SW_LAMP:
            rectFull({ 185,178,165 }); rect({ 165,158,145 }, 0, 0, h, 1); rect({ 165,158,145 }, 0, h / 2, h, 1);
            rect({ 165,158,145 }, 0, 0, 1, h); rect({ 165,158,145 }, h / 2, 0, 1, h);
            rect({ 55,55,60 }, h / 2 - 1, h / 3, 2, h * 2 / 3); rect({ 245,225,90 }, h / 2 - 5, h / 3 - 5, 10, 6);
            circ({ 255,248,180,160 }, h / 2, h / 3 - 2, 6); break;
        case SW_BENCH:    rectFull({ 185,178,165 }); rect({ 140,100,60 }, 3, h * 2 / 3, h - 6, 5); rect({ 110,80,50 }, 4, h / 2 + 2, 4, h / 3); rect({ 110,80,50 }, h - 8, h / 2 + 2, 4, h / 3); break;
        case SW_TREE:     rectFull({ 185,178,165 }); rect({ 120,85,45 }, h / 2 - 2, h / 2, 4, h / 2 - 2); circ({ 60,150,55 }, h / 2 + 3, h / 2 - 2, 9); circ({ 72,168,62 }, h / 2 - 3, h / 2 - 5, 11); break;
        case BLD_APART:
            rectFull({ 155,165,185 }); rect({ 100,115,145 }, 0, 0, h, h * 0.15f);
            for (int wy = 0; wy < 3; wy++) for (int wx = 0; wx < 3; wx++) rect({ 190,215,240 }, 3 + wx * h * 0.22f, h * 0.22f + wy * h * 0.2f, h * 0.16f, h * 0.12f);
            break;
        case BLD_HOUSE:
            rectFull({ 200,175,140 }); rect({ 165,75,55 }, 1, 1, h - 2, h / 2 - 2);
            rect({ 175,145,105 }, 1, h / 2, h - 2, h / 2 - 1);
            rect({ 195,225,255 }, 3, h / 2 + 3, h * 0.18f, h * 0.16f);
            rect({ 195,225,255 }, h - h * 0.24f, h / 2 + 3, h * 0.18f, h * 0.16f);
            rect({ 110,75,45 }, h / 2 - 3, h - h * 0.22f, 6, h * 0.22f);
            break;
        case BLD_SHOP:
            rectFull({ 215,205,190 }); rect({ 130,155,200 }, 1, 1, h - 2, h / 2 - 2);
            rect({ 240,70,70 }, 2, h / 2, h - 4, h * 0.16f);
            rect({ 175,215,250,190 }, 2, h / 2 + h * 0.18f, h - 4, h / 2 - h * 0.22f);
            break;
        case BLD_OFFICE:
            rectFull({ 180,190,200 }); rect({ 120,135,155 }, 0, 0, h, h * 0.1f);
            for (int wy = 0; wy < 4; wy++) for (int wx = 0; wx < 3; wx++) rect({ 160,195,230,180 }, 2 + wx * h * 0.24f, h * 0.12f + wy * h * 0.16f, h * 0.2f, h * 0.12f);
            break;
        case BLD_PARK:
            rectFull({ 100,175,80 }); circ({ 80,155,220 }, h / 2, h / 2, h * 0.22f); circ({ 100,175,80 }, h / 2, h / 2, h * 0.12f);
            {
                sf::RectangleShape bo({ h,h }); bo.setPosition({ x,y }); bo.setFillColor(sf::Color::Transparent);
                bo.setOutlineColor({ 80,150,60 }); bo.setOutlineThickness(2.f); target.draw(bo);
            }
            break;
        case BLD_PARKING:
            rectFull({ 85,85,92 }); rect({ 210,210,215 }, 0, 2, 2, h - 4); rect({ 210,210,215 }, h - 2, 2, 2, h - 4);
            rect({ 210,210,215 }, 0, 2, h, 2); rect({ 210,210,215 }, 0, h / 2, h, 1);
            rect({ 55,90,210 }, h / 2 - 5, h / 2 - h * 0.16f, 10, h * 0.3f);
            break;
        case BLD_ON_PARK_V:
            rectFull({ 75,75,80 }); rect({ 220,220,225 }, 1, 0, 2, h); rect({ 220,220,225 }, h - 3, 0, 2, h);
            break;
        case BLD_ON_PARK_H:
            rectFull({ 75,75,80 }); rect({ 220,220,225 }, 0, 1, h, 2); rect({ 220,220,225 }, 0, h - 3, h, 2);
            break;
        case NAT_GRASS:
            rectFull({ 95,170,75 }); for (int gi = 0; gi < 5; gi++) rect({ 75,150,58 }, gi * h * 0.14f + 2, (float)((gi * 7) % (int)(h - 4)), 3, 4);
            break;
        case NAT_TREE:
            rectFull({ 95,170,75 }); rect({ 130,90,50 }, h / 2 - 2, h / 2 + 2, 4, h / 2 - 2);
            circ({ 50,130,50 }, h / 2 + 4, h / 2 - 2, h * 0.2f); circ({ 60,150,60 }, h / 2 - 4, h / 2 - 4, h * 0.24f);
            circ({ 72,165,68 }, h / 2 + 2, h / 2 - h * 0.18f, h * 0.22f);
            break;
        case NAT_GARDEN:
            rectFull({ 100,175,80 }); for (int gi = 0; gi < 4; gi++) circ({ 220,80,80 }, 5 + gi * h * 0.16f, h * 0.52f, h * 0.09f);
            for (int gi = 0; gi < 3; gi++) circ({ 80,180,80 }, 9 + gi * h * 0.18f, h * 0.38f, h * 0.07f);
            break;
            // -- Course B new tiles --
        case NAT_POND:
            rectFull({ 98,172,78 }); circ({ 80,148,210 }, h / 2, h / 2, h * 0.22f);
            circ({ 100,165,225 }, h / 2, h / 2, h * 0.14f); break;
        case BLD_VILLA:
            rectFull({ 175,185,200 }); rect({ 105,115,140 }, 0, 0, h, 5);
            rect({ 140,155,185 }, 1, 6, h - 2, h / 2 - 4);
            rect({ 175,185,200 }, 1, h / 2 + 2, h - 2, h / 2 - 4);
            for (int vi = 0; vi < 2; vi++) for (int vj = 0; vj < 3; vj++)
                rect({ 190,215,240 }, 3 + vj * h * 0.2f, h / 2 + 5 + vi * h * 0.14f, h * 0.16f, h * 0.1f);
            break;
        case BLD_HIGHRISE:
            rectFull({ 140,155,175 }); rect({ 90,105,130 }, 0, 0, h, 4);
            for (int ri = 0; ri < 5; ri++) for (int ci = 0; ci < 3; ci++)
                rect({ 170,200,235,200 }, 2 + ci * h * 0.22f, 5 + ri * h * 0.12f, h * 0.18f, h * 0.08f);
            break;
        case BLD_CAFE:
            rectFull({ 210,190,165 }); rect({ 80,55,35 }, 0, 0, h, 5);
            rect({ 60,140,60 }, 0, h / 2 - 2, h, 8);
            rect({ 180,220,250,200 }, 2, h / 2 + 7, h - 4, h / 2 - 9);
            circ({ 160,120,80 }, h / 2, h * 0.35f, 4); break;
        case BLD_CONVENIENCE:
            rectFull({ 240,240,245 }); rect({ 30,100,200 }, 0, 0, h, 6);
            rect({ 30,100,200 }, 0, h / 2 - 1, h, 10);
            rect({ 200,230,255,220 }, 1, h / 2 + 10, h - 2, h / 2 - 12); break;
        case BLD_RESTAURANT:
            rectFull({ 220,200,175 }); rect({ 190,80,50 }, 0, 0, h, 5);
            rect({ 230,120,50 }, 2, h / 2, h - 4, 8);
            rect({ 180,215,245,180 }, 2, h / 2 + 9, h - 4, h / 2 - 11); break;
        case BLD_MART:
            rectFull({ 228,222,210 }); rect({ 145,70,55 }, 0, 0, h, 5);
            rect({ 210,40,40 }, 1, 6, h - 2, 6);
            rect({ 185,215,240,220 }, 2, 14, h - 4, 8);
            rect({ 120,160,190 }, h / 2 - 4, h - 10, 8, 10); break;
        case BLD_HOTEL:
            rectFull({ 200,185,160 }); rect({ 145,120,85 }, 0, 0, h, 5);
            for (int hi2 = 0; hi2 < 4; hi2++) rect({ 200,225,255 }, 3 + hi2 * h * 0.16f, 7, 5, 5);
            for (int hi2 = 0; hi2 < 4; hi2++) rect({ 200,225,255 }, 3 + hi2 * h * 0.16f, 15, 5, 5);
            rect({ 120,95,65 }, h / 2 - 4, h - 11, 8, 11); break;
        case BLD_HOSPITAL:
            rectFull({ 245,245,248 }); rect({ 180,180,185 }, 0, 0, h, 3);
            rect({ 220,40,40 }, h / 2 - 2, h / 4, 4, h / 2);
            rect({ 220,40,40 }, h / 4, h / 2 - 2, h / 2, 4);
            for (int hi2 = 0; hi2 < 2; hi2++) for (int hj = 0; hj < 2; hj++)
                rect({ 200,225,255 }, 2 + hj * h * 0.32f, h / 2 + 4 + hi2 * h * 0.14f, h * 0.22f, h * 0.1f);
            break;
        case BLD_SCHOOL:
            rectFull({ 220,215,190 }); rect({ 140,160,100 }, 0, 0, h, 4);
            for (int si = 0; si < 3; si++) rect({ 200,225,255 }, 2 + si * h * 0.22f, 6, h * 0.18f, h * 0.16f);
            rect({ 100,80,50 }, h / 2 - 4, h - 11, 8, 11); break;
        case BLD_POLICE:
            rectFull({ 60,90,180 }); rect({ 40,65,145 }, 0, 0, h, 4);
            for (int pi = 0; pi < 2; pi++) rect({ 210,230,255 }, 3 + pi * h * 0.3f, 6, h * 0.2f, h * 0.18f);
            rect({ 230,230,235 }, h / 2 - 4, h - 10, 8, 10); break;
        case BLD_FIRESTATION:
            rectFull({ 200,60,50 }); rect({ 160,40,35 }, 0, 0, h, 4);
            for (int fi = 0; fi < 2; fi++) rect({ 180,180,185 }, 2 + fi * h * 0.32f, h / 2 + 1, h * 0.25f, h / 2 - 3);
            break;
        case BLD_GASSTATION:
            rectFull({ 250,240,210 }); rect({ 200,80,30 }, 0, 0, h, 5);
            rect({ 180,180,185 }, 0, h / 3, h, 4);
            rect({ 80,80,90 }, 3, h / 2 + 2, 5, h / 2 - 4);
            rect({ 80,80,90 }, h - 8, h / 2 + 2, 5, h / 2 - 4);
            rect({ 255,50,50 }, 4, h / 2 + 3, 3, h / 2 - 6); break;
        case BLD_STADIUM:
            rectFull({ 160,145,125 }); rect({ 120,105,85 }, 0, 0, h, 4);
            for (int si = 0; si < 3; si++) rect({ 190,170,145 }, 1, 5 + si * h * 0.18f, h - 2, 5);
            break;
        case RD_ROUNDABOUT:
            rectFull({ 68,68,72 }); break;
        case RD_DIAG:
            rectFull({ 85,82,75 }); for (int di = 0; di < 6; di++) rect({ 95,92,85 }, di * 5.f, di * 5.f, 3, 3);
            break;
        case RD_MERGE:
            rectFull({ 68,68,72 }); rect({ 255,165,0,180 }, 0, h / 2 - 1, h, 2); break;
        case SW_FLOWER:
            rectFull({ 188,180,168 });
            for (int fi = 0; fi < 4; fi++) circ({ 220,80,80 }, 5 + fi * h * 0.16f, h * 0.6f, 3);
            for (int fi = 0; fi < 3; fi++) circ({ 80,175,80 }, 9 + fi * h * 0.18f, h * 0.4f, 3);
            break;
        case SW_PERSON:
            rectFull({ 188,180,168 });
            rect({ 168,160,148 }, 0, 0, h, 1); rect({ 168,160,148 }, 0, h / 2, h, 1);
            circ({ 220,198,172 }, h / 2, h / 2 - 6, 3.0f);
            rect({ 88,96,180 }, h / 2 - 2.5f, h / 2 - 2, 5, 8);
            rect({ 60,60,68 }, h / 2 - 3, h / 2 + 5, 1.7f, 4);
            rect({ 60,60,68 }, h / 2 + 0.5f, h / 2 + 5, 1.5f, 4);
            break;
        case SW_DOG:
            rectFull({ 188,180,168 });
            rect({ 168,160,148 }, 0, 0, h, 1); rect({ 168,160,148 }, 0, h / 2, h, 1);
            rect({ 132,102,72 }, h / 2 - 5.5f, h / 2 + 1, 10, 4);
            circ({ 132,102,72 }, h / 2 + 5.5f, h / 2 + 1, 2.5f);
            rect({ 92,72,50 }, h / 2 + 5, h / 2 - 3, 1.5f, 2.5f);
            rect({ 92,72,50 }, h / 2 - 4, h / 2 + 4, 1.2f, 3);
            rect({ 92,72,50 }, h / 2 + 2, h / 2 + 4, 1.2f, 3);
            rect({ 92,72,50 }, h / 2 - 8, h / 2, 2.5f, 1.2f);
            break;
            // -- Parking tiles --
        case PK_ZONE:        rectFull({ 70, 70, 77 }); break;
        case PK_STRIPE:      rectFull({ 76, 76, 83 }); break;
        case PK_LINE_V:      rectFull({ 76, 76, 83 }); rect({ 235,235,240 }, h / 2 - 2, 0, 4, h); break;
        case PK_LINE_H:      rectFull({ 76, 76, 83 }); rect({ 235,235,240 }, 0, h / 2 - 2, h, 4); break;
        case PK_EMPTY: {
            // ?? ???? ???? ???
            rectFull({ 70, 70, 77 });
            break;
        }
        case PK_CENTER:      rectFull({ 62, 62, 68 }); rect({ 235,205,45 }, 0, h / 2 - 4, h, 3); rect({ 235,205,45 }, 0, h / 2 + 2, h, 3); break;
        case PK_DASH:        rectFull({ 62, 62, 68 }); rect({ 225,225,230 }, 1, h / 2 - 2, h - 2, 4); break;
        case PK_CURB_CORNER: rectFull({ 132,125,112 }); circ({ 125,118,105 }, h / 2, h / 2, h / 2 - 3); break;
        case CAR_Y:          rectFull({ 235,196, 49 }); break;
        case CAR_W:          rectFull({ 245,245,245 }); break;
        case CAR_R:          rectFull({ 200, 75, 50 }); break;
        case CAR_BL:         rectFull({ 65,105,200 }); break;
        case CAR_G:          rectFull({ 60,170, 85 }); break;
        case CAR_GY:         rectFull({ 90, 90,100 }); break;
        case CAR_P:          rectFull({ 130, 60,180 }); break;
        case CAR_BK:         rectFull({ 30, 30, 35 }); break;
        case CAR_GLASS:      rectFull({ 115,175,220 }); break;
        default:          rectFull({ 68,68,72 }); break;
        }
    }

    void Renderer::drawMapCells(const MapSystem& map) {
        if (map.hasTileGrid()) {
            const float h = pixelsPerCell_;

            for (int y = 0; y < map.getHeight(); ++y) {
                for (int x = 0; x < map.getWidth(); ++x) {
                    TileID id = map.getTileId(x, y);
                    float px = (float)x * h;
                    float py = (float)y * h;

                    // Horizontal dashes: position the dash line next to the
                    // adjacent center-line tile so they visually align.
                    if (id == RD_DASH_H) {
                        sf::RectangleShape bg({ h, h });
                        bg.setFillColor({ 68,68,72 });
                        bg.setPosition({ px, py });
                        window_.draw(bg);

                        float lineY = h * 0.5f;
                        if (y > 0 && map.getTileId(x, y - 1) == RD_LANE_H)
                            lineY = h * 0.82f;
                        else if (y + 1 < map.getHeight() && map.getTileId(x, y + 1) == RD_LANE_H)
                            lineY = h * 0.18f;

                        const int dashCount = 6;
                        const float sideMargin = 4.f;
                        const float gap = 4.f;
                        const float dashW = (h - sideMargin * 2.f - gap * (dashCount - 1)) / dashCount;

                        for (int i = 0; i < dashCount; ++i) {
                            float xPos = sideMargin + i * (dashW + gap);
                            sf::RectangleShape dash({ dashW, 2.f });
                            dash.setFillColor({ 230,230,235 });
                            dash.setPosition({ px + xPos, py + lineY - 1.f });
                            window_.draw(dash);
                        }
                        continue;
                    }

                    // Vertical dashes: same logic, rotated.
                    if (id == RD_DASH_V) {
                        sf::RectangleShape bg({ h, h });
                        bg.setFillColor({ 68,68,72 });
                        bg.setPosition({ px, py });
                        window_.draw(bg);

                        float lineX = h * 0.5f;
                        if (x > 0 && map.getTileId(x - 1, y) == RD_LANE_V)
                            lineX = h * 0.82f;
                        else if (x + 1 < map.getWidth() && map.getTileId(x + 1, y) == RD_LANE_V)
                            lineX = h * 0.18f;

                        const int dashCount = 6;
                        const float sideMargin = 4.f;
                        const float gap = 4.f;
                        const float dashH = (h - sideMargin * 2.f - gap * (dashCount - 1)) / dashCount;

                        for (int i = 0; i < dashCount; ++i) {
                            float yPos = sideMargin + i * (dashH + gap);
                            sf::RectangleShape dash({ 2.f, dashH });
                            dash.setFillColor({ 230,230,235 });
                            dash.setPosition({ px + lineX - 1.f, py + yPos });
                            window_.draw(dash);
                        }
                        continue;
                    }

                    drawTile(id, px, py);
                }
            }
        }
        else {
            drawMapCellsLegacy(map);
        }
    }

    void Renderer::drawTile(TileID id, float x, float y) {
        const float h = pixelsPerCell_;
        auto rectFull = [&](sf::Color c) {
            sf::RectangleShape s({ h, h });
            s.setFillColor(c);
            s.setPosition({ x, y });
            window_.draw(s);
            };
        auto rect = [&](sf::Color c, float ox, float oy, float w, float ht) {
            sf::RectangleShape s({ w, ht });
            s.setFillColor(c);
            s.setPosition({ x + ox, y + oy });
            window_.draw(s);
            };
        auto circ = [&](sf::Color c, float cx, float cy, float r) {
            sf::CircleShape s(r);
            s.setFillColor(c);
            s.setOrigin({ r, r });
            s.setPosition({ x + cx, y + cy });
            window_.draw(s);
            };
        switch (id) {
        case RD_PLAIN:    rectFull({ 68,68,72 }); break;
        case RD_LANE_H:   rectFull({ 68,68,72 }); rect({ 230,195,45 }, 0, h / 2 - 2, h, 4); rect({ 230,195,45 }, 0, h / 2 + 2, h, 2); break;
        case RD_LANE_V:   rectFull({ 68,68,72 }); rect({ 230,195,45 }, h / 2 - 2, 0, 4, h); rect({ 230,195,45 }, h / 2 + 2, 0, 2, h); break;
        case RD_DASH_H:   rectFull({ 68,68,72 }); for (int i = 0; i < 6; i++) rect({ 230,230,235 }, i * h / 6.f, h / 2 - 1, h / 6 - 2.f, 2.f); break;
        case RD_DASH_V:   rectFull({ 68,68,72 }); for (int i = 0; i < 6; i++) rect({ 230,230,235 }, h / 2 - 1, i * h / 6.f, 2.f, h / 6 - 2.f); break;
        case RD_CROSS:    rectFull({ 68,68,72 }); break;
        case RD_CWALK_H:  rectFull({ 80,80,85 }); for (int i = 0; i < 4; i++) rect({ 240,240,245 }, 2, i * h / 4.f + 2, h - 4, h / 4 - 4.f); break;
        case RD_CWALK_V:  rectFull({ 80,80,85 }); for (int i = 0; i < 4; i++) rect({ 240,240,245 }, i * h / 4.f + 2, 2, h / 4 - 4.f, h - 4); break;
        case RD_SHOULDER: rectFull({ 58,60,65 }); rect({ 78,80,85 }, 0, 0, h, 2); break;
        case SW_PLAIN:    rectFull({ 185,178,165 }); rect({ 165,158,145 }, 0, 0, h, 1); rect({ 165,158,145 }, 0, h / 2, h, 1); rect({ 165,158,145 }, 0, 0, 1, h); rect({ 165,158,145 }, h / 2, 0, 1, h); break;
        case SW_CURB:     rectFull({ 140,135,125 }); rect({ 160,155,145 }, 1, 1, h - 2, h - 2); break;
        case SW_LAMP:
            rectFull({ 185,178,165 }); rect({ 165,158,145 }, 0, 0, h, 1); rect({ 165,158,145 }, 0, h / 2, h, 1);
            rect({ 165,158,145 }, 0, 0, 1, h); rect({ 165,158,145 }, h / 2, 0, 1, h);
            rect({ 55,55,60 }, h / 2 - 1, h / 3, 2, h * 2 / 3); rect({ 245,225,90 }, h / 2 - 5, h / 3 - 5, 10, 6);
            circ({ 255,248,180,160 }, h / 2, h / 3 - 2, 6); break;
        case SW_BENCH:    rectFull({ 185,178,165 }); rect({ 140,100,60 }, 3, h * 2 / 3, h - 6, 5); rect({ 110,80,50 }, 4, h / 2 + 2, 4, h / 3); rect({ 110,80,50 }, h - 8, h / 2 + 2, 4, h / 3); break;
        case SW_TREE:     rectFull({ 185,178,165 }); rect({ 120,85,45 }, h / 2 - 2, h / 2, 4, h / 2 - 2); circ({ 60,150,55 }, h / 2 + 3, h / 2 - 2, 9); circ({ 72,168,62 }, h / 2 - 3, h / 2 - 5, 11); break;
        case BLD_APART:
            rectFull({ 155,165,185 }); rect({ 100,115,145 }, 0, 0, h, h * 0.15f);
            for (int wy = 0; wy < 3; wy++) for (int wx = 0; wx < 3; wx++) rect({ 190,215,240 }, 3 + wx * h * 0.22f, h * 0.22f + wy * h * 0.2f, h * 0.16f, h * 0.12f);
            break;
        case BLD_HOUSE:
            rectFull({ 200,175,140 }); rect({ 165,75,55 }, 1, 1, h - 2, h / 2 - 2);
            rect({ 175,145,105 }, 1, h / 2, h - 2, h / 2 - 1);
            rect({ 195,225,255 }, 3, h / 2 + 3, h * 0.18f, h * 0.16f);
            rect({ 195,225,255 }, h - h * 0.24f, h / 2 + 3, h * 0.18f, h * 0.16f);
            rect({ 110,75,45 }, h / 2 - 3, h - h * 0.22f, 6, h * 0.22f);
            break;
        case BLD_SHOP:
            rectFull({ 215,205,190 }); rect({ 130,155,200 }, 1, 1, h - 2, h / 2 - 2);
            rect({ 240,70,70 }, 2, h / 2, h - 4, h * 0.16f);
            rect({ 175,215,250,190 }, 2, h / 2 + h * 0.18f, h - 4, h / 2 - h * 0.22f);
            break;
        case BLD_OFFICE:
            rectFull({ 180,190,200 }); rect({ 120,135,155 }, 0, 0, h, h * 0.1f);
            for (int wy = 0; wy < 4; wy++) for (int wx = 0; wx < 3; wx++) rect({ 160,195,230,180 }, 2 + wx * h * 0.24f, h * 0.12f + wy * h * 0.16f, h * 0.2f, h * 0.12f);
            break;
        case BLD_PARK:
            rectFull({ 100,175,80 }); circ({ 80,155,220 }, h / 2, h / 2, h * 0.22f); circ({ 100,175,80 }, h / 2, h / 2, h * 0.12f);
            {
                sf::RectangleShape bo({ h,h }); bo.setPosition({ x,y }); bo.setFillColor(sf::Color::Transparent);
                bo.setOutlineColor({ 80,150,60 }); bo.setOutlineThickness(2.f); window_.draw(bo);
            }
            break;
        case BLD_PARKING:
            rectFull({ 85,85,92 }); rect({ 210,210,215 }, 0, 2, 2, h - 4); rect({ 210,210,215 }, h - 2, 2, 2, h - 4);
            rect({ 210,210,215 }, 0, 2, h, 2); rect({ 210,210,215 }, 0, h / 2, h, 1);
            rect({ 55,90,210 }, h / 2 - 5, h / 2 - h * 0.16f, 10, h * 0.3f);
            break;
        case BLD_ON_PARK_V:
            rectFull({ 75,75,80 }); rect({ 220,220,225 }, 1, 0, 2, h); rect({ 220,220,225 }, h - 3, 0, 2, h);
            break;
        case BLD_ON_PARK_H:
            rectFull({ 75,75,80 }); rect({ 220,220,225 }, 0, 1, h, 2); rect({ 220,220,225 }, 0, h - 3, h, 2);
            break;
        case NAT_GRASS:
            rectFull({ 95,170,75 }); for (int gi = 0; gi < 5; gi++) rect({ 75,150,58 }, gi * h * 0.14f + 2, (float)((gi * 7) % (int)(h - 4)), 3, 4);
            break;
        case NAT_TREE:
            rectFull({ 95,170,75 }); rect({ 130,90,50 }, h / 2 - 2, h / 2 + 2, 4, h / 2 - 2);
            circ({ 50,130,50 }, h / 2 + 4, h / 2 - 2, h * 0.2f); circ({ 60,150,60 }, h / 2 - 4, h / 2 - 4, h * 0.24f);
            circ({ 72,165,68 }, h / 2 + 2, h / 2 - h * 0.18f, h * 0.22f);
            break;
        case NAT_GARDEN:
            rectFull({ 100,175,80 }); for (int gi = 0; gi < 4; gi++) circ({ 220,80,80 }, 5 + gi * h * 0.16f, h * 0.52f, h * 0.09f);
            for (int gi = 0; gi < 3; gi++) circ({ 80,180,80 }, 9 + gi * h * 0.18f, h * 0.38f, h * 0.07f);
            break;
            // -- Course B new tiles --
        case NAT_POND:
            rectFull({ 98,172,78 }); circ({ 80,148,210 }, h / 2, h / 2, h * 0.22f);
            circ({ 100,165,225 }, h / 2, h / 2, h * 0.14f); break;
        case BLD_VILLA:
            rectFull({ 175,185,200 }); rect({ 105,115,140 }, 0, 0, h, 5);
            rect({ 140,155,185 }, 1, 6, h - 2, h / 2 - 4);
            rect({ 175,185,200 }, 1, h / 2 + 2, h - 2, h / 2 - 4);
            for (int vi = 0; vi < 2; vi++) for (int vj = 0; vj < 3; vj++)
                rect({ 190,215,240 }, 3 + vj * h * 0.2f, h / 2 + 5 + vi * h * 0.14f, h * 0.16f, h * 0.1f);
            break;
        case BLD_HIGHRISE:
            rectFull({ 140,155,175 }); rect({ 90,105,130 }, 0, 0, h, 4);
            for (int ri = 0; ri < 5; ri++) for (int ci = 0; ci < 3; ci++)
                rect({ 170,200,235,200 }, 2 + ci * h * 0.22f, 5 + ri * h * 0.12f, h * 0.18f, h * 0.08f);
            break;
        case BLD_CAFE:
            rectFull({ 210,190,165 }); rect({ 80,55,35 }, 0, 0, h, 5);
            rect({ 60,140,60 }, 0, h / 2 - 2, h, 8);
            rect({ 180,220,250,200 }, 2, h / 2 + 7, h - 4, h / 2 - 9);
            circ({ 160,120,80 }, h / 2, h * 0.35f, 4); break;
        case BLD_CONVENIENCE:
            rectFull({ 240,240,245 }); rect({ 30,100,200 }, 0, 0, h, 6);
            rect({ 30,100,200 }, 0, h / 2 - 1, h, 10);
            rect({ 200,230,255,220 }, 1, h / 2 + 10, h - 2, h / 2 - 12); break;
        case BLD_RESTAURANT:
            rectFull({ 220,200,175 }); rect({ 190,80,50 }, 0, 0, h, 5);
            rect({ 230,120,50 }, 2, h / 2, h - 4, 8);
            rect({ 180,215,245,180 }, 2, h / 2 + 9, h - 4, h / 2 - 11); break;
        case BLD_MART:
            rectFull({ 228,222,210 }); rect({ 145,70,55 }, 0, 0, h, 5);
            rect({ 210,40,40 }, 1, 6, h - 2, 6);
            rect({ 185,215,240,220 }, 2, 14, h - 4, 8);
            rect({ 120,160,190 }, h / 2 - 4, h - 10, 8, 10); break;
        case BLD_HOTEL:
            rectFull({ 200,185,160 }); rect({ 145,120,85 }, 0, 0, h, 5);
            for (int hi2 = 0; hi2 < 4; hi2++) rect({ 200,225,255 }, 3 + hi2 * h * 0.16f, 7, 5, 5);
            for (int hi2 = 0; hi2 < 4; hi2++) rect({ 200,225,255 }, 3 + hi2 * h * 0.16f, 15, 5, 5);
            rect({ 120,95,65 }, h / 2 - 4, h - 11, 8, 11); break;
        case BLD_HOSPITAL:
            rectFull({ 245,245,248 }); rect({ 180,180,185 }, 0, 0, h, 3);
            rect({ 220,40,40 }, h / 2 - 2, h / 4, 4, h / 2);
            rect({ 220,40,40 }, h / 4, h / 2 - 2, h / 2, 4);
            for (int hi2 = 0; hi2 < 2; hi2++) for (int hj = 0; hj < 2; hj++)
                rect({ 200,225,255 }, 2 + hj * h * 0.32f, h / 2 + 4 + hi2 * h * 0.14f, h * 0.22f, h * 0.1f);
            break;
        case BLD_SCHOOL:
            rectFull({ 220,215,190 }); rect({ 140,160,100 }, 0, 0, h, 4);
            for (int si = 0; si < 3; si++) rect({ 200,225,255 }, 2 + si * h * 0.22f, 6, h * 0.18f, h * 0.16f);
            rect({ 100,80,50 }, h / 2 - 4, h - 11, 8, 11); break;
        case BLD_POLICE:
            rectFull({ 60,90,180 }); rect({ 40,65,145 }, 0, 0, h, 4);
            for (int pi = 0; pi < 2; pi++) rect({ 210,230,255 }, 3 + pi * h * 0.3f, 6, h * 0.2f, h * 0.18f);
            rect({ 230,230,235 }, h / 2 - 4, h - 10, 8, 10); break;
        case BLD_FIRESTATION:
            rectFull({ 200,60,50 }); rect({ 160,40,35 }, 0, 0, h, 4);
            for (int fi = 0; fi < 2; fi++) rect({ 180,180,185 }, 2 + fi * h * 0.32f, h / 2 + 1, h * 0.25f, h / 2 - 3);
            break;
        case BLD_GASSTATION:
            rectFull({ 250,240,210 }); rect({ 200,80,30 }, 0, 0, h, 5);
            rect({ 180,180,185 }, 0, h / 3, h, 4);
            rect({ 80,80,90 }, 3, h / 2 + 2, 5, h / 2 - 4);
            rect({ 80,80,90 }, h - 8, h / 2 + 2, 5, h / 2 - 4);
            rect({ 255,50,50 }, 4, h / 2 + 3, 3, h / 2 - 6); break;
        case BLD_STADIUM:
            rectFull({ 160,145,125 }); rect({ 120,105,85 }, 0, 0, h, 4);
            for (int si = 0; si < 3; si++) rect({ 190,170,145 }, 1, 5 + si * h * 0.18f, h - 2, 5);
            break;
        case RD_ROUNDABOUT:
            rectFull({ 68,68,72 }); break;
        case RD_DIAG:
            rectFull({ 85,82,75 }); for (int di = 0; di < 6; di++) rect({ 95,92,85 }, di * 5.f, di * 5.f, 3, 3);
            break;
        case RD_MERGE:
            rectFull({ 68,68,72 }); rect({ 255,165,0,180 }, 0, h / 2 - 1, h, 2); break;
        case SW_FLOWER:
            rectFull({ 188,180,168 });
            for (int fi = 0; fi < 4; fi++) circ({ 220,80,80 }, 5 + fi * h * 0.16f, h * 0.6f, 3);
            for (int fi = 0; fi < 3; fi++) circ({ 80,175,80 }, 9 + fi * h * 0.18f, h * 0.4f, 3);
            break;
        case SW_PERSON:
            rectFull({ 188,180,168 });
            rect({ 168,160,148 }, 0, 0, h, 1); rect({ 168,160,148 }, 0, h / 2, h, 1);
            circ({ 220,198,172 }, h / 2, h / 2 - 6, 3.0f);
            rect({ 88,96,180 }, h / 2 - 2.5f, h / 2 - 2, 5, 8);
            rect({ 60,60,68 }, h / 2 - 3, h / 2 + 5, 1.7f, 4);
            rect({ 60,60,68 }, h / 2 + 0.5f, h / 2 + 5, 1.5f, 4);
            break;
        case SW_DOG:
            rectFull({ 188,180,168 });
            rect({ 168,160,148 }, 0, 0, h, 1); rect({ 168,160,148 }, 0, h / 2, h, 1);
            rect({ 132,102,72 }, h / 2 - 5.5f, h / 2 + 1, 10, 4);
            circ({ 132,102,72 }, h / 2 + 5.5f, h / 2 + 1, 2.5f);
            rect({ 92,72,50 }, h / 2 + 5, h / 2 - 3, 1.5f, 2.5f);
            rect({ 92,72,50 }, h / 2 - 4, h / 2 + 4, 1.2f, 3);
            rect({ 92,72,50 }, h / 2 + 2, h / 2 + 4, 1.2f, 3);
            rect({ 92,72,50 }, h / 2 - 8, h / 2, 2.5f, 1.2f);
            break;
            // -- Parking tiles --
        case PK_ZONE:        rectFull({ 70, 70, 77 }); break;
        case PK_STRIPE:      rectFull({ 76, 76, 83 }); break;
        case PK_LINE_V:      rectFull({ 76, 76, 83 }); rect({ 235,235,240 }, h / 2 - 2, 0, 4, h); break;
        case PK_LINE_H:      rectFull({ 76, 76, 83 }); rect({ 235,235,240 }, 0, h / 2 - 2, h, 4); break;
        case PK_EMPTY: {
            // ?? ???? ???? ???
            rectFull({ 70, 70, 77 });
            break;
        }
        case PK_CENTER:      rectFull({ 62, 62, 68 }); rect({ 235,205,45 }, 0, h / 2 - 4, h, 3); rect({ 235,205,45 }, 0, h / 2 + 2, h, 3); break;
        case PK_DASH:        rectFull({ 62, 62, 68 }); rect({ 225,225,230 }, 1, h / 2 - 2, h - 2, 4); break;
        case PK_CURB_CORNER: rectFull({ 132,125,112 }); circ({ 125,118,105 }, h / 2, h / 2, h / 2 - 3); break;
        case CAR_Y:          rectFull({ 235,196, 49 }); break;
        case CAR_W:          rectFull({ 245,245,245 }); break;
        case CAR_R:          rectFull({ 200, 75, 50 }); break;
        case CAR_BL:         rectFull({ 65,105,200 }); break;
        case CAR_G:          rectFull({ 60,170, 85 }); break;
        case CAR_GY:         rectFull({ 90, 90,100 }); break;
        case CAR_P:          rectFull({ 130, 60,180 }); break;
        case CAR_BK:         rectFull({ 30, 30, 35 }); break;
        case CAR_GLASS:      rectFull({ 115,175,220 }); break;
        default:          rectFull({ 68,68,72 }); break;
        }
    }

    void Renderer::drawNpcCars(const MapSystem& map) {
        const auto& cars = map.getNpcCars();
        for (size_t ci = 0; ci < cars.size(); ++ci) {
            const auto& car = cars[ci];
            // Skip cars waiting to respawn (invisible during delay)
            if (car.waiting) continue;
            sf::Vector2f pos = worldToPixels(car.position);

            const float carLength = 2.8f * pixelsPerCell_;
            const float carWidth = 1.5f * pixelsPerCell_;

            // --------------------------------------------------------
            // Roundabout cars: free-rotation rendering
            // --------------------------------------------------------
            if (car.inRoundabout) {
                const float h = car.heading;
                const float degH = radToDeg(h);
                float fdx = std::cos(h), fdy = std::sin(h);
                float sx = -fdy, sy = fdx;

                // Shadow
                sf::RectangleShape shadow({ carLength, carWidth });
                shadow.setOrigin({ carLength / 2.f, carWidth / 2.f });
                shadow.setPosition({ pos.x + 3.f, pos.y + 3.f });
                shadow.setRotation(sf::degrees(degH));
                shadow.setFillColor(sf::Color(0, 0, 0, 35));
                window_.draw(shadow);

                // Body
                sf::RectangleShape body({ carLength, carWidth });
                body.setOrigin({ carLength / 2.f, carWidth / 2.f });
                body.setPosition(pos);
                body.setRotation(sf::degrees(degH));
                body.setFillColor(sf::Color((uint8_t)car.cr, (uint8_t)car.cg, (uint8_t)car.cb));
                body.setOutlineThickness(1.8f);
                body.setOutlineColor(sf::Color(
                    (uint8_t)std::max(0, car.cr - 55),
                    (uint8_t)std::max(0, car.cg - 55),
                    (uint8_t)std::max(0, car.cb - 55)));
                window_.draw(body);

                // Front windshield
                {
                    const float fgW = carLength * 0.22f, fgH = carWidth * 0.68f;
                    sf::RectangleShape fg({ fgW, fgH });
                    fg.setOrigin({ fgW / 2.f, fgH / 2.f });
                    fg.setPosition({ pos.x + fdx * carLength * 0.22f, pos.y + fdy * carLength * 0.22f });
                    fg.setRotation(sf::degrees(degH));
                    fg.setFillColor(sf::Color(135, 206, 235, 200));
                    window_.draw(fg);
                }
                // Rear windshield
                {
                    const float rgW = carLength * 0.18f, rgH = carWidth * 0.58f;
                    sf::RectangleShape rg({ rgW, rgH });
                    rg.setOrigin({ rgW / 2.f, rgH / 2.f });
                    rg.setPosition({ pos.x - fdx * carLength * 0.22f, pos.y - fdy * carLength * 0.22f });
                    rg.setRotation(sf::degrees(degH));
                    rg.setFillColor(sf::Color(85, 130, 160, 180));
                    window_.draw(rg);
                }
                // Headlights
                {
                    const float lw = carLength * 0.045f, lh = carWidth * 0.25f;
                    sf::RectangleShape hl({ lw, lh });
                    hl.setOrigin({ lw / 2.f, lh / 2.f });
                    hl.setFillColor(sf::Color(255, 245, 192, 210));
                    hl.setRotation(sf::degrees(degH));
                    hl.setPosition({ pos.x + fdx * carLength * 0.46f + sx * carWidth * 0.24f,
                                     pos.y + fdy * carLength * 0.46f + sy * carWidth * 0.24f });
                    window_.draw(hl);
                    hl.setPosition({ pos.x + fdx * carLength * 0.46f - sx * carWidth * 0.24f,
                                     pos.y + fdy * carLength * 0.46f - sy * carWidth * 0.24f });
                    window_.draw(hl);
                }
                // Taillights
                {
                    const float lw = carLength * 0.045f, lh = carWidth * 0.22f;
                    sf::RectangleShape tl({ lw, lh });
                    tl.setOrigin({ lw / 2.f, lh / 2.f });
                    tl.setFillColor(sf::Color(215, 45, 45, 210));
                    tl.setRotation(sf::degrees(degH));
                    tl.setPosition({ pos.x - fdx * carLength * 0.46f + sx * carWidth * 0.24f,
                                     pos.y - fdy * carLength * 0.46f + sy * carWidth * 0.24f });
                    window_.draw(tl);
                    tl.setPosition({ pos.x - fdx * carLength * 0.46f - sx * carWidth * 0.24f,
                                     pos.y - fdy * carLength * 0.46f - sy * carWidth * 0.24f });
                    window_.draw(tl);
                }
                continue;
            }

            // --------------------------------------------------------
            // Normal cars: axis-aligned (original code)
            // --------------------------------------------------------
            const bool horiz = (car.dir == NPC_RIGHT || car.dir == NPC_LEFT);
            const float bw = horiz ? carLength : carWidth;
            const float bh = horiz ? carWidth : carLength;

            // Shadow
            sf::RectangleShape shadow({ bw, bh });
            shadow.setOrigin({ bw / 2.f, bh / 2.f });
            shadow.setPosition({ pos.x + 3.f, pos.y + 3.f });
            shadow.setFillColor(sf::Color(0, 0, 0, 35));
            window_.draw(shadow);

            // Body with outline
            sf::RectangleShape body({ bw, bh });
            body.setOrigin({ bw / 2.f, bh / 2.f });
            body.setPosition(pos);
            body.setFillColor(sf::Color((uint8_t)car.cr, (uint8_t)car.cg, (uint8_t)car.cb));
            body.setOutlineThickness(1.8f);
            body.setOutlineColor(sf::Color(
                (uint8_t)std::max(0, car.cr - 55),
                (uint8_t)std::max(0, car.cg - 55),
                (uint8_t)std::max(0, car.cb - 55)));
            window_.draw(body);

            // Front windshield direction
            float fdx = 0, fdy = 0;
            if (car.dir == NPC_RIGHT) fdx = 1.f;
            else if (car.dir == NPC_LEFT)  fdx = -1.f;
            else if (car.dir == NPC_DOWN)  fdy = 1.f;
            else                           fdy = -1.f;

            // Front windshield
            const float fgW = horiz ? bw * 0.22f : bw * 0.68f;
            const float fgH = horiz ? bh * 0.68f : bh * 0.22f;
            sf::RectangleShape fglass({ fgW, fgH });
            fglass.setOrigin({ fgW / 2.f, fgH / 2.f });
            fglass.setPosition({ pos.x + fdx * bw * 0.22f, pos.y + fdy * bh * 0.22f });
            fglass.setFillColor(sf::Color(135, 206, 235, 200));
            window_.draw(fglass);

            // Rear windshield (darker)
            const float rgW = horiz ? bw * 0.18f : bw * 0.58f;
            const float rgH = horiz ? bh * 0.58f : bh * 0.18f;
            sf::RectangleShape rglass({ rgW, rgH });
            rglass.setOrigin({ rgW / 2.f, rgH / 2.f });
            rglass.setPosition({ pos.x - fdx * bw * 0.22f, pos.y - fdy * bh * 0.22f });
            rglass.setFillColor(sf::Color(85, 130, 160, 180));
            window_.draw(rglass);

            // Headlights (front, yellow-white)
            {
                const float sx = -fdy, sy = fdx; // perpendicular
                const float lw = horiz ? bw * 0.045f : bw * 0.25f;
                const float lh = horiz ? bh * 0.25f : bh * 0.045f;
                sf::RectangleShape hl({ lw, lh });
                hl.setOrigin({ lw / 2.f, lh / 2.f });
                hl.setFillColor(sf::Color(255, 245, 192, 210));
                // Left headlight
                hl.setPosition({ pos.x + fdx * bw * 0.46f + sx * bw * 0.24f,
                                 pos.y + fdy * bh * 0.46f + sy * bh * 0.24f });
                window_.draw(hl);
                // Right headlight
                hl.setPosition({ pos.x + fdx * bw * 0.46f - sx * bw * 0.24f,
                                 pos.y + fdy * bh * 0.46f - sy * bh * 0.24f });
                window_.draw(hl);
            }

            // Taillights (rear, red)
            {
                const float sx = -fdy, sy = fdx;
                const float lw = horiz ? bw * 0.045f : bw * 0.22f;
                const float lh = horiz ? bh * 0.22f : bh * 0.045f;
                sf::RectangleShape tl({ lw, lh });
                tl.setOrigin({ lw / 2.f, lh / 2.f });
                tl.setFillColor(sf::Color(215, 45, 45, 210));
                tl.setPosition({ pos.x - fdx * bw * 0.46f + sx * bw * 0.24f,
                                 pos.y - fdy * bh * 0.46f + sy * bh * 0.24f });
                window_.draw(tl);
                tl.setPosition({ pos.x - fdx * bw * 0.46f - sx * bw * 0.24f,
                                 pos.y - fdy * bh * 0.46f - sy * bh * 0.24f });
                window_.draw(tl);
            }

            // Turn signals for moving cars
            if (car.speed > 0.0f && car.speed > 0.0f) {
                // ... existing blinker logic can be added later
            }
        }
    }

    void Renderer::drawCheckpoints(const std::vector<Checkpoint>& checkpoints) {
        for (size_t i = 0; i < checkpoints.size(); ++i) {
            sf::CircleShape marker(pixelsPerCell_ * 0.20f, 24);
            marker.setOrigin({ marker.getRadius(), marker.getRadius() });
            marker.setPosition(worldToPixels(checkpoints[i].position));
            marker.setFillColor(checkpoints[i].passed ? sf::Color(80, 200, 120, 170) : sf::Color(255, 204, 70, 210));
            marker.setOutlineThickness(2.0f);
            marker.setOutlineColor(checkpoints[i].passed ? sf::Color(200, 255, 220) : sf::Color(255, 240, 200));
            window_.draw(marker);
        }
    }

    void Renderer::drawTrail(const Trail& trail) {
        sf::CircleShape dot(pixelsPerCell_ * 0.08f, 10);
        dot.setOrigin({ dot.getRadius(), dot.getRadius() });
        dot.setFillColor(sf::Color(121, 189, 255, 170));
        for (size_t i = 0; i < trail.pts.size(); ++i) {
            dot.setPosition({ trail.pts[i].first * pixelsPerCell_, trail.pts[i].second * pixelsPerCell_ });
            window_.draw(dot);
        }
    }

    void Renderer::drawPenaltyMarkers(const std::vector<std::pair<Vec2, Penalty>>& penaltyMarkers) {
        sf::CircleShape marker(pixelsPerCell_ * 0.12f, 16);
        marker.setOrigin({ marker.getRadius(), marker.getRadius() });
        marker.setFillColor(sf::Color(230, 74, 74, 220));
        for (size_t i = 0; i < penaltyMarkers.size(); ++i) {
            marker.setPosition(worldToPixels(penaltyMarkers[i].first));
            window_.draw(marker);
        }
    }

    void Renderer::drawSignals(const std::vector<TrafficSignal>& signals) {
        // id%4 : 0,1 = ???? ????(???? ????) -> ????? ???? ???
        //         2,3 = ???? ????(???? ????) -> ????? ???? ???
        for (size_t i = 0; i < signals.size(); ++i) {
            const TrafficSignal& sig = signals[i];

            sf::Vector2f pos = worldToPixels(sig.position);
            const float cell = pixelsPerCell_;
            const float radius = cell * 0.22f;

            // id%4 ????: 2,3?? ???? ???? -> ???? ???
            const bool horizontal = ((sig.id % 4) == 2 || (sig.id % 4) == 3);

            // ???: ???? ?????? ??<????, ???? ?????? ??>????
            const float boxW = horizontal ? cell * 0.90f : cell * 0.72f;
            const float boxH = horizontal ? cell * 0.72f : cell * 0.90f;
            const float spacing = horizontal ? boxW * 0.30f : boxH * 0.30f;

            // ???? ??? ???
            sf::RectangleShape box({ boxW, boxH });
            box.setOrigin({ boxW * 0.5f, boxH * 0.5f });
            box.setPosition(pos);
            box.setFillColor(sf::Color(18, 18, 20, 235));
            box.setOutlineColor(sf::Color(55, 55, 60, 210));
            box.setOutlineThickness(1.5f);
            window_.draw(box);

            // ???? ?? ????
            sf::Color redCol = sf::Color(55, 10, 10, 200);
            sf::Color yelCol = sf::Color(50, 40, 5, 200);
            sf::Color grnCol = sf::Color(5, 45, 15, 200);
            switch (sig.state) {
            case TrafficLight::Red:
                redCol = sf::Color(255, 45, 45, 255); break;
            case TrafficLight::Yellow:
                yelCol = sf::Color(255, 205, 35, 255); break;
            case TrafficLight::Green:
            case TrafficLight::LeftArrow:
                grnCol = sf::Color(55, 230, 95, 255); break;
            default: break;
            }


            sf::Vector2f posR = pos, posY2 = pos, posG = pos;
            if (horizontal) {
                posR.x -= spacing;
                posG.x += spacing;
            }
            else {
                posR.y -= spacing;
                posG.y += spacing;
            }

            auto drawCircle = [&](sf::Vector2f p, sf::Color c) {
                sf::CircleShape cs(radius);
                cs.setOrigin({ radius, radius });
                cs.setPosition(p);
                cs.setFillColor(c);
                window_.draw(cs);
                };
            drawCircle(posR, redCol);
            drawCircle(posY2, yelCol);
            drawCircle(posG, grnCol);

            // ?????
            sf::Vector2f glowPos = pos;
            sf::Color glowCol;
            if (sig.state == TrafficLight::Red) {
                glowPos = posR;
                glowCol = sf::Color(255, 45, 45, 55);
            }
            else if (sig.state == TrafficLight::Yellow) {
                glowPos = posY2;
                glowCol = sf::Color(255, 205, 35, 55);
            }
            else {
                glowPos = posG;
                glowCol = sf::Color(55, 230, 95, 55);
            }
            sf::CircleShape glow(radius * 1.6f);
            glow.setOrigin({ radius * 1.6f, radius * 1.6f });
            glow.setPosition(glowPos);
            glow.setFillColor(glowCol);
            window_.draw(glow);
        }
    }

    void Renderer::drawTargetSlot(const ParkingSlot& slot) {
        const float ppc = pixelsPerCell_;
        sf::Vector2f center = worldToPixels(slot.position);
        center.x += ppc * 0.5f;

        const bool isParallel = std::fabs(std::sin(slot.angle)) < 0.5f;
        if (isParallel) {
            center.y += ppc * 0.5f;
        }

        float sw, sh, rotation;
        if (isParallel) {
            sw = ppc * 3.0f;
            sh = ppc * 1.5f;
            rotation = 0.0f;
        }
        else {
            sw = ppc * 3.0f;
            sh = ppc * 1.5f;
            rotation = 90.0f;
        }

        sf::Color slotColor = slot.isParked
            ? sf::Color(50, 255, 120, 255)
            : sf::Color(220, 40, 40, 230);

        sf::Color arrowColor = slot.isParked
            ? sf::Color(50, 255, 120, 230)
            : sf::Color(220, 40, 40, 230);

        // green glow when parked
        if (slot.isParked) {
            float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 3.0f);
            float glowR = std::max(sw, sh) * (0.55f + pulse * 0.08f);
            sf::CircleShape glow(glowR, 60);
            glow.setOrigin({ glowR, glowR });
            glow.setPosition(center);
            glow.setFillColor(sf::Color(50, 255, 120, (uint8_t)(55 + pulse * 70)));
            window_.draw(glow);
        }

        // slot outline border
        const float thick = ppc * 0.15f;
        sf::RectangleShape outline({ sw, sh });
        outline.setOrigin({ sw * 0.5f, sh * 0.5f });
        outline.setPosition(center);
        outline.setRotation(sf::degrees(rotation));
        outline.setFillColor(sf::Color::Transparent);
        outline.setOutlineThickness(thick);
        outline.setOutlineColor(slotColor);
        window_.draw(outline);

        // direction arrow (RectangleShape shaft + triangle head)
        const float angle = slot.angle;
        const float deg = angle * 180.0f / 3.14159265f;
        const float arrowLen = ppc * 1.5f;
        const float arrowHead = ppc * 0.4f;
        const float arrowW = ppc * 0.15f;

        float dx = std::cos(angle);
        float dy = std::sin(angle);

        // shaft
        sf::RectangleShape shaft({ arrowLen - arrowHead, arrowW });
        shaft.setOrigin({ 0.0f, arrowW * 0.5f });
        shaft.setPosition({
            center.x - dx * arrowLen * 0.5f,
            center.y - dy * arrowLen * 0.5f });
        shaft.setRotation(sf::degrees(deg));
        shaft.setFillColor(arrowColor);
        window_.draw(shaft);

        // arrowhead triangle using LOCAL coordinates + setPosition
        sf::ConvexShape tri(3);
        float hh = arrowHead * 0.7f;
        // local coords: tip at (arrowHead, 0), base at (0, +/-hh)
        tri.setPoint(0, { arrowHead, 0.0f });
        tri.setPoint(1, { 0.0f, -hh });
        tri.setPoint(2, { 0.0f,  hh });
        tri.setOrigin({ arrowHead * 0.5f, 0.0f });
        tri.setPosition({
            center.x + dx * (arrowLen * 0.5f - arrowHead * 0.5f),
            center.y + dy * (arrowLen * 0.5f - arrowHead * 0.5f) });
        tri.setRotation(sf::degrees(deg));
        tri.setFillColor(arrowColor);
        window_.draw(tri);
    }

    void Renderer::drawVehicle(const VehicleState& vehicle) {
        const float carLength = 2.8f * pixelsPerCell_;
        const float carWidth = 1.5f * pixelsPerCell_;
        const sf::Vector2f pos = worldToPixels(vehicle.position);
        const float deg = vehicle.heading * 180.0f / 3.14159265f;
        const float dx = std::cos(vehicle.heading);
        const float dy = std::sin(vehicle.heading);

        // Glow effect (pulsing outline)
        float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 4.0f);
        float glowR = std::max(carLength, carWidth) * (0.5f + pulse * 0.05f);
        sf::CircleShape glow(glowR, 48);
        glow.setOrigin({ glowR, glowR });
        glow.setPosition(pos);
        glow.setFillColor(sf::Color(255, 220, 50, (uint8_t)(35 + pulse * 30)));
        window_.draw(glow);

        // Shadow
        sf::RectangleShape shadow({ carLength, carWidth });
        shadow.setOrigin({ carLength * 0.5f, carWidth * 0.5f });
        shadow.setPosition({ pos.x + 3.f, pos.y + 3.f });
        shadow.setRotation(sf::degrees(deg));
        shadow.setFillColor(sf::Color(0, 0, 0, 40));
        window_.draw(shadow);

        // Body (yellow)
        sf::RectangleShape body({ carLength, carWidth });
        body.setOrigin({ carLength * 0.5f, carWidth * 0.5f });
        body.setPosition(pos);
        body.setRotation(sf::degrees(deg));
        body.setFillColor(sf::Color(255, 210, 0));
        body.setOutlineThickness(2.0f);
        body.setOutlineColor(vehicle.engineOn ? sf::Color(180, 145, 0) : sf::Color(120, 120, 120));
        window_.draw(body);

        // Front windshield
        sf::RectangleShape windshield({ carLength * 0.22f, carWidth * 0.68f });
        windshield.setOrigin({ windshield.getSize().x * 0.5f, windshield.getSize().y * 0.5f });
        windshield.setPosition({ pos.x + dx * carLength * 0.22f, pos.y + dy * carLength * 0.22f });
        windshield.setRotation(sf::degrees(deg));
        windshield.setFillColor(sf::Color(135, 206, 235, 200));
        window_.draw(windshield);

        // Rear windshield
        sf::RectangleShape rearGlass({ carLength * 0.18f, carWidth * 0.58f });
        rearGlass.setOrigin({ rearGlass.getSize().x * 0.5f, rearGlass.getSize().y * 0.5f });
        rearGlass.setPosition({ pos.x - dx * carLength * 0.22f, pos.y - dy * carLength * 0.22f });
        rearGlass.setRotation(sf::degrees(deg));
        rearGlass.setFillColor(sf::Color(85, 130, 160, 180));
        window_.draw(rearGlass);

        // Headlights
        {
            const float sx = -dy, sy = dx;
            const float lw = carLength * 0.045f, lh = carWidth * 0.25f;
            sf::RectangleShape hl({ lw, lh });
            hl.setOrigin({ lw / 2.f, lh / 2.f });
            hl.setRotation(sf::degrees(deg));
            hl.setFillColor(sf::Color(255, 245, 192, 210));
            hl.setPosition({ pos.x + dx * carLength * 0.46f + sx * carWidth * 0.24f,
                             pos.y + dy * carLength * 0.46f + sy * carWidth * 0.24f });
            window_.draw(hl);
            hl.setPosition({ pos.x + dx * carLength * 0.46f - sx * carWidth * 0.24f,
                             pos.y + dy * carLength * 0.46f - sy * carWidth * 0.24f });
            window_.draw(hl);
        }

        // Taillights
        {
            const float sx = -dy, sy = dx;
            const float lw = carLength * 0.045f, lh = carWidth * 0.22f;
            sf::RectangleShape tl({ lw, lh });
            tl.setOrigin({ lw / 2.f, lh / 2.f });
            tl.setRotation(sf::degrees(deg));
            tl.setFillColor(sf::Color(215, 45, 45, 210));
            tl.setPosition({ pos.x - dx * carLength * 0.46f + sx * carWidth * 0.24f,
                             pos.y - dy * carLength * 0.46f + sy * carWidth * 0.24f });
            window_.draw(tl);
            tl.setPosition({ pos.x - dx * carLength * 0.46f - sx * carWidth * 0.24f,
                             pos.y - dy * carLength * 0.46f - sy * carWidth * 0.24f });
            window_.draw(tl);
        }

        if (vehicle.signal != Signal::Off) {
            const bool blinkOn = static_cast<int>(std::fmod(ImGui::GetTime() * 2.0, 2.0)) == 0;
            if (blinkOn || vehicle.signal == Signal::Hazard) {
                sf::CircleShape blinker(pixelsPerCell_ * 0.09f, 12);
                blinker.setOrigin({ blinker.getRadius(), blinker.getRadius() });
                blinker.setFillColor(sf::Color(255, 180, 40));
                const float sx = -dy, sy = dx;
                if (vehicle.signal == Signal::Left || vehicle.signal == Signal::Hazard) {
                    blinker.setPosition({ pos.x + dx * carLength * 0.32f - sx * carWidth * 0.35f,
                                          pos.y + dy * carLength * 0.32f - sy * carWidth * 0.35f });
                    window_.draw(blinker);
                }
                if (vehicle.signal == Signal::Right || vehicle.signal == Signal::Hazard) {
                    blinker.setPosition({ pos.x + dx * carLength * 0.32f + sx * carWidth * 0.35f,
                                          pos.y + dy * carLength * 0.32f + sy * carWidth * 0.35f });
                    window_.draw(blinker);
                }
            }
        }
    }


    void Renderer::drawWorld(const MapSystem& map, const VehicleState& vehicle,
        const Trail* trail, const std::vector<TrafficSignal>* signals,
        const std::vector<std::pair<Vec2, Penalty>>* penaltyMarkers,
        const ParkingSlot* targetSlot, const std::vector<Checkpoint>* checkpoints) {
        applyWorldView(map, vehicle);
        drawMapCached(map);
        // Roundabout overlay (smooth SFML circles)
        if (map.hasRoundabout()) {
            sf::Vector2f rc = worldToPixels(map.getRoundaboutCenter());
            float rad = map.getRoundaboutRadius() * pixelsPerCell_;

            // Outer road circle (compact, matching intersection size)
            float outerR = rad * 1.5f;
            sf::CircleShape outerRoad(outerR, 96);
            outerRoad.setFillColor(sf::Color(68, 68, 72));
            outerRoad.setOrigin({ outerR, outerR });
            outerRoad.setPosition(rc);
            window_.draw(outerRoad);

            // Green island
            float islandR = rad * 0.8f;
            sf::CircleShape island(islandR, 96);
            island.setFillColor(sf::Color(130, 200, 120));
            island.setOrigin({ islandR, islandR });
            island.setPosition(rc);
            window_.draw(island);

            // Island dark border ring
            sf::CircleShape islandBorder(islandR, 96);
            islandBorder.setFillColor(sf::Color::Transparent);
            islandBorder.setOutlineColor(sf::Color(80, 80, 85));
            islandBorder.setOutlineThickness(2.5f);
            islandBorder.setOrigin({ islandR, islandR });
            islandBorder.setPosition(rc);
            window_.draw(islandBorder);

            // Center water pool
            float poolR = rad * 0.38f;
            sf::CircleShape pool(poolR, 64);
            pool.setFillColor(sf::Color(50, 150, 250));
            pool.setOrigin({ poolR, poolR });
            pool.setPosition(rc);
            window_.draw(pool);

            // Statue base
            float baseS = rad * 0.22f;
            sf::RectangleShape base({ baseS * 2, baseS * 2 });
            base.setFillColor(sf::Color(100, 100, 110));
            base.setOrigin({ baseS, baseS });
            base.setPosition(rc);
            window_.draw(base);

            // Statue body
            float bodyS = rad * 0.15f;
            sf::RectangleShape sbody({ bodyS * 2, bodyS * 2 });
            sbody.setFillColor(sf::Color(220, 225, 230));
            sbody.setOrigin({ bodyS, bodyS });
            sbody.setPosition(rc);
            window_.draw(sbody);

            // Statue head
            float headR = rad * 0.08f;
            sf::CircleShape head(headR, 32);
            head.setFillColor(sf::Color(240, 245, 255));
            head.setOrigin({ headR, headR });
            head.setPosition(rc);
            window_.draw(head);
        }
        if (checkpoints)     drawCheckpoints(*checkpoints);
        if (trail)           drawTrail(*trail);
        if (penaltyMarkers)  drawPenaltyMarkers(*penaltyMarkers);
        if (targetSlot)      drawTargetSlot(*targetSlot);
        drawVehicle(vehicle);
        if (!map.getNpcCars().empty()) drawNpcCars(map);
        // Traffic lights drawn AFTER all cars so they're always visible
        if (signals)         drawSignals(*signals);
        resetUiView();
    }

    void Renderer::drawMenuUi(int selectedIndex, bool& roadARequested, bool& roadBRequested, bool& parallelParkingRequested, bool& tParkingRequested, bool& quitRequested) {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(displaySize);
        ImGui::Begin("MenuLayer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float time = (float)ImGui::GetTime();
        float speedMult = 200.0f;
        float roadY = displaySize.y * 0.45f;

        // --- celestial body movement ---
        // cycle range calculation
        float celestialRange = displaySize.x + 200.0f;
        float celestialX = std::fmod(time * speedMult, celestialRange) - 100.0f;

        // position progress (0=left, 0.5=center, 1=right)
        float posProgress = (celestialX + 100.0f) / celestialRange;

        // celestial height (highest at center)
        float celestialY = roadY - 100.0f - (std::sin(posProgress * 3.1415f) * 150.0f);

        ImU32 skyTop, skyBot, sunCol;
        float sunGlow = 0.0f;

        // state based on progress
        if (posProgress < 0.2f) { // morning (sunrise)
            float t = posProgress / 0.2f;
            skyTop = IM_COL32(255, 140, 80, 255);
            skyBot = IM_COL32(100, 160, 255, 255);
            sunCol = IM_COL32(255, 200, 50, 255);
            sunGlow = t;
        }
        else if (posProgress < 0.6f) { // daytime (noon)
            float t = (posProgress - 0.2f) / 0.4f;
            skyTop = IM_COL32(0, 120, 255, 255);
            skyBot = IM_COL32(135, 206, 250, 255);
            sunCol = IM_COL32(255, 255, 200, 255);
            sunGlow = 1.0f;
        }
        else if (posProgress < 0.85f) { // evening (sunset)
            float t = (posProgress - 0.6f) / 0.25f;
            skyTop = IM_COL32(255, 80, 40, 255);
            skyBot = IM_COL32(60, 60, 140, 255);
            sunCol = IM_COL32(255, 100, 0, 255);
            sunGlow = 1.0f - t;
        }
        else { // night
            skyTop = IM_COL32(10, 12, 30, 255);
            skyBot = IM_COL32(25, 30, 60, 255);
            sunCol = IM_COL32(200, 200, 255, 200); // looks like moon at night
            sunGlow = 0.2f;
        }

        // 0. draw sky and sun
        drawList->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(displaySize.x, roadY), skyTop, skyTop, skyBot, skyBot);

        // sun/moon (moves with car speed)
        drawList->AddCircleFilled(ImVec2(celestialX, celestialY), 35.0f, sunCol);
        if (sunGlow > 0.1f) {
            drawList->AddCircleFilled(ImVec2(celestialX, celestialY), 50.0f, IM_COL32(255, 255, 255, (int)(60 * sunGlow)));
        }

        // 1. background buildings
        for (int i = 0; i < 6; ++i) {
            float bWidth = 180.0f;
            float bX = std::fmod((time * speedMult * 0.5f) + (i * 250.0f), displaySize.x + bWidth) - bWidth;
            float bHeight = 150.0f + (std::sin(i * 1.5f) * 50.0f);
            float bY = roadY - bHeight;
            drawList->AddRectFilled(ImVec2(bX, bY), ImVec2(bX + bWidth * 0.8f, bY + bHeight), IM_COL32(30, 35, 45, 255));
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    drawList->AddRectFilled(ImVec2(bX + 15 + col * 35, bY + 20 + row * 40), ImVec2(bX + 35 + col * 35, bY + 45 + row * 40), IM_COL32(60, 70, 90, 255));
                }
            }
        }

        // 2. road
        float roadHeight = 120.0f;
        drawList->AddRectFilled(ImVec2(0, roadY), ImVec2(displaySize.x, roadY + roadHeight), IM_COL32(45, 48, 55, 255));
        float lineW = 40.0f; float lineGap = 40.0f;
        float lineOffset = std::fmod(time * speedMult, lineW + lineGap);
        for (float x = -lineOffset; x < displaySize.x; x += (lineW + lineGap)) {
            drawList->AddLine(ImVec2(x, roadY + roadHeight * 0.5f), ImVec2(x + lineW, roadY + roadHeight * 0.5f), IM_COL32(255, 255, 255, 180), 3.0f);
        }

        // 3. trees and traffic lights
        for (int i = 0; i < 8; ++i) {
            float objX = std::fmod((time * speedMult) + (i * 200.0f), displaySize.x + 100.0f) - 100.0f;
            float objBaseY = roadY;
            if (i % 3 == 0) {
                drawList->AddRectFilled(ImVec2(objX, objBaseY - 60), ImVec2(objX + 4, objBaseY), IM_COL32(80, 80, 80, 255));
                drawList->AddRectFilled(ImVec2(objX - 6, objBaseY - 90), ImVec2(objX + 10, objBaseY - 60), IM_COL32(20, 20, 20, 255), 2.0f);
                ImU32 lightCol = (std::fmod(time, 2.0f) > 1.0f) ? IM_COL32(0, 255, 0, 255) : IM_COL32(0, 100, 0, 255);
                drawList->AddCircleFilled(ImVec2(objX + 2, objBaseY - 68), 4.0f, lightCol);
            }
            else {
                drawList->AddRectFilled(ImVec2(objX, objBaseY - 30), ImVec2(objX + 6, objBaseY), IM_COL32(101, 67, 33, 255));
                drawList->AddCircleFilled(ImVec2(objX + 3, objBaseY - 45), 18.0f, IM_COL32(34, 139, 34, 255));
            }
        }

        // 4. yellow car
        float carW = 140.0f;
        float carX = std::fmod(time * speedMult * 1.2f, displaySize.x + carW) - carW;
        float carBottomY = roadY + roadHeight * 0.85f;

        auto drawSideCar = [&](float x, float y) {
            ImU32 bodyCol = IM_COL32(255, 210, 0, 255);
            ImU32 windowCol = IM_COL32(40, 50, 60, 230);
            ImU32 lightWhite = IM_COL32(255, 255, 200, 255);
            ImU32 lightRed = IM_COL32(255, 50, 0, 255);

            drawList->AddCircleFilled(ImVec2(x + 35, y), 13.0f, IM_COL32(15, 15, 15, 255));
            drawList->AddCircleFilled(ImVec2(x + 105, y), 13.0f, IM_COL32(15, 15, 15, 255));
            drawList->AddCircleFilled(ImVec2(x + 35, y), 5.0f, IM_COL32(80, 80, 80, 255));
            drawList->AddCircleFilled(ImVec2(x + 105, y), 5.0f, IM_COL32(80, 80, 80, 255));

            drawList->AddRectFilled(ImVec2(x, y - 35), ImVec2(x + carW, y - 5), bodyCol, 6.0f);
            drawList->AddRectFilled(ImVec2(x + 25, y - 62), ImVec2(x + 115, y - 35), bodyCol, 10.0f, ImDrawFlags_RoundCornersTop);

            drawList->AddRectFilled(ImVec2(x + 32, y - 55), ImVec2(x + 68, y - 38), windowCol, 2.0f);
            drawList->AddRectFilled(ImVec2(x + 72, y - 55), ImVec2(x + 108, y - 38), windowCol, 2.0f);

            drawList->AddLine(ImVec2(x + 70, y - 35), ImVec2(x + 70, y - 8), IM_COL32(0, 0, 0, 50), 1.5f);
            drawList->AddRectFilled(ImVec2(x + 55, y - 30), ImVec2(x + 65, y - 26), IM_COL32(30, 30, 30, 200), 1.0f);
            drawList->AddRectFilled(ImVec2(x + 75, y - 30), ImVec2(x + 85, y - 26), IM_COL32(30, 30, 30, 200), 1.0f);

            drawList->AddRectFilled(ImVec2(x + carW - 8, y - 30), ImVec2(x + carW, y - 15), lightWhite, 2.0f, ImDrawFlags_RoundCornersRight);
            drawList->AddCircleFilled(ImVec2(x + carW, y - 22.5f), 6.0f, IM_COL32(255, 255, 255, 80));
            drawList->AddRectFilled(ImVec2(x, y - 30), ImVec2(x + 6, y - 18), lightRed, 1.0f, ImDrawFlags_RoundCornersLeft);
            };
        drawSideCar(carX, carBottomY);

        // --- UI logic ---
        if (g_currentStep == MenuStep::MainMenu) {
            ImGui::SetCursorPosY(displaySize.y * 0.12f);
            float hue = std::fmod(time * 0.5f, 1.0f);
            ImGui::SetWindowFontScale(6.0f);
            std::string welcomeTxt = "WELCOME";
            ImGui::SetCursorPosX((displaySize.x - ImGui::CalcTextSize(welcomeTxt.c_str()).x) * 0.5f);
            ImGui::TextColored(HSLtoRGB(hue, 0.7f, 0.6f), "%s", welcomeTxt.c_str());

            ImGui::SetCursorPosY(displaySize.y * 0.62f);
            ImGui::SetWindowFontScale(2.2f);
            auto renderBtn = [&](const char* label, int idx) {
                ImVec2 size(460, 70);
                ImGui::SetCursorPosX((displaySize.x - size.x) * 0.5f);
                if (selectedIndex == idx) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.9f, 1.0f));
                bool clicked = ImGui::Button(label, size);
                if (selectedIndex == idx) ImGui::PopStyleColor();
                return clicked;
                };

            if (renderBtn("ROAD DRIVING EXAM", 0)) g_currentStep = MenuStep::CourseSelect;
            ImGui::Spacing();
            if (renderBtn("PARKING PRACTICE", 1)) g_currentStep = MenuStep::ParkingSelect;
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 0.9f));
            if (renderBtn("QUIT GAME", 2)) quitRequested = true;
            ImGui::PopStyleColor();
        }
        else if (g_currentStep == MenuStep::CourseSelect) {
            ImGui::SetCursorPosY(displaySize.y * 0.15f);
            ImGui::SetWindowFontScale(4.0f);
            const char* selectTxt = "SELECT COURSE";
            ImGui::SetCursorPosX((displaySize.x - ImGui::CalcTextSize(selectTxt).x) * 0.5f);
            ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", selectTxt);

            ImGui::SetCursorPosY(displaySize.y * 0.60f);
            ImGui::SetWindowFontScale(2.5f);
            float btnW = 500.0f; float btnH = 100.0f;
            float startX = (displaySize.x - (btnW * 2 + 40.0f)) * 0.5f;

            ImGui::SetCursorPosX(startX);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.3f, 0.9f));
            if (ImGui::Button(" [A] COURSE ", ImVec2(btnW, btnH))) { roadARequested = true; g_currentStep = MenuStep::MainMenu; }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetCursorPosX(startX + btnW + 40.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.8f, 0.9f));
            if (ImGui::Button(" [B] COURSE", ImVec2(btnW, btnH))) { roadBRequested = true; g_currentStep = MenuStep::MainMenu; }
            ImGui::PopStyleColor();

            ImGui::SetCursorPosY(displaySize.y * 0.85f);
            ImGui::SetWindowFontScale(1.5f);
            ImGui::SetCursorPosX((displaySize.x - 200) * 0.5f);
            if (ImGui::Button("BACK TO MENU", ImVec2(200, 50))) g_currentStep = MenuStep::MainMenu;
        }
        else if (g_currentStep == MenuStep::ParkingSelect) {
            ImGui::SetCursorPosY(displaySize.y * 0.15f);
            ImGui::SetWindowFontScale(4.0f);
            const char* selectTxt = "SELECT PARKING TYPE";
            ImGui::SetCursorPosX((displaySize.x - ImGui::CalcTextSize(selectTxt).x) * 0.5f);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", selectTxt);

            ImGui::SetCursorPosY(displaySize.y * 0.60f);
            ImGui::SetWindowFontScale(2.5f);
            float btnW = 500.0f; float btnH = 100.0f;
            float startX = (displaySize.x - (btnW * 2 + 40.0f)) * 0.5f;

            ImGui::SetCursorPosX(startX);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.2f, 0.6f, 0.9f));
            if (ImGui::Button(" T-SHAPE ", ImVec2(btnW, btnH))) { tParkingRequested = true; g_currentStep = MenuStep::MainMenu; }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetCursorPosX(startX + btnW + 40.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.5f, 0.9f));
            if (ImGui::Button(" PARALLEL ", ImVec2(btnW, btnH))) { parallelParkingRequested = true; g_currentStep = MenuStep::MainMenu; }
            ImGui::PopStyleColor();

            ImGui::SetCursorPosY(displaySize.y * 0.85f);
            ImGui::SetWindowFontScale(1.5f);
            ImGui::SetCursorPosX((displaySize.x - 200) * 0.5f);
            if (ImGui::Button("BACK TO MENU", ImVec2(200, 50))) g_currentStep = MenuStep::MainMenu;
        }

        ImGui::End();
    }

    void Renderer::drawDrivingUi(const VehicleState& vehicle,
        int cpDone, int cpTotal, int penalty, float elapsed,
        int collisions, const std::string& warning, float warnTime, bool courseMode,
        bool& menuRequested) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 320, vp->WorkPos.y + 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300, 270), ImGuiCond_Always);
        ImGui::Begin(courseMode ? "Road Driving HUD" : "Parking HUD", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::Text("Speed: %.1f km/h", std::fabs(vehicle.speed) * 3.6f);
        ImGui::Text("Gear: %s", gearToChar(vehicle.gear));
        ImGui::Text("Engine: %s", vehicle.engineOn ? "ON" : "OFF");
        ImGui::Text("Seatbelt: %s", vehicle.seatbeltOn ? "ON" : "OFF");
        ImGui::Text("Auto Hold: %s", vehicle.autoHold ? "ON" : "OFF");
        ImGui::Text("Elapsed: %.1f s", elapsed);
        ImGui::Text("Penalty: %d", penalty);
        ImGui::Text("Collisions: %d", collisions);
        if (courseMode) ImGui::Text("Checkpoints: %d / %d", cpDone, cpTotal);
        ImGui::Separator();
        ImGui::SliderFloat("Zoom", &zoom_, 0.15f, 4.0f, "%.2fx");
        if (warnTime > 0.0f && !warning.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.18f, 1.0f), "%s", warning.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("BACK TO MENU", ImVec2(280, 0))) menuRequested = true;
        ImGui::End();
    }

    void Renderer::drawResultUi(const std::string& title,
        float elapsed, int cpDone, int cpTotal, int penalty, int collisions,
        const std::vector<Penalty>& penaltyLog,
        bool disqualified, bool passed, const std::string& disqualifyReason, int finalScore,
        bool canReplay,
        bool& restartRequested, bool& replayRequested, bool& menuRequested, bool& quitRequested) {

        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        float ww = std::min(ds.x * 0.55f, 620.0f), wh = std::min(ds.y * 0.72f, 520.0f);
        ImGui::SetNextWindowSize(ImVec2(ww, wh), ImGuiCond_Always);

        ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
        ImGui::SetWindowFontScale(1.35f);

        ImGui::Text("=== Driving Exam Result ===");
        ImGui::Spacing();

        if (disqualified) {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Result: DISQUALIFIED");
            ImGui::TextWrapped("Reason: %s", toKoreanReason(disqualifyReason).c_str());
        }
        else if (passed) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.35f, 1), "Result: PASS");
            ImGui::Text("Final Score: %d / 100", finalScore);
        }
        else {
            ImGui::TextColored(ImVec4(1, 0.82f, 0.2f, 1), "Result: FAIL");
            ImGui::Text("Final Score: %d / 100", finalScore);
        }

        ImGui::Separator();
        ImGui::Text("Drive Time: %.1f sec", elapsed);
        ImGui::Text("Progress: %d / %d", cpDone, cpTotal);
        ImGui::Text("Penalty: %d pts", penalty);
        ImGui::Text("Collisions: %d", collisions);

        ImGui::Separator();
        ImGui::Text("Penalty / Disqualification Log");
        if (penaltyLog.empty()) {
            ImGui::TextDisabled("No penalties recorded.");
        }
        else {
            float logH = wh * 0.28f;
            ImGui::BeginChild("PenaltyLog", ImVec2(0, logH), true);
            for (const auto& p : penaltyLog) {
                if (p.points >= 100) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "[DQ] %.1fs - %s", p.timestamp, toKoreanReason(p.description).c_str());
                else ImGui::Text("- %s (%d pts, %.1fs)", toKoreanReason(p.description).c_str(), p.points, p.timestamp);
            }
            ImGui::EndChild();
        }
        ImGui::Separator();
        ImGui::Spacing();
        float bw = 130.0f, bh = 32.0f;
        float totalBw = bw * 3 + (canReplay ? bw + 30 : 0) + 30;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - totalBw) * 0.5f);
        if (ImGui::Button("Restart", ImVec2(bw, bh))) restartRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Next", ImVec2(bw, bh))) menuRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Quit", ImVec2(bw, bh))) quitRequested = true;
        if (canReplay) { ImGui::SameLine(); if (ImGui::Button("Replay", ImVec2(bw, bh))) replayRequested = true; }

        ImGui::SetWindowFontScale(1.0f);
        ImGui::End();
    }

    void Renderer::drawResultDetailUi(const std::string& title,
        int finalScore, bool disqualified, bool passed, const std::string& disqualifyReason,
        const std::vector<Penalty>& penaltyLog,
        bool& nextToMenuRequested, bool& replayRequested, bool& backRequested, bool canReplay) {

        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        float ww = std::min(ds.x * 0.55f, 620.0f), wh = std::min(ds.y * 0.72f, 520.0f);
        ImGui::SetNextWindowSize(ImVec2(ww, wh), ImGuiCond_Always);
        ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
        ImGui::SetWindowFontScale(1.35f);

        ImGui::Text("=== Detailed Exam Report ===");
        ImGui::Separator();
        if (disqualified) { ImGui::TextColored(ImVec4(1, 0.25f, 0.25f, 1), "Result: DISQUALIFIED"); ImGui::TextWrapped("Reason: %s", toKoreanReason(disqualifyReason).c_str()); }
        else if (passed) { ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.35f, 1), "Result: PASS"); ImGui::Text("Score: %d / 100", finalScore); }
        else { ImGui::TextColored(ImVec4(1, 0.82f, 0.2f, 1), "Result: FAIL"); ImGui::Text("Score: %d / 100", finalScore); }

        ImGui::Separator();
        ImGui::Text("Detailed Penalty Log");
        if (penaltyLog.empty()) { ImGui::TextDisabled("No penalties recorded."); }
        else {
            float logH = wh * 0.45f;
            ImGui::BeginChild("DetailPenaltyLog", ImVec2(0, logH), true);
            for (const auto& p : penaltyLog) {
                if (p.points >= 100) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "[DQ] %.1fs - %s", p.timestamp, toKoreanReason(p.description).c_str());
                else ImGui::Text("- %.1fs | %s | -%d pts", p.timestamp, toKoreanReason(p.description).c_str(), p.points);
            }
            ImGui::EndChild();
        }
        ImGui::Separator();
        ImGui::Spacing();
        float bw = 140.0f, bh = 32.0f;
        float totalBw = bw * 2 + (canReplay ? bw + 15 : 0) + 15;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - totalBw) * 0.5f);
        if (ImGui::Button("Back", ImVec2(bw, bh))) backRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Menu", ImVec2(bw, bh))) nextToMenuRequested = true;
        if (canReplay) { ImGui::SameLine(); if (ImGui::Button("Replay", ImVec2(bw, bh))) replayRequested = true; }

        ImGui::SetWindowFontScale(1.0f);
        ImGui::End();
    }

    void Renderer::drawCollisionUi(bool& restartRequested, bool& menuRequested, bool& quitRequested) {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        float ww = std::min(ds.x * 0.45f, 480.0f), wh = std::min(ds.y * 0.35f, 240.0f);
        ImGui::SetNextWindowSize(ImVec2(ww, wh), ImGuiCond_Always);
        ImGui::Begin("Collision", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
        ImGui::SetWindowFontScale(1.4f);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "Collision detected!");
        ImGui::Spacing();
        ImGui::Text("Press Next to view violation details.");
        ImGui::Separator();
        ImGui::Spacing(); ImGui::Spacing();

        float bw = 130.0f, bh = 34.0f;
        float totalBw = bw * 3 + 20.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - totalBw) * 0.5f);
        if (ImGui::Button("Restart", ImVec2(bw, bh))) restartRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Next", ImVec2(bw, bh))) menuRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Quit", ImVec2(bw, bh))) quitRequested = true;

        ImGui::SetWindowFontScale(1.0f);
        ImGui::End();
    }

    void Renderer::drawCollisionDetailUi(const std::string& title, const std::string& collisionReason,
        const std::vector<Penalty>& penaltyLog, bool& backRequested, bool& nextToMenuRequested) {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        float ww = std::min(ds.x * 0.55f, 600.0f), wh = std::min(ds.y * 0.6f, 420.0f);
        ImGui::SetNextWindowSize(ImVec2(ww, wh), ImGuiCond_Always);
        ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);
        ImGui::SetWindowFontScale(1.35f);

        ImGui::Text("=== Collision / Violation Detail ===");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "Reason: %s", toKoreanReason(collisionReason).c_str());
        ImGui::Separator();
        ImGui::Text("Related Penalty Log");
        if (penaltyLog.empty()) { ImGui::TextDisabled("No related penalties."); }
        else {
            float logH = wh * 0.45f;
            ImGui::BeginChild("CollisionPenaltyLog", ImVec2(0, logH), true);
            for (const auto& p : penaltyLog) {
                if (p.points >= 100) ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "[DQ] %.1fs - %s", p.timestamp, toKoreanReason(p.description).c_str());
                else ImGui::Text("- %.1fs | %s | %d pts", p.timestamp, toKoreanReason(p.description).c_str(), p.points);
            }
            ImGui::EndChild();
        }
        ImGui::Separator(); ImGui::Spacing();
        float bw = 150.0f, bh = 32.0f;
        float totalBw = bw * 2 + 15.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - totalBw) * 0.5f);
        if (ImGui::Button("Back", ImVec2(bw, bh))) backRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Menu", ImVec2(bw, bh))) nextToMenuRequested = true;

        ImGui::SetWindowFontScale(1.0f);
        ImGui::End();
    }

    void Renderer::drawReplayUi(float currentTime, float totalTime, float& playSpeed,
        bool paused, int frameIndex, int totalFrames,
        bool& togglePlay, bool& menuRequested, bool& restartRequested,
        bool& prevPenaltyRequested, bool& nextPenaltyRequested, int& requestedFrameIndex) {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_Always);
        ImGui::Begin("Replay", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::Text("Time: %.2f / %.2f", currentTime, totalTime);
        ImGui::Text("Status: %s", paused ? "Paused" : "Playing");
        if (ImGui::Button(paused ? "Play" : "Pause", ImVec2(90, 0))) togglePlay = true;
        ImGui::SameLine();
        if (ImGui::Button("Prev Penalty", ImVec2(120, 0))) prevPenaltyRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Next Penalty", ImVec2(120, 0))) nextPenaltyRequested = true;
        ImGui::SliderFloat("Playback Speed", &playSpeed, 0.25f, 4.0f, "%.2fx");
        int sliderIndex = frameIndex;
        if (totalFrames <= 0) totalFrames = 1;
        if (ImGui::SliderInt("Frame", &sliderIndex, 0, totalFrames - 1)) requestedFrameIndex = sliderIndex;
        if (ImGui::Button("Restart Run", ImVec2(120, 0))) restartRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Back to Menu", ImVec2(120, 0))) menuRequested = true;
        ImGui::End();
    }

    void Renderer::drawCourseSelectUi(int& /*selectedCourse*/, bool& /*confirmed*/, bool& /*back*/) {}

} // namespace bestdriver