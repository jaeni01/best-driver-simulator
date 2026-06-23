#pragma once
#ifndef BESTDRIVER_MAP_SYSTEM_H
#define BESTDRIVER_MAP_SYSTEM_H

#include "common_types.h"
#include <vector>
#include <string>
#include <utility>

namespace bestdriver {

    // ================================================================
    // Visual Tile IDs
    // ================================================================
    enum TileID {
        RD_PLAIN, RD_LANE_H, RD_LANE_V, RD_DASH_H, RD_DASH_V,
        RD_CROSS, RD_CWALK_H, RD_CWALK_V, RD_SHOULDER,
        SW_PLAIN, SW_CURB, SW_LAMP, SW_BENCH, SW_TREE,
        BLD_APART, BLD_HOUSE, BLD_SHOP, BLD_OFFICE, BLD_PARK, BLD_PARKING,
        BLD_ON_PARK_V, BLD_ON_PARK_H,
        NAT_GRASS, NAT_TREE, NAT_GARDEN, NAT_POND,
        // Course B building types
        BLD_VILLA, BLD_HIGHRISE,
        BLD_CAFE, BLD_CONVENIENCE, BLD_RESTAURANT,
        BLD_MART, BLD_HOTEL,
        BLD_HOSPITAL, BLD_SCHOOL, BLD_POLICE, BLD_FIRESTATION,
        BLD_GASSTATION, BLD_STADIUM,
        // Course B road/sidewalk extras
        RD_ROUNDABOUT, RD_DIAG, RD_MERGE,
        SW_FLOWER, SW_PERSON, SW_DOG,
        // Parking map tiles
        PK_ZONE,         // parking lot asphalt
        PK_STRIPE,       // parking slot interior (lighter asphalt)
        PK_LINE_V,       // slot divider vertical
        PK_LINE_H,       // slot divider horizontal
        PK_EMPTY,        // empty target slot
        PK_CENTER,       // road center line (double yellow)
        PK_DASH,         // road dashed lane line
        PK_CURB_CORNER,  // curb corner
        CAR_Y,           // yellow car body
        CAR_W,           // white car body
        CAR_R,           // red car body
        CAR_BL,          // blue car body
        CAR_G,           // green car body
        CAR_GY,          // gray car body
        CAR_P,           // purple car body
        CAR_BK,          // black car body
        CAR_GLASS,       // car window glass
        TILE_COUNT
    };

    // ================================================================
    // NPC Cars
    // ================================================================
    enum NpcDir { NPC_RIGHT = 0, NPC_LEFT, NPC_DOWN, NPC_UP };

    struct NpcCar {
        Vec2   position;
        NpcDir dir;
        float  speed;
        int    cr, cg, cb;   // body color

        // Roundabout navigation
        float  heading;
        bool   inRoundabout;
        float  orbAngle;
        float  exitAngle;
        NpcDir exitDir;
        float  exitAlignTimer;

        // Respawn system
        bool   waiting;        // true = car is invisible, waiting to respawn
        float  respawnTimer;   // countdown until respawn (seconds)
        float  spawnX, spawnY; // original spawn position
        NpcDir spawnDir;       // original spawn direction
        NpcDir desiredExitDir; // if != spawnDir, override exitDir when entering roundabout
        bool   hasCustomExit;  // true = use desiredExitDir instead of car.dir for roundabout exit

        NpcCar() : dir(NPC_RIGHT), speed(1.5f), cr(220), cg(60), cb(60),
            heading(0.0f), inRoundabout(false), orbAngle(0.0f),
            exitAngle(0.0f), exitDir(NPC_RIGHT), exitAlignTimer(0.0f),
            waiting(false), respawnTimer(0.0f), spawnX(0.0f), spawnY(0.0f), spawnDir(NPC_RIGHT),
            desiredExitDir(NPC_RIGHT), hasCustomExit(false) {}
        NpcCar(float x, float y, NpcDir d, float sp,
            int r, int g, int b)
            : position(x, y), dir(d), speed(sp), cr(r), cg(g), cb(b),
            heading(0.0f), inRoundabout(false), orbAngle(0.0f),
            exitAngle(0.0f), exitDir(d), exitAlignTimer(0.0f),
            waiting(false), respawnTimer(0.0f), spawnX(x), spawnY(y), spawnDir(d),
            desiredExitDir(d), hasCustomExit(false) {
            switch (d) {
            case NPC_RIGHT: heading = 0.0f; break;
            case NPC_LEFT:  heading = (float)M_PI; break;
            case NPC_DOWN:  heading = (float)M_PI * 0.5f; break;
            case NPC_UP:    heading = -(float)M_PI * 0.5f; break;
            }
        }
    };

    // ================================================================
    // Trail
    // ================================================================
    struct Trail {
        std::vector<std::pair<int, int>> pts;
        void add(int x, int y) {
            if (!pts.empty() && pts.back().first == x && pts.back().second == y) return;
            pts.push_back({ x, y });
            if (pts.size() > 3000) pts.erase(pts.begin());
        }
        bool has(int x, int y) const {
            for (auto& p : pts) if (p.first == x && p.second == y) return true;
            return false;
        }
        void clear() { pts.clear(); }
    };

    // ================================================================
    // Raw cell type (game logic)
    // ================================================================
    enum RawCellType {
        RC_ROAD = 0, RC_WALL, RC_CURB, RC_GRASS, RC_CLINE, RC_PARK,
        RC_BLDG, RC_STOP, RC_XWALK, RC_BUMP,
        RC_YCAR, RC_WCAR, RC_CWINDOW, RC_PARKLINE
    };

    class ICourseMap {
    public:
        virtual ~ICourseMap() {}
        virtual CellType getCellAt(int gx, int gy) const = 0;
    };

    // ================================================================
    // MapSystem
    // ================================================================
    class MapSystem : public ICourseMap {
    public:
        MapSystem();

        // -- Legacy load methods --
        bool loadFromFile(const std::string& filepath);
        bool loadParkingFromFile(const std::string& filepath);
        void generateParkingMap();

        // -- Block-based course map generation ----------------------
        void generateCourseMap();       // Course A (original)
        void generateCourseMapB();      // Course B (larger blocks, more varied)

        // -- Parking map generation --------------------------------
        void generateParallelParkingMap();  // Parallel parking (48x32)
        void generateTParkingMap();         // T-shaped parking lot (48x32)

        // -- Queries --
        CellType getCellAt(int x, int y) const override;
        bool     isInBounds(int x, int y) const;
        bool     isPassable(int x, int y) const;
        int      getRawCell(int x, int y) const;
        TileID   getTileId(int x, int y) const;
        bool     hasTileGrid() const { return hasTileGrid_; }

        int   getWidth()         const { return width_; }
        int   getHeight()        const { return height_; }
        Vec2  getSpawnPosition() const { return Vec2(spawnX_, spawnY_); }
        float getSpawnHeading()  const { return spawnHeading_; }

        const std::vector<Checkpoint>& getCheckpoints()    const { return checkpoints_; }
        std::vector<Checkpoint>& getCheckpointsMut() { return checkpoints_; }
        const std::vector<ParkingSlot>& getParkingSlots()   const { return parkingSlots_; }

        // -- NPC Cars --
        const std::vector<NpcCar>& getNpcCars()    const { return npcCars_; }
        std::vector<NpcCar>& getNpcCarsMut() { return npcCars_; }
        void updateNpcCars(float dt, const std::vector<TrafficSignal>* signals = nullptr, const VehicleState* player = nullptr);

        // Roundabout overlay info
        bool  hasRoundabout()     const { return hasRoundabout_; }
        Vec2  getRoundaboutCenter() const { return roundaboutCenter_; }
        float getRoundaboutRadius() const { return roundaboutRadius_; }

    private:
        // Legacy grid (RC_*, game logic)
        std::vector<std::vector<int>> grid_;
        int   width_, height_;
        float spawnX_, spawnY_, spawnHeading_;
        float npcDelay_ = 0.0f;
        bool  hasTileGrid_;
        bool  hasRoundabout_ = false;
        Vec2  roundaboutCenter_;
        float roundaboutRadius_ = 0.0f;

        std::vector<Checkpoint>  checkpoints_;
        std::vector<ParkingSlot> parkingSlots_;

        // New tile grid (TileID, rendering)
        std::vector<std::vector<TileID>> tileGrid_;
        std::vector<NpcCar>              npcCars_;

        // -- Block layout constants (Course A) ----------------------
        static const int BLOCK_ROWS = 4;
        static const int BLOCK_COLS = 7;
        static const int ROAD_W = 5;
        static const int COL_W[BLOCK_COLS];
        static const int ROW_H[BLOCK_ROWS];
        static const TileID BLOCK_FILLS[BLOCK_ROWS][BLOCK_COLS];

        // -- Block layout constants (Course B) ----------------------
        static const int COL_W_B[BLOCK_COLS];
        static const int ROW_H_B[BLOCK_ROWS];
        static const TileID BLOCK_FILLS_B[BLOCK_ROWS][BLOCK_COLS];

        int MAP_W_, MAP_H_;
        int colStart_[BLOCK_COLS];
        int rowStart_[BLOCK_ROWS];
        int vRoadX_[BLOCK_COLS - 1];
        int hRoadY_[BLOCK_ROWS - 1];

        // -- Internal helpers (Course A) ---------------------------
        void calcLayout();
        bool isVRoadCol(int c) const;
        bool isHRoadRow(int r) const;
        int  getVRoadIdx(int c) const;
        int  getHRoadIdx(int r) const;

        void buildTileGrid();
        void buildRcGrid();
        void buildCheckpoints();
        void initNpcCars();

        static int tileToRc(TileID t);

        void fixRightSideDriving();

        // -- Internal helpers (Course B) ---------------------------
        void calcLayoutB();
        void buildTileGridB();
        void buildRcGridB();
        void buildCheckpointsB();
        void initNpcCarsB();

        // Course B layout data (filled by calcLayoutB)
        int MAP_W_B_, MAP_H_B_;
        int colStartB_[BLOCK_COLS];
        int rowStartB_[BLOCK_ROWS];
        int vRoadXB_[BLOCK_COLS - 1];
        int hRoadYB_[BLOCK_ROWS - 1];

        bool isVRoadColB(int c) const;
        bool isHRoadRowB(int r) const;
        int  getVRoadIdxB(int c) const;
        int  getHRoadIdxB(int r) const;
    };

} // namespace bestdriver
#endif
