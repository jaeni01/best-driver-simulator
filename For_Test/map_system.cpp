#define _CRT_SECURE_NO_WARNINGS
#include "map_system.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <algorithm>
#include <cmath>
#include <climits>

namespace bestdriver {


    // Block layout static constants

    const int MapSystem::COL_W[BLOCK_COLS] = { 4, 8, 5, 10, 6, 7, 4 };
    const int MapSystem::ROW_H[BLOCK_ROWS] = { 5, 9, 7, 4 };

    const TileID MapSystem::BLOCK_FILLS[BLOCK_ROWS][BLOCK_COLS] = {
        { BLD_SHOP,    BLD_APART,   BLD_HOUSE,   BLD_PARK,    BLD_OFFICE,  BLD_SHOP,   BLD_HOUSE  },
        { BLD_HOUSE,   BLD_OFFICE,  BLD_APART,   BLD_APART,   BLD_SHOP,    BLD_PARK,   BLD_SHOP   },
        { BLD_PARKING, BLD_APART,   BLD_PARK,    BLD_OFFICE,  BLD_HOUSE,   BLD_APART,  BLD_OFFICE },
        { BLD_HOUSE,   BLD_SHOP,    BLD_PARKING, BLD_SHOP,    BLD_APART,   BLD_HOUSE,  BLD_PARK   },
    };

    // Course B: larger blocks, different arrangement
    const int MapSystem::COL_W_B[BLOCK_COLS] = { 5, 10, 6, 12, 7, 9, 5 };
    const int MapSystem::ROW_H_B[BLOCK_ROWS] = { 6, 10, 8, 5 };

    const TileID MapSystem::BLOCK_FILLS_B[BLOCK_ROWS][BLOCK_COLS] = {
        { BLD_SHOP,    BLD_APART,   BLD_HOUSE,   BLD_PARK,    BLD_OFFICE,  BLD_SHOP,    BLD_HOUSE  },
        { BLD_VILLA,   BLD_SHOP,    BLD_APART,   BLD_APART,   BLD_SHOP,    BLD_PARK,    BLD_SHOP   },
        { BLD_PARKING, BLD_APART,   BLD_PARK,    BLD_OFFICE,  BLD_HOUSE,   BLD_APART,   BLD_OFFICE },
        { BLD_HOUSE,   BLD_SHOP,    BLD_PARKING, BLD_SHOP,    BLD_APART,   BLD_VILLA,   BLD_PARK   },
    };


    // Constructor

    MapSystem::MapSystem()
        : width_(0), height_(0),
        spawnX_(2), spawnY_(1), spawnHeading_(0.0f),
        hasTileGrid_(false),
        MAP_W_(0), MAP_H_(0),
        MAP_W_B_(0), MAP_H_B_(0)
    {
    }


    // Legacy: load road map from text file
    //지워도됨
    bool MapSystem::loadFromFile(const std::string& filepath) {
        FILE* fp = fopen(filepath.c_str(), "r");
        if (!fp) return false;
        hasTileGrid_ = false;
        grid_.clear(); checkpoints_.clear();
        char buf[4096];
        while (fgets(buf, sizeof(buf), fp)) {
            std::string ln(buf);
            while (!ln.empty() && (ln.back() == '\n' || ln.back() == '\r')) ln.pop_back();
            if (ln.empty() || ln[0] == '#') continue;
            int row = (int)grid_.size();
            std::vector<int> r;
            for (int i = 0; i < (int)ln.size(); i++) {
                char ch = ln[i]; int ct = RC_ROAD;
                switch (ch) {
                case 'C': ct = RC_CURB; break; case '#': ct = RC_BLDG; break;
                case 'G': ct = RC_GRASS; break; case 'U': ct = RC_CLINE; break;
                case 'P': ct = RC_PARK; break; case 'L': ct = RC_STOP; break;
                case 'X': ct = RC_XWALK; break; case 'B': ct = RC_BUMP; break;
                case 'S': ct = RC_ROAD; spawnX_ = (float)i; spawnY_ = (float)row; break;
                case '.': case ' ': ct = RC_ROAD; break;
                default:
                    ct = RC_ROAD;
                    if (ch >= '0' && ch <= '9') {
                        int num = ch - '0'; int numLen = 1;
                        while (i + numLen < (int)ln.size() && ln[i + numLen] >= '0' && ln[i + numLen] <= '9') {
                            num = num * 10 + (ln[i + numLen] - '0'); numLen++;
                        }
                        float cpx = (float)i + (numLen - 1) * 0.5f;
                        bool dup = false;
                        for (size_t ci = 0; ci < checkpoints_.size(); ci++)
                            if (checkpoints_[ci].id == num && (int)checkpoints_[ci].position.y == row) { dup = true; break; }
                        if (!dup) {
                            Checkpoint cp; cp.id = num; cp.position = Vec2(cpx, (float)row);
                            cp.radius = 4.0f; cp.passed = false; checkpoints_.push_back(cp);
                        }
                        for (int skip = 1; skip < numLen; skip++) { r.push_back(RC_ROAD); i++; }
                    }
                    break;
                }
                r.push_back(ct);
            }
            grid_.push_back(r);
        }
        fclose(fp);
        height_ = (int)grid_.size(); width_ = 0;
        for (size_t i = 0; i < grid_.size(); i++) if ((int)grid_[i].size() > width_) width_ = (int)grid_[i].size();
        for (size_t i = 0; i < grid_.size(); i++) grid_[i].resize(width_, RC_ROAD);
        fixRightSideDriving();
        return height_ > 0;
    }


    // Legacy: load parking map file

    bool MapSystem::loadParkingFromFile(const std::string& filepath) {
        FILE* fp = fopen(filepath.c_str(), "r");
        if (!fp) return false;
        hasTileGrid_ = false;
        grid_.clear(); checkpoints_.clear(); parkingSlots_.clear();
        char buf[4096];
        while (fgets(buf, sizeof(buf), fp)) {
            std::string ln(buf);
            while (!ln.empty() && (ln.back() == '\n' || ln.back() == '\r')) ln.pop_back();
            if (ln.empty()) { grid_.push_back(std::vector<int>()); continue; }
            int row = (int)grid_.size();
            std::vector<int> r;
            for (int i = 0; i < (int)ln.size(); i++) {
                char ch = ln[i]; int ct = RC_ROAD;
                switch (ch) {
                case 'C': ct = RC_CURB; break;
                case 'G': ct = RC_PARKLINE; break;
                case 'Y': ct = RC_YCAR; break;
                case 'W': ct = RC_WCAR; break;
                case 'B': ct = RC_CWINDOW; break;
                case 'S': ct = RC_ROAD; spawnX_ = (float)i; spawnY_ = (float)row; break;
                case ' ': case '.': ct = RC_ROAD; break;
                default: ct = RC_ROAD; break;
                }
                r.push_back(ct);
            }
            grid_.push_back(r);
        }
        fclose(fp);
        height_ = (int)grid_.size(); width_ = 0;
        for (size_t i = 0; i < grid_.size(); i++) if ((int)grid_[i].size() > width_) width_ = (int)grid_[i].size();
        for (size_t i = 0; i < grid_.size(); i++) grid_[i].resize(width_, RC_ROAD);
        spawnX_ = (float)(width_ / 2);
        spawnY_ = 12.0f;
        ParkingSlot target;
        target.id = 0;
        target.position = Vec2((float)(width_ / 2), 35.0f);
        target.angle = (float)M_PI / 2.0f;
        target.isTarget = true;
        parkingSlots_.push_back(target);
        return height_ > 0;
    }


    // Legacy: fixRightSideDriving

    void MapSystem::fixRightSideDriving() {
        if ((int)spawnY_ <= 3) spawnY_ += 4.0f;
        for (size_t i = 0; i < checkpoints_.size(); i++) {
            int cpRow = (int)checkpoints_[i].position.y;
            if (cpRow == 1 && checkpoints_[i].position.x < 65)
                checkpoints_[i].position.y += 4.0f;
            if (cpRow == 27)
                checkpoints_[i].position.y += 2.0f;
        }
    }


    // Legacy: fallback parking map generation

    void MapSystem::generateParkingMap() {
        if (loadParkingFromFile("parking_v2.txt")) return;
        hasTileGrid_ = false;
        width_ = 30; height_ = 20;
        grid_.assign(height_, std::vector<int>(width_, RC_WALL));
        for (int y = 6; y <= 13; y++) for (int x = 2; x <= 27; x++) grid_[y][x] = RC_ROAD;
        for (int x = 4; x <= 25; x += 4) for (int y = 2; y <= 5; y++) grid_[y][x] = RC_PARK;
        int slotId = 0;
        for (int x = 4; x <= 25; x += 4) {
            for (int y = 14; y <= 17; y++) grid_[y][x] = RC_PARK;
            ParkingSlot slot; slot.id = slotId++; slot.position = Vec2((float)x, 15.5f);
            slot.angle = (float)M_PI / 2.0f; slot.isTarget = (x == 16);
            parkingSlots_.push_back(slot);
        }
        spawnX_ = 14.0f; spawnY_ = 9.0f;
    }


    // Block-based course map generation

    void MapSystem::generateCourseMap() {
        checkpoints_.clear();
        parkingSlots_.clear();
        npcCars_.clear();
        tileGrid_.clear();

        calcLayout();
        buildTileGrid();
        buildRcGrid();
        buildCheckpoints();
        initNpcCars();

        hasRoundabout_ = false;
        hasTileGrid_ = true;

        // Spawn: on first horizontal road, right lane center.
        // X = midpoint of first block's road-facing side (between
        // vRoadX_[0] and vRoadX_[1]), away from crosswalks.
        spawnX_ = (float)(vRoadX_[0] + ROAD_W) + (float)(COL_W[1] / 2);
        spawnY_ = (float)(hRoadY_[0] + 3) + 0.75f;
        spawnHeading_ = 0.0f;
        npcDelay_ = 5.0f; // NPC delay 5 seconds

        // Remove NPCs near spawn
        const float clearR = 10.0f;
        npcCars_.erase(
            std::remove_if(npcCars_.begin(), npcCars_.end(), [&](const NpcCar& c) {
                float dx = c.position.x - spawnX_;
                float dy = c.position.y - spawnY_;
                return (dx * dx + dy * dy) < clearR * clearR;
                }),
            npcCars_.end()
        );
    }


    // Layout calculation

    void MapSystem::calcLayout() {
        int x = 0;
        for (int j = 0; j < BLOCK_COLS; j++) {
            colStart_[j] = x;
            x += COL_W[j];
            if (j < BLOCK_COLS - 1) { vRoadX_[j] = x; x += ROAD_W; }
        }
        MAP_W_ = x;

        int y = 0;
        for (int i = 0; i < BLOCK_ROWS; i++) {
            rowStart_[i] = y;
            y += ROW_H[i];
            if (i < BLOCK_ROWS - 1) { hRoadY_[i] = y; y += ROAD_W; }
        }
        MAP_H_ = y;

        width_ = MAP_W_;
        height_ = MAP_H_;
    }


    // Helper: road detection

    bool MapSystem::isVRoadCol(int c) const {
        for (int i = 0; i < BLOCK_COLS - 1; i++)
            if (c >= vRoadX_[i] && c < vRoadX_[i] + ROAD_W) return true;
        return false;
    }

    bool MapSystem::isHRoadRow(int r) const {
        for (int i = 0; i < BLOCK_ROWS - 1; i++)
            if (r >= hRoadY_[i] && r < hRoadY_[i] + ROAD_W) return true;
        return false;
    }

    int MapSystem::getVRoadIdx(int c) const {
        for (int i = 0; i < BLOCK_COLS - 1; i++)
            if (c >= vRoadX_[i] && c < vRoadX_[i] + ROAD_W) return i;
        return -1;
    }

    int MapSystem::getHRoadIdx(int r) const {
        for (int i = 0; i < BLOCK_ROWS - 1; i++)
            if (r >= hRoadY_[i] && r < hRoadY_[i] + ROAD_W) return i;
        return -1;
    }


    // Tile grid generation - ported from original course_map_1.cpp buildMap()


    // vertical road tile per original vRoadTile
    static TileID s_vRoadTile(int c, int baseCol, int r,
        const int hRoadY_arr[], int numH, int RW)
    {
        auto isHR = [&](int rr) {
            for (int i = 0; i < numH; i++)
                if (rr >= hRoadY_arr[i] && rr < hRoadY_arr[i] + RW) return true;
            return false;
            };
        int local = c - baseCol;
        if (isHR(r)) { return RD_CROSS; }  // ?????? ???? ???? ????
        switch (local) {
        case 0: return RD_PLAIN;
        case 1: return (r % 3 == 0) ? RD_DASH_V : RD_PLAIN;
        case 2: return RD_LANE_V;
        case 3: return (r % 3 == 0) ? RD_DASH_V : RD_PLAIN;
        case 4: return RD_PLAIN;
        }
        return RD_PLAIN;
    }

    // horizontal road tile per original hRoadTile
    static TileID s_hRoadTile(int r, int baseRow, int c,
        const int vRoadX_arr[], int numV, int RW)
    {
        auto isVR = [&](int cc) {
            for (int i = 0; i < numV; i++)
                if (cc >= vRoadX_arr[i] && cc < vRoadX_arr[i] + RW) return true;
            return false;
            };
        int local = r - baseRow;
        if (isVR(c)) { return RD_CROSS; }  // ?????? ???? ???? ????
        switch (local) {
        case 0: return RD_PLAIN;
        case 1: return (c % 3 == 0) ? RD_DASH_H : RD_PLAIN;
        case 2: return RD_LANE_H;
        case 3: return (c % 3 == 0) ? RD_DASH_H : RD_PLAIN;
        case 4: return RD_PLAIN;
        }
        return RD_PLAIN;
    }

    // block interior tile - exact port of original blockTile()
    static TileID s_blockTile(int r, int c, int r0, int r1, int c0, int c1, TileID bldType)
    {
        int lr = r - r0, lc = c - c0;
        int bh = r1 - r0 + 1, bw = c1 - c0 + 1;
        bool edgeTop = (lr == 0), edgeBot = (lr == bh - 1);
        bool edgeLeft = (lc == 0), edgeRight = (lc == bw - 1);
        if (edgeTop || edgeBot || edgeLeft || edgeRight) {
            if ((edgeTop || edgeBot) && (edgeLeft || edgeRight)) return SW_LAMP;
            int seed = (r * 37 + c * 13) % 10;
            if (seed == 0) return SW_TREE;
            if (seed == 1) return SW_BENCH;
            return SW_PLAIN;
        }
        int br = lr - 1, bc = lc - 1;
        int bih = bh - 2, biw = bw - 2;
        switch (bldType) {
        case BLD_APART: {
            if (biw >= 6 && bih >= 5) {
                bool zone = (bc < biw / 2 && br < bih * 2 / 3) || (bc >= biw / 2 + 1 && br >= bih / 3);
                return zone ? BLD_APART : NAT_GRASS;
            }
            return (bc < biw * 2 / 3) ? BLD_APART : NAT_GARDEN;
        }
        case BLD_HOUSE: {
            int gr = br / 2, gc = bc / 2;
            if ((gr + gc) % 3 == 2) return NAT_TREE;
            if (br % 2 == 1 || bc % 2 == 1) return NAT_GRASS;
            return BLD_HOUSE;
        }
        case BLD_SHOP: {
            if (bih >= 5) {
                if (br < bih / 3 + 1) return (bc % 3 < 2) ? BLD_SHOP : NAT_GRASS;
                return BLD_PARKING;
            }
            return (br == 0) ? BLD_SHOP : ((bc % 3 == 0) ? BLD_PARKING : NAT_GRASS);
        }
        case BLD_OFFICE: {
            if (biw >= 6 && bih >= 5) {
                if (bc < biw * 3 / 5 && br < bih * 3 / 4) return BLD_OFFICE;
                if (br >= bih * 3 / 4 + 1) return BLD_PARKING;
                return NAT_GRASS;
            }
            if (bc < biw * 2 / 3 && br < bih * 2 / 3) return BLD_OFFICE;
            return (br >= bih * 2 / 3) ? BLD_PARKING : NAT_GRASS;
        }
        case BLD_PARK: {
            float fr = (float)br / bih, fc = (float)bc / biw;
            float dist = (fc - 0.5f) * (fc - 0.5f) + (fr - 0.5f) * (fr - 0.5f);
            if (dist < 0.06f) return BLD_PARK;
            if ((br + bc) % 4 == 0) return NAT_TREE;
            return NAT_GRASS;
        }
        case BLD_PARKING: return BLD_PARKING;
        default: return NAT_GRASS;
        }
    }

    void MapSystem::buildTileGrid() {
        tileGrid_.assign(MAP_H_, std::vector<TileID>(MAP_W_, SW_PLAIN));
        // 1) Vertical roads
        for (int i = 0; i < BLOCK_COLS - 1; i++) {
            int base = vRoadX_[i];
            for (int r = 0; r < MAP_H_; r++)
                for (int c = base; c < base + ROAD_W && c < MAP_W_; c++)
                    tileGrid_[r][c] = s_vRoadTile(c, base, r, hRoadY_, BLOCK_ROWS - 1, ROAD_W);
        }
        // 2) Horizontal roads (overwrite intersections)
        for (int i = 0; i < BLOCK_ROWS - 1; i++) {
            int base = hRoadY_[i];
            for (int c = 0; c < MAP_W_; c++)
                for (int r = base; r < base + ROAD_W && r < MAP_H_; r++) {
                    if (isVRoadCol(c)) tileGrid_[r][c] = RD_CROSS;  // ?????? ???? ???? ????
                    else tileGrid_[r][c] = s_hRoadTile(r, base, c, vRoadX_, BLOCK_COLS - 1, ROAD_W);
                }
        }
        // 3) Crosswalks
        for (int vi = 0; vi < BLOCK_COLS - 1; vi++) {
            int vb = vRoadX_[vi];
            for (int hi = 0; hi < BLOCK_ROWS - 1; hi++) {
                int hb = hRoadY_[hi];
                int he = hb + ROAD_W - 1;

                // buildTileGrid() ???? Crosswalks ????? ??????? ????
                if (hb > 0)
                    for (int c = vb; c <= vb + ROAD_W - 1 && c < MAP_W_; c++)
                        tileGrid_[hb - 1][c] = RD_CWALK_V;

                if (he + 1 < MAP_H_)
                    for (int c = vb; c <= vb + ROAD_W - 1 && c < MAP_W_; c++)
                        tileGrid_[he + 1][c] = RD_CWALK_V;

                if (vb > 0)
                    for (int r = hb; r <= he && r < MAP_H_; r++)
                        tileGrid_[r][vb - 1] = RD_CWALK_H;

                if (vb + ROAD_W < MAP_W_)
                    for (int r = hb; r <= he && r < MAP_H_; r++)
                        tileGrid_[r][vb + ROAD_W] = RD_CWALK_H;
            }
        }
        // 4) Fill blocks
        for (int bi = 0; bi < BLOCK_ROWS; bi++)
            for (int bj = 0; bj < BLOCK_COLS; bj++) {
                int r0 = rowStart_[bi], c0 = colStart_[bj];
                int r1 = r0 + ROW_H[bi] - 1, c1 = c0 + COL_W[bj] - 1;
                TileID bt = BLOCK_FILLS[bi][bj];
                for (int r = r0; r <= r1 && r < MAP_H_; r++)
                    for (int c = c0; c <= c1 && c < MAP_W_; c++)
                        tileGrid_[r][c] = s_blockTile(r, c, r0, r1, c0, c1, bt);
            }
    }


    // TileID to RC_* mapping

    int MapSystem::tileToRc(TileID t) {
        switch (t) {
        case RD_PLAIN:      return RC_ROAD;
        case RD_LANE_H:     return RC_CLINE;
        case RD_LANE_V:     return RC_CLINE;
        case RD_DASH_H:     return RC_ROAD;
        case RD_DASH_V:     return RC_ROAD;
        case RD_CROSS:      return RC_ROAD;
        case RD_CWALK_H:    return RC_XWALK;
        case RD_CWALK_V:    return RC_XWALK;
        case RD_SHOULDER:   return RC_ROAD;
        case SW_PLAIN:      return RC_CURB;
        case SW_CURB:       return RC_CURB;
        case SW_LAMP:       return RC_CURB;
        case SW_BENCH:      return RC_CURB;
        case SW_TREE:       return RC_CURB;
        case BLD_APART:     return RC_BLDG;
        case BLD_HOUSE:     return RC_BLDG;
        case BLD_SHOP:      return RC_BLDG;
        case BLD_OFFICE:    return RC_BLDG;
        case BLD_PARK:      return RC_GRASS;
        case BLD_PARKING:   return RC_BLDG;
        case BLD_ON_PARK_V: return RC_BLDG;
        case BLD_ON_PARK_H: return RC_BLDG;
        case NAT_GRASS:     return RC_GRASS;
        case NAT_TREE:      return RC_GRASS;
        case NAT_GARDEN:    return RC_GRASS;
        case NAT_POND:      return RC_GRASS;
            // Course B building types
        case BLD_VILLA:     return RC_BLDG;
        case BLD_HIGHRISE:  return RC_BLDG;
        case BLD_CAFE:      return RC_BLDG;
        case BLD_CONVENIENCE:return RC_BLDG;
        case BLD_RESTAURANT:return RC_BLDG;
        case BLD_MART:      return RC_BLDG;
        case BLD_HOTEL:     return RC_BLDG;
        case BLD_HOSPITAL:  return RC_BLDG;
        case BLD_SCHOOL:    return RC_BLDG;
        case BLD_POLICE:    return RC_BLDG;
        case BLD_FIRESTATION:return RC_BLDG;
        case BLD_GASSTATION:return RC_BLDG;
        case BLD_STADIUM:   return RC_BLDG;
        case RD_ROUNDABOUT: return RC_ROAD;
        case RD_DIAG:       return RC_ROAD;
        case RD_MERGE:      return RC_ROAD;
        case SW_FLOWER:     return RC_CURB;
        case SW_PERSON:     return RC_CURB;
        case SW_DOG:        return RC_CURB;
            // Parking tiles
        case PK_ZONE:       return RC_ROAD;
        case PK_STRIPE:     return RC_ROAD;
        case PK_LINE_V:     return RC_PARKLINE;
        case PK_LINE_H:     return RC_PARKLINE;
        case PK_EMPTY:      return RC_ROAD;
        case PK_CENTER:     return RC_CLINE;
        case PK_DASH:       return RC_ROAD;
        case PK_CURB_CORNER:return RC_CURB;
        case CAR_Y:         return RC_YCAR;
        case CAR_W:         return RC_WCAR;
        case CAR_R:         return RC_YCAR;
        case CAR_BL:        return RC_YCAR;
        case CAR_G:         return RC_YCAR;
        case CAR_GY:        return RC_WCAR;
        case CAR_P:         return RC_YCAR;
        case CAR_BK:        return RC_YCAR;
        case CAR_GLASS:     return RC_CWINDOW;
        default:            return RC_ROAD;
        }
    }


    // RC grid build (game logic)

    void MapSystem::buildRcGrid() {
        grid_.assign(MAP_H_, std::vector<int>(MAP_W_, RC_ROAD));
        for (int r = 0; r < MAP_H_; r++)
            for (int c = 0; c < MAP_W_; c++)
                grid_[r][c] = tileToRc(tileGrid_[r][c]);
    }


    // Auto-generate checkpoints

    void MapSystem::buildCheckpoints() {
        checkpoints_.clear();
        int id = 1;
        // Horizontal road NPCs
        for (int hi = 0; hi < BLOCK_ROWS - 1; hi++) {
            float cy = (float)(hRoadY_[hi] + 3) + 0.5f;
            // at 1/3, 2/3 vertical road centers
            int v1 = BLOCK_COLS / 3;
            int v2 = (BLOCK_COLS * 2) / 3;
            if (v1 >= BLOCK_COLS - 1) v1 = BLOCK_COLS - 2;
            if (v2 >= BLOCK_COLS - 1) v2 = BLOCK_COLS - 2;

            Checkpoint cp1, cp2;
            cp1.id = id++;
            cp1.position = Vec2((float)(vRoadX_[v1] + ROAD_W / 2) + 0.5f, cy);
            cp1.radius = 4.0f;
            cp1.passed = false;
            checkpoints_.push_back(cp1);

            cp2.id = id++;
            cp2.position = Vec2((float)(vRoadX_[v2] + ROAD_W / 2) + 0.5f, cy);
            cp2.radius = 4.0f;
            cp2.passed = false;
            checkpoints_.push_back(cp2);
        }
    }


    // NPC car initialization

    void MapSystem::initNpcCars() {
        npcCars_.clear();
        srand((unsigned)time(NULL));

        const int NC = 8;
        static const int COLORS[8][3] = {
            {220,60,60},{60,120,220},{60,180,60},{220,180,60},
            {180,60,220},{220,140,60},{220,220,220},{60,60,60}
        };
        int ci = 0;

        // Horizontal road NPCs ? offset from center line toward lane edge
        // between intersections (mid-block X, not at block edges)
        for (int hi = 0; hi < BLOCK_ROWS - 1 && (int)npcCars_.size() < 5; hi++) {
            float ry3 = (float)(hRoadY_[hi] + 3) + 0.75f;   // right lane, away from center
            float ry1 = (float)(hRoadY_[hi] + 1) + 0.25f;           // left lane, away from center
            // Spawn between vRoadX_[1] and vRoadX_[2] (mid-map)
            //float cx = (float)(vRoadX_[1] + ROAD_W) + (float)(COL_W[2] / 2);
            float spawnRightX = -2.0f;
            float sp = 3.5f + (rand() % 10) * 0.1f;
            const int* c1 = COLORS[ci++ % NC];
            npcCars_.push_back(NpcCar(spawnRightX, ry3, NPC_RIGHT, sp, c1[0], c1[1], c1[2]));
            if ((int)npcCars_.size() >= 5) break;
            // LEFT car: start further right in the map
            float spawnLeftX = (float)MAP_W_ + 2.0f;
            const int* c2 = COLORS[ci++ % NC];
            npcCars_.push_back(NpcCar(spawnLeftX, ry1, NPC_LEFT, sp, c2[0], c2[1], c2[2]));
        }

        // Vertical road NPCs ? offset from center line toward lane edge
        // between intersections (mid-block Y)
        // Vertical road NPCs - spawn from map top/bottom edges
        for (int vi = 1; vi < BLOCK_COLS - 1 && (int)npcCars_.size() < 10; vi += 2) {
            float cx1 = (float)(vRoadX_[vi] + 1) + 0.25f; // down lane, same lane
            float cx3 = (float)(vRoadX_[vi] + 3) + 0.75f; // up lane, same lane
            float sp = 3.5f + (rand() % 10) * 0.1f;

            // DOWN car: spawn from the top edge of the map
            float spawnDownY = -2.0f;
            const int* c1 = COLORS[ci++ % NC];
            npcCars_.push_back(NpcCar(cx1, spawnDownY, NPC_DOWN, sp, c1[0], c1[1], c1[2]));
            if ((int)npcCars_.size() >= 10) break;
            // UP car: spawn from the bottom edge of the map
            float spawnUpY = (float)MAP_H_ + 2.0f;
            const int* c2 = COLORS[ci++ % NC];
            npcCars_.push_back(NpcCar(cx3, spawnUpY, NPC_UP, sp, c2[0], c2[1], c2[2]));
        }

        // Remove NPCs near spawn
        const float clearR2 = 15.0f * 15.0f;
        for (int k = (int)npcCars_.size() - 1; k >= 0; --k) {
            float dx = npcCars_[k].position.x - spawnX_;
            float dy = npcCars_[k].position.y - spawnY_;
            if (dx * dx + dy * dy < clearR2)
                npcCars_.erase(npcCars_.begin() + k);
        }
    }


    // NPC car update (dt: seconds)

    void MapSystem::updateNpcCars(float dt, const std::vector<TrafficSignal>* signals, const VehicleState* player) {
        if (npcDelay_ > 0.0f) { npcDelay_ -= dt; return; }

        const bool isCourseB = hasRoundabout_;
        const float safeGap = isCourseB ? 5.2f : 6.5f;
        const float stopDist = isCourseB ? 4.4f : 5.0f;

        // Roundabout parameters
        // Island visual radius = 3.75 * 0.8 = 3.0,  tiles cleared to d <= 5.0
        // Orbit at 4.4 = just outside island, inside cleared road area
        const float orbR = 4.4f;
        // Lateral tolerance: how close to the road centerline the car must
        // be (perpendicular to travel direction) to be considered "on course"
        // for the roundabout intersection. ROAD_W/2 = 2.5
        const float laneTol = isCourseB ? 3.0f : 2.8f;

        auto normA = [](float a) -> float {
            while (a > (float)M_PI)  a -= 2.0f * (float)M_PI;
            while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
            return a;
            };

        auto dirToHeading = [](NpcDir d) -> float {
            switch (d) {
            case NPC_RIGHT: return 0.0f;
            case NPC_LEFT:  return (float)M_PI;
            case NPC_DOWN:  return (float)M_PI * 0.5f;
            case NPC_UP:    return -(float)M_PI * 0.5f;
            }
            return 0.0f;
            };

        // Exit angle from center for each direction.
        //
        // Right-hand traffic: the car must exit on the right-hand lane of
        // its outgoing road. With screen coordinates (+y = down) and an
        // orbit radius of 3.8, the right-lane exit point for each direction
        // is on the circle and at a lateral offset of 1.5 from the road
        // centerline (which passes through the roundabout center).
        //
        // Solving r*sin(theta) for the lateral target (and r*cos(theta) for
        // the along-axis target) gives:
        //
        // With orbR = 4.4 and laneOffset = 1.5:
        //   sin(theta) = 1.5 / 4.4 = 0.341,  x = sqrt(19.36 - 2.25) = 4.14
        //
        // Right-hand driving lane targets (screen coords, +y = down):
        //   NPC_RIGHT (east)  : bottom lane, y = center + 1.5
        //   NPC_LEFT  (west)  : top lane,    y = center - 1.5
        //   NPC_DOWN  (south) : left lane,   x = center - 1.5
        //   NPC_UP    (north) : right lane,  x = center + 1.5
        //
        //   NPC_RIGHT : exit at (+4.14, +1.5)  -> theta = atan2( +1.5, +4.14) =  +0.349
        //   NPC_LEFT  : exit at (-4.14, -1.5)  -> theta = atan2( -1.5, -4.14) =  -2.793
        //   NPC_DOWN  : exit at (-1.5,  +4.14) -> theta = atan2( +4.14,-1.5)  =  +1.920
        //   NPC_UP    : exit at (+1.5,  -4.14) -> theta = atan2( -4.14,+1.5)  =  -1.222
        auto dirToExitAngle = [](NpcDir d) -> float {
            switch (d) {
            case NPC_RIGHT: return  0.349f;
            case NPC_LEFT:  return -2.793f;
            case NPC_DOWN:  return  1.920f;
            case NPC_UP:    return -1.222f;
            }
            return 0.0f;
            };

        auto absDot = [](const Vec2& a, const Vec2& b) -> float {
            return std::fabs(a.x * b.x + a.y * b.y);
            };

        // OBB-ish proximity test: would moving an NPC to (testX, testY) overlap the player?
        // B코스는 판정을 조금 더 느슨하게 해서 "스치기만 해도 막히는" 느낌을 줄임.
        auto npcWouldHitPlayer = [&](float testX, float testY, const NpcCar& movingCar) -> bool {
            if (!player) return false;

            Vec2 pForward(std::cos(player->heading), std::sin(player->heading));
            float pLen = std::sqrt(pForward.x * pForward.x + pForward.y * pForward.y);
            if (pLen > 0.00001f) { pForward.x /= pLen; pForward.y /= pLen; }
            else { pForward = Vec2(1.0f, 0.0f); }

            Vec2 pRight(-pForward.y, pForward.x);
            const float pForwardOffset = isCourseB ? 0.12f : 0.22f;
            const float pHalfX = isCourseB ? 1.10f : 1.32f;
            const float pHalfY = isCourseB ? 0.58f : 0.70f;

            Vec2 pCenter(
                player->position.x + pForward.x * pForwardOffset,
                player->position.y + pForward.y * pForwardOffset
            );

            Vec2 npcAxisX, npcAxisY;
            float npcHalfX = isCourseB ? 1.08f : 1.30f;
            float npcHalfY = isCourseB ? 0.58f : 0.68f;
            if (movingCar.dir == NPC_LEFT || movingCar.dir == NPC_RIGHT) {
                npcAxisX = Vec2(1.0f, 0.0f); npcAxisY = Vec2(0.0f, 1.0f);
            }
            else {
                npcAxisX = Vec2(0.0f, 1.0f); npcAxisY = Vec2(1.0f, 0.0f);
            }

            Vec2 diff(testX - pCenter.x, testY - pCenter.y);
            float projForward = std::fabs(diff.x * pForward.x + diff.y * pForward.y);
            float projRight = std::fabs(diff.x * pRight.x + diff.y * pRight.y);
            float npcRadiusOnForward = absDot(npcAxisX, pForward) * npcHalfX + absDot(npcAxisY, pForward) * npcHalfY;
            float npcRadiusOnRight = absDot(npcAxisX, pRight) * npcHalfX + absDot(npcAxisY, pRight) * npcHalfY;

            const float clearanceForward = isCourseB ? 0.16f : 0.0f;
            const float clearanceRight = isCourseB ? 0.10f : 0.0f;

            if (projForward <= pHalfX + npcRadiusOnForward - clearanceForward &&
                projRight <= pHalfY + npcRadiusOnRight - clearanceRight)
                return true;

            return false;
            };

        for (size_t i = 0; i < npcCars_.size(); ++i) {
            NpcCar& car = npcCars_[i];

            // --- Respawn waiting ---
            if (car.waiting) {
                car.respawnTimer -= dt;
                if (car.respawnTimer <= 0.0f) {
                    car.waiting = false;
                    car.position.x = car.spawnX;
                    car.position.y = car.spawnY;
                    car.dir = car.spawnDir;
                    car.inRoundabout = false;
                    car.exitAlignTimer = 0.0f;
                    switch (car.dir) {
                    case NPC_RIGHT: car.heading = 0.0f; break;
                    case NPC_LEFT:  car.heading = (float)M_PI; break;
                    case NPC_DOWN:  car.heading = (float)M_PI * 0.5f; break;
                    case NPC_UP:    car.heading = -(float)M_PI * 0.5f; break;
                    }
                }
                continue; // skip all logic while waiting
            }

            if (car.speed <= 0.0f) continue;

            // ========================================================
            // ROUNDABOUT MODE
            // ========================================================
            if (car.inRoundabout && hasRoundabout_) {
                float curDx = car.position.x - roundaboutCenter_.x;
                float curDy = car.position.y - roundaboutCenter_.y;
                float curR = std::sqrt(curDx * curDx + curDy * curDy);
                if (curR < 0.1f) curR = 0.1f;

                float angularSpeed = car.speed / curR;

                car.orbAngle -= angularSpeed * dt;
                car.orbAngle = normA(car.orbAngle);

                float smoothR = curR + (orbR - curR) * std::min(1.0f, 2.0f * dt);

                car.position.x = roundaboutCenter_.x + smoothR * std::cos(car.orbAngle);
                car.position.y = roundaboutCenter_.y + smoothR * std::sin(car.orbAngle);

                float targetHeading = car.orbAngle - (float)M_PI * 0.5f;
                float headingDiff = normA(targetHeading - car.heading);
                car.heading = normA(car.heading + headingDiff * std::min(1.0f, 2.5f * dt));

                float diff = normA(car.orbAngle - car.exitAngle);
                float window = angularSpeed * dt * 2.5f + 0.12f;
                if (std::fabs(diff) < window) {
                    car.inRoundabout = false;
                    car.dir = car.exitDir;
                    car.exitAlignTimer = 2.0f;

                    car.position.x = roundaboutCenter_.x + orbR * std::cos(car.exitAngle);
                    car.position.y = roundaboutCenter_.y + orbR * std::sin(car.exitAngle);
                }
                continue;
            }

            // ========================================================
            // POST-EXIT HEADING ALIGNMENT
            // ========================================================
            if (car.exitAlignTimer > 0.0f) {
                car.exitAlignTimer -= dt;
                float targetH = dirToHeading(car.dir);
                float hdiff = normA(targetH - car.heading);
                car.heading = normA(car.heading + hdiff * std::min(1.0f, 3.0f * dt));
            }

            // ========================================================
            // CHECK ROUNDABOUT ENTRY
            // ========================================================
            if (hasRoundabout_) {
                float dx = car.position.x - roundaboutCenter_.x;
                float dy = car.position.y - roundaboutCenter_.y;
                float dist = std::sqrt(dx * dx + dy * dy);

                float lateral = 0.0f;
                float ahead = 0.0f;
                bool headingToward = false;

                switch (car.dir) {
                case NPC_RIGHT:
                    ahead = roundaboutCenter_.x - car.position.x;
                    lateral = std::fabs(car.position.y - roundaboutCenter_.y);
                    headingToward = (ahead > 0.0f);
                    break;
                case NPC_LEFT:
                    ahead = car.position.x - roundaboutCenter_.x;
                    lateral = std::fabs(car.position.y - roundaboutCenter_.y);
                    headingToward = (ahead > 0.0f);
                    break;
                case NPC_DOWN:
                    ahead = roundaboutCenter_.y - car.position.y;
                    lateral = std::fabs(car.position.x - roundaboutCenter_.x);
                    headingToward = (ahead > 0.0f);
                    break;
                case NPC_UP:
                    ahead = car.position.y - roundaboutCenter_.y;
                    lateral = std::fabs(car.position.x - roundaboutCenter_.x);
                    headingToward = (ahead > 0.0f);
                    break;
                }

                if (headingToward && ahead < orbR + 1.5f && ahead > 0.0f && lateral < laneTol) {
                    car.inRoundabout = true;
                    car.orbAngle = std::atan2(dy, dx);
                    car.exitDir = car.dir;
                    car.exitAngle = dirToExitAngle(car.exitDir);
                    continue;
                }
            }

            // ========================================================
            // STRAIGHT-LINE DRIVING
            // ========================================================

            // 1. front car gap check
            bool blocked = false;
            float nearestFrontDist = 999999.0f;
            const float sameLaneTolNpc = isCourseB ? 0.95f : 1.30f;
            const float sameLaneTolPlayer = isCourseB ? 0.90f : 1.20f;

            for (size_t j = 0; j < npcCars_.size(); ++j) {
                if (i == j) continue;
                const NpcCar& other = npcCars_[j];
                if (other.inRoundabout) continue;
                if (car.dir != other.dir) continue;

                float aheadD = 0.0f, lateralD = 0.0f;
                switch (car.dir) {
                case NPC_RIGHT: aheadD = other.position.x - car.position.x; lateralD = std::fabs(other.position.y - car.position.y); break;
                case NPC_LEFT:  aheadD = car.position.x - other.position.x; lateralD = std::fabs(other.position.y - car.position.y); break;
                case NPC_DOWN:  aheadD = other.position.y - car.position.y; lateralD = std::fabs(other.position.x - car.position.x); break;
                case NPC_UP:    aheadD = car.position.y - other.position.y; lateralD = std::fabs(other.position.x - car.position.x); break;
                }

                if (aheadD > 0.0f && lateralD < sameLaneTolNpc) {
                    if (aheadD < nearestFrontDist) nearestFrontDist = aheadD;
                    if (aheadD < safeGap) blocked = true;
                }
            }

            // 1b. player as front car
            if (!blocked && player) {
                float aheadD = 0.0f, lateralD = 0.0f;
                switch (car.dir) {
                case NPC_RIGHT: aheadD = player->position.x - car.position.x; lateralD = std::fabs(player->position.y - car.position.y); break;
                case NPC_LEFT:  aheadD = car.position.x - player->position.x; lateralD = std::fabs(player->position.y - car.position.y); break;
                case NPC_DOWN:  aheadD = player->position.y - car.position.y; lateralD = std::fabs(player->position.x - car.position.x); break;
                case NPC_UP:    aheadD = car.position.y - player->position.y; lateralD = std::fabs(player->position.x - car.position.x); break;
                }
                if (aheadD > 0.0f && lateralD < sameLaneTolPlayer) {
                    if (aheadD < nearestFrontDist) nearestFrontDist = aheadD;
                    if (aheadD < safeGap) blocked = true;
                }
            }

            if (blocked) continue;

            // 2. traffic light check
            if (signals && !signals->empty()) {
                const float stopMargin = isCourseB ? 0.8f : 1.0f;
                const float stopSnapEpsilon = 0.03f;
                const float yellowCommitDist = isCourseB ? 1.8f : 1.2f;
                const float signalLaneTol = isCourseB ? 0.95f : 1.5f;
                const float signalSearchAhead = stopDist + stopMargin + (isCourseB ? 0.6f : 1.0f);

                bool npcIsHorizontal = (car.dir == NPC_RIGHT || car.dir == NPC_LEFT);

                float minStopAhead = 999999.0f;
                float bestSignalScore = 999999.0f;
                TrafficLight st = TrafficLight::Green;
                bool found = false;

                for (const auto& sig : *signals) {
                    if (sig.isHorizontal != npcIsHorizontal) continue;

                    float aheadS = 0.0f, lateralS = 0.0f;
                    float stopAhead = 999999.0f;

                    switch (car.dir) {
                    case NPC_RIGHT:
                        aheadS = sig.position.x - car.position.x;
                        lateralS = std::fabs(sig.position.y - car.position.y);
                        stopAhead = (sig.position.x - stopMargin) - car.position.x;
                        break;
                    case NPC_LEFT:
                        aheadS = car.position.x - sig.position.x;
                        lateralS = std::fabs(sig.position.y - car.position.y);
                        stopAhead = car.position.x - (sig.position.x + stopMargin);
                        break;
                    case NPC_DOWN:
                        aheadS = sig.position.y - car.position.y;
                        lateralS = std::fabs(sig.position.x - car.position.x);
                        stopAhead = (sig.position.y - stopMargin) - car.position.y;
                        break;
                    case NPC_UP:
                        aheadS = car.position.y - sig.position.y;
                        lateralS = std::fabs(sig.position.x - car.position.x);
                        stopAhead = car.position.y - (sig.position.y + stopMargin);
                        break;
                    }

                    if (aheadS > 0.0f &&
                        aheadS < signalSearchAhead &&
                        lateralS < signalLaneTol &&
                        stopAhead >= 0.0f) {

                        float score = stopAhead + lateralS * 2.5f;
                        if (score < bestSignalScore) {
                            bestSignalScore = score;
                            minStopAhead = stopAhead;
                            st = sig.state;
                            found = true;
                        }
                    }
                }

                if (found) {
                    bool mustStop = false;

                    if (st == TrafficLight::Red) {
                        mustStop = true;
                    }
                    else if (st == TrafficLight::Yellow) {
                        bool hasFrontCarNear = (nearestFrontDist < safeGap + 0.8f);
                        if (hasFrontCarNear) {
                            mustStop = true;
                        }
                        else if (minStopAhead > yellowCommitDist) {
                            mustStop = true;
                        }
                    }

                    if (mustStop) {
                        if (minStopAhead > stopSnapEpsilon) {
                            float moveDist = car.speed * dt;
                            if (moveDist > minStopAhead) moveDist = minStopAhead;

                            float prevX = car.position.x, prevY = car.position.y;
                            switch (car.dir) {
                            case NPC_RIGHT: car.position.x += moveDist; break;
                            case NPC_LEFT:  car.position.x -= moveDist; break;
                            case NPC_DOWN:  car.position.y += moveDist; break;
                            case NPC_UP:    car.position.y -= moveDist; break;
                            }
                            if (npcWouldHitPlayer(car.position.x, car.position.y, car)) {
                                car.position.x = prevX; car.position.y = prevY;
                            }
                            car.heading = dirToHeading(car.dir);
                        }
                        continue;
                    }
                }
            }

            // 3. intersection collision avoidance
            // Only block if the other car is genuinely crossing our
            // path: it must be close ahead on OUR forward axis
            // AND laterally overlapping on our cross axis.
            if (car.dir == NPC_DOWN || car.dir == NPC_UP) {
                bool crossBlocked = false;
                const float crossAheadTol = isCourseB ? 1.8f : 2.5f;
                const float crossLaneTol = isCourseB ? 1.2f : 1.8f;

                for (size_t j = 0; j < npcCars_.size(); ++j) {
                    if (i == j) continue;
                    const NpcCar& other2 = npcCars_[j];
                    if (other2.dir != NPC_RIGHT && other2.dir != NPC_LEFT) continue;
                    float cdx = std::fabs(other2.position.x - car.position.x);
                    float cdy = std::fabs(other2.position.y - car.position.y);
                    if (cdy < crossAheadTol && cdx < crossLaneTol) { crossBlocked = true; break; }
                }
                if (crossBlocked) continue;
            }

            // 4. movement ? straight line only
            float nextX = car.position.x, nextY = car.position.y;
            switch (car.dir) {
            case NPC_RIGHT: nextX += car.speed * dt; break;
            case NPC_LEFT:  nextX -= car.speed * dt; break;
            case NPC_DOWN:  nextY += car.speed * dt; break;
            case NPC_UP:    nextY -= car.speed * dt; break;
            }

            if (npcWouldHitPlayer(nextX, nextY, car)) continue;

            car.position.x = nextX;
            car.position.y = nextY;
            if (car.exitAlignTimer <= 0.0f) {
                car.heading = dirToHeading(car.dir);
            }

            // Boundary check: despawn and start respawn timer
            bool outOfBounds = false;
            if (car.dir == NPC_LEFT && car.position.x < -2.0f) outOfBounds = true;
            if (car.dir == NPC_RIGHT && car.position.x > (float)MAP_W_ + 2.0f) outOfBounds = true;
            if (car.dir == NPC_UP && car.position.y < -2.0f) outOfBounds = true;
            if (car.dir == NPC_DOWN && car.position.y > (float)MAP_H_ + 2.0f) outOfBounds = true;

            if (outOfBounds) {
                car.waiting = true;
                car.respawnTimer = 3.0f;
                // Move off-screen so it's not visible during wait
                car.position.x = -999.0f;
                car.position.y = -999.0f;
            }
        }
    }


    // Queries

    TileID MapSystem::getTileId(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return NAT_GRASS;
        if ((int)tileGrid_.size() <= y || (int)tileGrid_[y].size() <= x) return NAT_GRASS;
        return tileGrid_[y][x];
    }

    CellType MapSystem::getCellAt(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return CellType::Wall;
        int raw = grid_[y][x];
        switch (raw) {
        case RC_ROAD:  return CellType::Road;
        case RC_WALL:  return CellType::Wall;
        case RC_CURB:  return CellType::Curb;
        case RC_GRASS: return CellType::Grass;
        case RC_CLINE: return CellType::CenterLine;
        case RC_PARK:  return CellType::ParkingSlot;
        case RC_BLDG:  return CellType::Building;
        case RC_STOP:  return CellType::StopLine;
        case RC_XWALK: return CellType::Crosswalk;
        case RC_BUMP:  return CellType::SpeedBump;
        case RC_YCAR: case RC_WCAR: case RC_CWINDOW: return CellType::Wall;
        case RC_PARKLINE: return CellType::Road;
        default:       return CellType::Road;
        }
    }

    bool MapSystem::isInBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

    bool MapSystem::isPassable(int x, int y) const {
        int c = getRawCell(x, y);
        return c == RC_ROAD || c == RC_CLINE || c == RC_PARK || c == RC_STOP || c == RC_XWALK || c == RC_BUMP || c == RC_PARKLINE;
    }

    int MapSystem::getRawCell(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return RC_WALL;
        return grid_[y][x];
    }


    // Course B: layout helpers

    void MapSystem::calcLayoutB() {
        int x = 0;
        for (int j = 0; j < BLOCK_COLS; j++) {
            colStartB_[j] = x;
            x += COL_W_B[j];
            if (j < BLOCK_COLS - 1) { vRoadXB_[j] = x; x += ROAD_W; }
        }
        MAP_W_B_ = x;

        int y = 0;
        for (int i = 0; i < BLOCK_ROWS; i++) {
            rowStartB_[i] = y;
            y += ROW_H_B[i];
            if (i < BLOCK_ROWS - 1) { hRoadYB_[i] = y; y += ROAD_W; }
        }
        MAP_H_B_ = y;

        width_ = MAP_W_B_;
        height_ = MAP_H_B_;
    }

    bool MapSystem::isVRoadColB(int c) const {
        for (int i = 0; i < BLOCK_COLS - 1; i++)
            if (c >= vRoadXB_[i] && c < vRoadXB_[i] + ROAD_W) return true;
        return false;
    }
    bool MapSystem::isHRoadRowB(int r) const {
        for (int i = 0; i < BLOCK_ROWS - 1; i++)
            if (r >= hRoadYB_[i] && r < hRoadYB_[i] + ROAD_W) return true;
        return false;
    }
    int MapSystem::getVRoadIdxB(int c) const {
        for (int i = 0; i < BLOCK_COLS - 1; i++)
            if (c >= vRoadXB_[i] && c < vRoadXB_[i] + ROAD_W) return i;
        return -1;
    }
    int MapSystem::getHRoadIdxB(int r) const {
        for (int i = 0; i < BLOCK_ROWS - 1; i++)
            if (r >= hRoadYB_[i] && r < hRoadYB_[i] + ROAD_W) return i;
        return -1;
    }

    void MapSystem::buildTileGridB() {
        tileGrid_.assign(MAP_H_B_, std::vector<TileID>(MAP_W_B_, SW_PLAIN));

        auto setTileSafe = [&](int r, int c, TileID t) {
            if (r >= 0 && r < MAP_H_B_ && c >= 0 && c < MAP_W_B_) {
                tileGrid_[r][c] = t;
            }
            };

        // 1) Vertical roads
        for (int i = 0; i < BLOCK_COLS - 1; i++) {
            int base = vRoadXB_[i];
            for (int r = 0; r < MAP_H_B_; r++)
                for (int c = base; c < base + ROAD_W && c < MAP_W_B_; c++)
                    tileGrid_[r][c] = s_vRoadTile(c, base, r, hRoadYB_, BLOCK_ROWS - 1, ROAD_W);
        }
        // 2) Horizontal roads
        for (int i = 0; i < BLOCK_ROWS - 1; i++) {
            int base = hRoadYB_[i];
            for (int c = 0; c < MAP_W_B_; c++)
                for (int r = base; r < base + ROAD_W && r < MAP_H_B_; r++) {
                    if (isVRoadColB(c)) tileGrid_[r][c] = RD_CROSS;  // ?????? ???? ???? ????
                    else tileGrid_[r][c] = s_hRoadTile(r, base, c, vRoadXB_, BLOCK_COLS - 1, ROAD_W);
                }
        }

        // 3) Crosswalks
        for (int vi = 0; vi < BLOCK_COLS - 1; vi++) {
            int vb = vRoadXB_[vi];

            for (int hi = 0; hi < BLOCK_ROWS - 1; hi++) {
                int hb = hRoadYB_[hi];
                int he = hb + ROAD_W - 1;

                // ???? ???? ??????
                for (int c = vb; c <= vb + ROAD_W - 1; c++) {
                    setTileSafe(hb - 1, c, RD_CWALK_V);
                }

                // ????? ???? ??????
                for (int c = vb; c <= vb + ROAD_W - 1; c++) {
                    setTileSafe(he + 1, c, RD_CWALK_V);
                }

                // ???? ???? ??????
                for (int r = hb; r <= he; r++) {
                    setTileSafe(r, vb - 1, RD_CWALK_H);
                }

                // ?????? ???? ??????
                for (int r = hb; r <= he; r++) {
                    setTileSafe(r, vb + ROAD_W, RD_CWALK_H);
                }
            }
        }
        // Helper lambdas for block filling
        auto fillSW = [&](int r0, int r1, int c0, int c1) {
            for (int r = r0; r <= r1 && r < MAP_H_B_; r++)
                for (int c = c0; c <= c1 && c < MAP_W_B_; c++) {
                    bool top = r == r0, bot = r == r1, left = c == c0, right = c == c1;
                    bool corner = (top || bot) && (left || right);
                    if (corner) { tileGrid_[r][c] = SW_LAMP; continue; }
                    if (top || bot || left || right) {
                        int s = (r * 31 + c * 17) % 10;
                        if (s == 0) tileGrid_[r][c] = SW_TREE;
                        else if (s == 1) tileGrid_[r][c] = SW_BENCH;
                        else if (s == 2) tileGrid_[r][c] = SW_FLOWER;
                        else tileGrid_[r][c] = SW_PLAIN;
                        continue;
                    }
                }
            };
        auto fillR = [&](int r0, int r1, int c0, int c1, TileID id) {
            for (int r = r0; r <= r1 && r < MAP_H_B_; r++)
                for (int c = c0; c <= c1 && c < MAP_W_B_; c++)
                    tileGrid_[r][c] = id;
            };

        // placeBlock - internal layout per building type (from course_map_2.cpp)
        auto placeBlock = [&](int bi, int bj, TileID btype) {
            int r0 = rowStartB_[bi], r1 = r0 + ROW_H_B[bi] - 1;
            int c0 = colStartB_[bj], c1 = c0 + COL_W_B[bj] - 1;
            fillSW(r0, r1, c0, c1);
            int ir0 = r0 + 1, ir1 = r1 - 1, ic0 = c0 + 1, ic1 = c1 - 1;
            int iw = ic1 - ic0 + 1, ih = ir1 - ir0 + 1;
            if (iw <= 0 || ih <= 0) return;

            switch (btype) {
            case BLD_HOUSE:
                for (int r = ir0; r <= ir1; r++) for (int c = ic0; c <= ic1; c++) {
                    int lr = r - ir0, lc = c - ic0;
                    tileGrid_[r][c] = (lr % 3 == 2 || lc % 3 == 2) ? NAT_GRASS : BLD_HOUSE;
                }
                break;
            case BLD_VILLA:
                fillR(ir0, ir0 + ih / 2 - 1, ic0, ic1, BLD_VILLA);
                fillR(ir0 + ih / 2, ir1, ic0, ic1, BLD_VILLA);
                if (ih > 3) tileGrid_[ir0 + ih / 2][ic0 + iw / 2] = NAT_GRASS;
                break;
            case BLD_APART: {
                int mid = (ic0 + ic1) / 2;
                fillR(ir0, ir1, ic0, mid - 1, BLD_APART);
                fillR(ir0, ir1, mid + 1, ic1, BLD_APART);
                for (int r = ir0; r <= ir1; r++) tileGrid_[r][mid] = NAT_GRASS;
                if (ih >= 4) fillR(ir1 - 1, ir1, ic0, ic1, BLD_PARKING);
                break;
            }
            case BLD_HIGHRISE:
                fillR(ir0, ir0 + ih * 2 / 3, ic0, ic1, BLD_HIGHRISE);
                fillR(ir0 + ih * 2 / 3 + 1, ir1, ic0, ic1, NAT_GARDEN);
                break;
            case BLD_SHOP: {
                TileID shops[] = { BLD_CONVENIENCE,BLD_CAFE,BLD_RESTAURANT,BLD_SHOP };
                for (int r = ir0; r <= ir1; r++) for (int c = ic0; c <= ic1; c++) {
                    int lr = r - ir0, lc = c - ic0;
                    if (lr == 0 || lr == ih - 1) { tileGrid_[r][c] = NAT_GRASS; continue; }
                    tileGrid_[r][c] = shops[(lc / (iw / 4 + 1)) % 4];
                }
                break;
            }
            case BLD_MART:
                fillR(ir0, ir0 + ih / 2, ic0 + 1, ic1 - 1, BLD_MART);
                for (int r = ir0; r <= ir0 + ih / 2; r++) { tileGrid_[r][ic0] = SW_PLAIN; tileGrid_[r][ic1] = SW_PLAIN; }
                if (ir0 + ih / 2 + 1 <= ir1 - 2) for (int r = ir0 + ih / 2 + 1; r <= ir1 - 2; r++) fillR(r, r, ic0, ic1, SW_PLAIN);
                fillR(ir1 - 1, ir1, ic0, ic1, BLD_PARKING);
                break;
            case BLD_HOTEL:
                fillR(ir0, ir1, ic0 + 1, ic1 - 1, BLD_HOTEL);
                for (int r = ir0; r <= ir1; r++) { tileGrid_[r][ic0] = NAT_GARDEN; tileGrid_[r][ic1] = NAT_GARDEN; }
                break;
            case BLD_OFFICE:
                fillR(ir0, ir0 + ih * 3 / 4, ic0, ic1, BLD_OFFICE);
                fillR(ir0 + ih * 3 / 4 + 1, ir1, ic0, ic1, NAT_GARDEN);
                break;
            case BLD_HOSPITAL:
                fillR(ir0, ir1 - 1, ic0 + 1, ic1 - 1, BLD_HOSPITAL);
                fillR(ir1, ir1, ic0, ic1, BLD_PARKING);
                tileGrid_[ir0][ic0] = NAT_GRASS; tileGrid_[ir0][ic1] = NAT_GRASS;
                break;
            case BLD_SCHOOL:
                fillR(ir0, ir0 + ih / 2, ic0 + iw / 2 + 1, ic1, BLD_SCHOOL);
                fillR(ir0, ir1, ic0, ic0 + iw / 2, NAT_GRASS);
                fillR(ir0 + ih / 2 + 1, ir1, ic0 + iw / 2 + 1, ic1, BLD_PARKING);
                break;
            case BLD_PARK:
                for (int r = ir0; r <= ir1; r++) for (int c = ic0; c <= ic1; c++) {
                    int lr = r - ir0, lc = c - ic0;
                    int s = (lr * 7 + lc * 5) % 9;
                    if (s < 2) tileGrid_[r][c] = NAT_TREE;
                    else if (s == 3) tileGrid_[r][c] = NAT_POND;
                    else if (s == 4) tileGrid_[r][c] = NAT_GARDEN;
                    else tileGrid_[r][c] = NAT_GRASS;
                }
                tileGrid_[(ir0 + ir1) / 2][(ic0 + ic1) / 2] = NAT_POND;
                break;
            case BLD_PARKING:
                fillR(ir0, ir1, ic0, ic1, BLD_PARKING);
                break;
            case BLD_GASSTATION:
                fillR(ir0, ir1, ic0, ic1, BLD_GASSTATION);
                break;
            default:
                for (int r = ir0; r <= ir1; r++) for (int c = ic0; c <= ic1; c++) {
                    int s = (r * 11 + c * 7) % 5;
                    tileGrid_[r][c] = (s < 2) ? NAT_GRASS : NAT_TREE;
                }
                break;
            }
            };

        // placeMergedBlock - spans multiple block indices
        auto placeMergedBlock = [&](int bi1, int bi2, int bj1, int bj2, TileID btype) {
            int r0 = rowStartB_[bi1], r1 = rowStartB_[bi2] + ROW_H_B[bi2] - 1;
            int c0 = colStartB_[bj1], c1 = colStartB_[bj2] + COL_W_B[bj2] - 1;
            // Clear internal roads in merged area
            for (int r = r0; r <= r1 && r < MAP_H_B_; r++)
                for (int c = c0; c <= c1 && c < MAP_W_B_; c++)
                    tileGrid_[r][c] = SW_PLAIN;
            // Sidewalk border
            for (int r = r0; r <= r1 && r < MAP_H_B_; r++)
                for (int c = c0; c <= c1 && c < MAP_W_B_; c++) {
                    bool top = r == r0, bot = r == r1, left = c == c0, right = c == c1;
                    if (top || bot || left || right) {
                        bool corner = (top || bot) && (left || right);
                        tileGrid_[r][c] = corner ? SW_LAMP : SW_PLAIN;
                    }
                }
            int ir0 = r0 + 1, ir1 = r1 - 1, ic0 = c0 + 1, ic1 = c1 - 1;
            int iw = ic1 - ic0 + 1, ih = ir1 - ir0 + 1;
            if (iw <= 0 || ih <= 0) return;

            switch (btype) {
            case BLD_STADIUM:
                fillR(ir0, ir1, ic0, ic1, BLD_STADIUM);
                {
                    int fr0 = ir0 + ih / 5, fr1 = ir1 - ih / 5, fc0 = ic0 + iw / 6, fc1 = ic1 - iw / 6;
                    fillR(fr0, fr1, fc0, fc1, NAT_GRASS);
                }
                break;
            case BLD_APART:
                for (int r = ir0; r <= ir1; r++) for (int c = ic0; c <= ic1; c++) {
                    int lr = r - ir0, lc = c - ic0;
                    if (lr >= ih - 2) { tileGrid_[r][c] = BLD_PARKING; continue; }
                    if (lc % (iw / 3) == iw / 3 - 1 || lr % (ih / 3) == ih / 3 - 1) { tileGrid_[r][c] = NAT_GRASS; continue; }
                    tileGrid_[r][c] = BLD_APART;
                }
                break;
            case BLD_PARK:
                for (int r = ir0; r <= ir1; r++) for (int c = ic0; c <= ic1; c++) {
                    int lr = r - ir0, lc = c - ic0;
                    int s = (lr * 5 + lc * 7) % 11;
                    if (s < 3) tileGrid_[r][c] = NAT_TREE;
                    else if (s == 4) tileGrid_[r][c] = NAT_POND;
                    else if (s == 5) tileGrid_[r][c] = NAT_GARDEN;
                    else tileGrid_[r][c] = NAT_GRASS;
                }
                {
                    int mr = (ir0 + ir1) / 2, mc = (ic0 + ic1) / 2;
                    for (int r = ir0; r <= ir1 && r < MAP_H_B_; r++) tileGrid_[r][mc] = SW_PLAIN;
                    for (int c = ic0; c <= ic1 && c < MAP_W_B_; c++) tileGrid_[mr][c] = SW_PLAIN;
                }
                break;
            case BLD_SCHOOL:
                for (int r = ir0; r <= ir1; r++) for (int c = ic0; c <= ic1; c++) {
                    int lc = c - ic0;
                    tileGrid_[r][c] = (lc < iw / 2) ? NAT_GRASS : BLD_SCHOOL;
                }
                break;
            case BLD_MART: {
                int buildBot = ir0 + ih / 2;
                fillR(ir0, buildBot, ic0 + 1, ic1 - 1, BLD_MART);
                for (int r = ir0; r <= buildBot; r++) { tileGrid_[r][ic0] = SW_PLAIN; tileGrid_[r][ic1] = SW_PLAIN; }
                if (buildBot + 1 <= ir1 - 2) fillR(buildBot + 1, ir1 - 2, ic0, ic1, SW_PLAIN);
                fillR(ir1 - 1, ir1, ic0, ic1, BLD_PARKING);
                break;
            }
            default:
                fillR(ir0, ir1, ic0, ic1, btype);
                break;
            }
            };

        // 4) Fill individual blocks
        for (int bi = 0; bi < BLOCK_ROWS; bi++)
            for (int bj = 0; bj < BLOCK_COLS; bj++)
                placeBlock(bi, bj, BLOCK_FILLS_B[bi][bj]);

        // 5) Merged blocks (overwrite individual blocks)
        placeMergedBlock(0, 0, 1, 2, BLD_APART);     // top-left large apartment
        placeMergedBlock(1, 1, 3, 4, BLD_MART);       // center mart
        placeMergedBlock(2, 3, 1, 2, BLD_PARK);       // bottom-left large park
        placeMergedBlock(1, 2, 5, 6, BLD_STADIUM);    // right side stadium
        placeMergedBlock(3, 3, 5, 6, BLD_SCHOOL);     // bottom-right school

        // 6) Specific building placements
        // Gas station in block(0,4)
        {
            int r0 = rowStartB_[0] + 1, r1 = rowStartB_[0] + 3, c0 = colStartB_[4] + 1, c1 = colStartB_[4] + 3;
            fillR(r0, r1, c0, c1, BLD_GASSTATION);
        }
        // Hospital in block(3,3)
        {
            int r0 = rowStartB_[3] + 1, r1 = rowStartB_[3] + ROW_H_B[3] - 2, c0 = colStartB_[3] + 1, c1 = colStartB_[3] + 4;
            fillR(r0, r1, c0, c1, BLD_HOSPITAL);
        }
        // Police in block(3,0)
        {
            int r0 = rowStartB_[3] + 1, r1 = rowStartB_[3] + 2, c0 = colStartB_[0] + 1, c1 = colStartB_[0] + 3;
            fillR(r0, r1, c0, c1, BLD_POLICE);
        }

        // 7) Center lines on horizontal roads
        for (int i = 0; i < BLOCK_ROWS - 1; i++) {
            int centerR = hRoadYB_[i] + 2;
            for (int c = 0; c < MAP_W_B_; c++)
                if (tileGrid_[centerR][c] == RD_PLAIN)
                    tileGrid_[centerR][c] = RD_LANE_H;
        }

        // 8) Roundabout at intersection (hRoadY[0] x vRoadX[2])
        //    Cleared area sized so NPC cars orbiting at radius 4.4
        //    (plus body width ~0.5) fit within passable tiles.
        //    Island center tiles set to SW_CURB so the player car
        //    cannot drive through the island.
        {
            int cr = hRoadYB_[0] + 2, cc = vRoadXB_[2] + 2;
            int ext = 5;
            // First pass: clear the full roundabout area to road
            for (int i = -ext; i <= ext; i++)
                for (int j = -ext; j <= ext; j++) {
                    int rr = cr + i, cc2 = cc + j;
                    if (rr >= 0 && rr < MAP_H_B_ && cc2 >= 0 && cc2 < MAP_W_B_) {
                        float d = std::sqrt((float)(i * i + j * j));
                        if (d <= 5.0f) tileGrid_[rr][cc2] = RD_PLAIN;
                    }
                }
            // Second pass: island center as impassable curb.
            // Visual island radius = 3.75 * 0.8 = 3.0.  Use 2.8 for
            // the tile boundary so it sits just inside the visual circle.
            for (int i = -3; i <= 3; i++)
                for (int j = -3; j <= 3; j++) {
                    int rr = cr + i, cc2 = cc + j;
                    if (rr >= 0 && rr < MAP_H_B_ && cc2 >= 0 && cc2 < MAP_W_B_) {
                        float d = std::sqrt((float)(i * i + j * j));
                        if (d <= 2.8f) tileGrid_[rr][cc2] = SW_CURB;
                    }
                }
            hasRoundabout_ = true;
            roundaboutCenter_ = Vec2((float)cc + 0.5f, (float)cr + 0.5f);
            roundaboutRadius_ = 3.75f;
        }

        // 9) People and dogs on sidewalks: place only on cells strictly inside a block
        //    AND at least 1 tile away from any road row/col. Try a handful of candidates
        //    in corner/edge blocks where the outer perimeter is the map boundary.
        {
            auto cellAdjacentToRoad = [&](int r, int c) -> bool {
                if (r - 1 >= 0 && isHRoadRowB(r - 1)) return true;
                if (r + 1 < MAP_H_B_ && isHRoadRowB(r + 1)) return true;
                if (c - 1 >= 0 && isVRoadColB(c - 1)) return true;
                if (c + 1 < MAP_W_B_ && isVRoadColB(c + 1)) return true;
                return false;
                };
            auto tryPlace = [&](int r, int c, TileID t) -> bool {
                if (r < 0 || r >= MAP_H_B_ || c < 0 || c >= MAP_W_B_) return false;
                if (isHRoadRowB(r) || isVRoadColB(c)) return false;
                if (cellAdjacentToRoad(r, c)) return false;
                if (tileGrid_[r][c] != SW_PLAIN) return false;
                tileGrid_[r][c] = t;
                return true;
                };
            // Top-left corner block (block 0,0): top edge & left edge are map boundary
            tryPlace(0, 2, SW_PERSON); tryPlace(0, 3, SW_DOG);
            // Top-right corner block (block 0, BLOCK_COLS-1)
            tryPlace(0, MAP_W_B_ - 3, SW_PERSON); tryPlace(0, MAP_W_B_ - 2, SW_DOG);
            // Bottom-left corner block (block BLOCK_ROWS-1, 0)
            tryPlace(MAP_H_B_ - 1, 2, SW_PERSON); tryPlace(MAP_H_B_ - 1, 3, SW_DOG);
            // Stadium right perimeter (map-boundary col, mid stadium)
            tryPlace(rowStartB_[1] + ROW_H_B[1] / 2, MAP_W_B_ - 1, SW_PERSON);
            tryPlace(rowStartB_[1] + ROW_H_B[1] / 2 + 1, MAP_W_B_ - 1, SW_DOG);
        }

        // 10) Final scrub: enforce 1-tile gap from roads for decorative tiles.
        //     Catches fillSW lamps/trees/benches/flowers placed on block perimeters
        //     that happen to sit adjacent to a road row/col.
        {
            auto isDecorativeStickout = [](TileID t) -> bool {
                return t == SW_PERSON || t == SW_DOG || t == SW_BENCH ||
                    t == SW_LAMP || t == SW_FLOWER ||
                    t == NAT_TREE || t == NAT_GARDEN;
                };
            auto cellAdjRoad = [&](int r, int c) -> bool {
                if (r - 1 >= 0 && isHRoadRowB(r - 1)) return true;
                if (r + 1 < MAP_H_B_ && isHRoadRowB(r + 1)) return true;
                if (c - 1 >= 0 && isVRoadColB(c - 1)) return true;
                if (c + 1 < MAP_W_B_ && isVRoadColB(c + 1)) return true;
                return false;
                };
            for (int r = 0; r < MAP_H_B_; r++)
                for (int c = 0; c < MAP_W_B_; c++)
                    if (isDecorativeStickout(tileGrid_[r][c]) && cellAdjRoad(r, c))
                        tileGrid_[r][c] = SW_PLAIN;
        }
    }

    void MapSystem::buildRcGridB() {
        grid_.assign(MAP_H_B_, std::vector<int>(MAP_W_B_, RC_ROAD));
        for (int r = 0; r < MAP_H_B_; r++)
            for (int c = 0; c < MAP_W_B_; c++)
                grid_[r][c] = tileToRc(tileGrid_[r][c]);
    }

    void MapSystem::buildCheckpointsB() {
        checkpoints_.clear();
        int id = 1;
        for (int hi = 0; hi < BLOCK_ROWS - 1; hi++) {
            float cy = (float)(hRoadYB_[hi] + 3) + 0.5f;
            int v1 = BLOCK_COLS / 3;
            int v2 = (BLOCK_COLS * 2) / 3;
            if (v1 >= BLOCK_COLS - 1) v1 = BLOCK_COLS - 2;
            if (v2 >= BLOCK_COLS - 1) v2 = BLOCK_COLS - 2;

            Checkpoint cp1, cp2;
            cp1.id = id++;
            cp1.position = Vec2((float)(vRoadXB_[v1] + ROAD_W / 2) + 0.5f, cy);
            cp1.radius = 4.0f;
            cp1.passed = false;
            checkpoints_.push_back(cp1);

            cp2.id = id++;
            cp2.position = Vec2((float)(vRoadXB_[v2] + ROAD_W / 2) + 0.5f, cy);
            cp2.radius = 4.0f;
            cp2.passed = false;
            checkpoints_.push_back(cp2);
        }
    }

    void MapSystem::initNpcCarsB() {
        npcCars_.clear();

        // Route 1 BLUE: hRoad[0] westbound, top lane
        // 시작 위치를 더 오른쪽으로 보내고 속도를 조금 낮춰 초반 압박 완화
        npcCars_.push_back(NpcCar(92.0f, 7.25f, NPC_LEFT, 3.3f, 60, 120, 220));

        // Route 2 RED: vRoad[0] northbound, right lane
        npcCars_.push_back(NpcCar(8.75f, 51.0f, NPC_UP, 3.2f, 220, 60, 60));

        // Route 3 ORANGE: vRoad[2] northbound, right lane
        npcCars_.push_back(NpcCar(34.75f, 51.0f, NPC_UP, 3.3f, 220, 140, 60));

        // Route 4 GREEN: vRoad[4] northbound, right lane
        npcCars_.push_back(NpcCar(63.75f, 51.0f, NPC_UP, 3.2f, 60, 180, 60));
    }


    // generateCourseMapB - Course B (larger city layout)

    void MapSystem::generateCourseMapB() {
        checkpoints_.clear();
        parkingSlots_.clear();
        npcCars_.clear();
        tileGrid_.clear();

        calcLayoutB();
        // Sync MAP_W_/MAP_H_ so updateNpcCars wrapping works correctly
        MAP_W_ = MAP_W_B_;
        MAP_H_ = MAP_H_B_;
        buildTileGridB();
        buildRcGridB();
        buildCheckpointsB();
        initNpcCarsB();

        // hasRoundabout_ set in buildTileGridB
        hasTileGrid_ = true;

        // Spawn: first horizontal road, right lane center, facing east.
        // X = midpoint between vRoadXB_[0] and vRoadXB_[1] (on road,
        // away from crosswalks).  Y = lane center.
        spawnX_ = (float)(vRoadXB_[0] + ROAD_W) + (float)(COL_W_B[1] / 2);
        spawnY_ = (float)(hRoadYB_[0] + 3) + 0.75f;
        spawnHeading_ = 0.0f;
        npcDelay_ = 5.0f;

        // A코스처럼 B코스도 시작 지점 근처 NPC 제거
        const float clearR = 18.0f;
        npcCars_.erase(
            std::remove_if(npcCars_.begin(), npcCars_.end(), [&](const NpcCar& c) {
                float dx = c.position.x - spawnX_;
                float dy = c.position.y - spawnY_;
                return (dx * dx + dy * dy) < clearR * clearR;
                }),
            npcCars_.end()
        );
    }


    // generateParallelParkingMap - TileID based
    // MAP: 64 x 32 tiles (wider than original 48, same slot width=5, 11 slots per side)

    void MapSystem::generateParallelParkingMap() {
        checkpoints_.clear();
        parkingSlots_.clear();
        npcCars_.clear();
        tileGrid_.clear();

        const int MW = 64, MH = 32;   // widened from 48 -> 64
        width_ = MW;
        height_ = MH;
        MAP_W_ = MW; MAP_H_ = MH;

        // Row layout (unchanged from original)
        const int APT_TOP_T = 0, APT_TOP_B = 5;
        const int SW_TOP = 7;
        const int CURB_TOP = 8;
        const int PZONE_TOP_T = 9, PZONE_TOP_B = 10;
        const int ROAD_T = 11, ROAD_B = 20;
        const int ROAD_MID = 15;
        const int PZONE_BOT_T = 21, PZONE_BOT_B = 22;
        const int CURB_BOT = 23;
        const int SW_BOT = 24;
        const int APT_BOT_T = 26, APT_BOT_B = 31;

        // Same slot width as original, slots increased from 8 -> 11
        const int SLOT_W = 5;
        const int SLOT_CNT = 11;
        const int SLOT_START = 3;

        // 1) Init tileGrid
        tileGrid_.assign(MH, std::vector<TileID>(MW, NAT_GRASS));

        // 2) Top apartments - 5 buildings across 64 cols (same style as original)
        {
            int blds[][2] = { {1,9},{12,22},{25,33},{36,47},{50,62} };
            for (int b = 0; b < 5; b++) {
                int c0 = blds[b][0], c1 = blds[b][1];
                for (int r = APT_TOP_T; r <= APT_TOP_B; r++)
                    for (int c = c0; c <= c1 && c < MW; c++)
                        tileGrid_[r][c] = BLD_APART;
                for (int c = c0; c <= c1 && c < MW; c++)
                    tileGrid_[APT_TOP_T][c] = BLD_OFFICE;
                for (int r = APT_TOP_T + 1; r <= APT_TOP_B - 1; r++)
                    for (int c = c0 + 1; c <= c1 - 1 && c < MW; c++) {
                        int lr = r - APT_TOP_T - 1, lc = c - c0 - 1;
                        if (lr % 2 == 0 && lc % 3 == 1)
                            tileGrid_[r][c] = BLD_OFFICE;
                    }
                int mid = (c0 + c1) / 2;
                if (mid - 1 >= 0 && mid + 1 < MW) {
                    tileGrid_[APT_TOP_B][mid - 1] = BLD_SHOP;
                    tileGrid_[APT_TOP_B][mid] = BLD_SHOP;
                    tileGrid_[APT_TOP_B][mid + 1] = BLD_SHOP;
                }
            }
            // Gaps between buildings
            for (int c = 10; c <= 11; c++)
                for (int r = APT_TOP_T + 1; r <= APT_TOP_B; r++) tileGrid_[r][c] = NAT_GARDEN;
            for (int c = 23; c <= 24; c++)
                for (int r = APT_TOP_T + 1; r <= APT_TOP_B; r++) tileGrid_[r][c] = BLD_PARKING;
            for (int c = 34; c <= 35; c++)
                for (int r = APT_TOP_T + 1; r <= APT_TOP_B; r++) tileGrid_[r][c] = NAT_GARDEN;
            for (int c = 48; c <= 49; c++)
                for (int r = APT_TOP_T + 1; r <= APT_TOP_B; r++) tileGrid_[r][c] = BLD_PARKING;
        }

        // 3) Grass/tree row 6
        for (int c = 0; c < MW; c++) {
            if (c % 6 == 0) tileGrid_[6][c] = NAT_TREE;
            else if (c % 6 == 3) tileGrid_[6][c] = NAT_GARDEN;
            else                  tileGrid_[6][c] = NAT_GRASS;
        }

        // 4) Sidewalk top
        for (int c = 0; c < MW; c++) tileGrid_[SW_TOP][c] = SW_PLAIN;

        // 5) Curb top
        for (int c = 0; c < MW; c++) tileGrid_[CURB_TOP][c] = SW_CURB;

        // 6) Top parking zone
        for (int r = PZONE_TOP_T; r <= PZONE_TOP_B; r++)
            for (int c = 0; c < MW; c++)
                tileGrid_[r][c] = PK_ZONE;

        // Top slot dividers
        for (int s = 0; s <= SLOT_CNT; s++) {
            int col = SLOT_START + s * SLOT_W;
            if (col >= MW) break;
            for (int r = PZONE_TOP_T; r <= PZONE_TOP_B; r++)
                tileGrid_[r][col] = PK_LINE_V;
        }

        // Top parked cars (skip slot 2 = empty target, no yellow)
        {
            int colors[][3] = {
                {65,105,200},{60,170,85},{/*empty*/0,0,0},{200,75,50},
                {90,90,100},{130,60,180},{245,245,245},{30,30,35},
                {65,105,200},{60,170,85},{200,75,50}
            };
            for (int s = 0; s < SLOT_CNT; s++) {
                if (s == 2) continue; // empty target slot
                float cx = (float)(SLOT_START + s * SLOT_W) + SLOT_W * 0.5f + 0.5f;
                float cy = (float)(PZONE_TOP_T + PZONE_TOP_B) * 0.5f + 0.5f;
                npcCars_.push_back(NpcCar(cx, cy, NPC_RIGHT, 0.0f, colors[s][0], colors[s][1], colors[s][2]));
            }
        }

        // Top empty slot marker
        {
            int c0 = SLOT_START + 2 * SLOT_W + 1;
            int c1 = SLOT_START + 3 * SLOT_W - 1;
            for (int r = PZONE_TOP_T; r <= PZONE_TOP_B; r++)
                for (int c = c0; c <= c1 && c < MW; c++)
                    tileGrid_[r][c] = PK_EMPTY;
        }

        // 7) Road
        for (int r = ROAD_T; r <= ROAD_B; r++)
            for (int c = 0; c < MW; c++)
                tileGrid_[r][c] = RD_PLAIN;

        // Center line (double yellow)
        for (int c = 0; c < MW; c++) {
            tileGrid_[ROAD_MID][c] = PK_CENTER;
            tileGrid_[ROAD_MID + 1][c] = PK_CENTER;
        }

        // Dashed lane lines
        for (int c = 0; c < MW; c++) {
            if (c % 5 < 3) {
                tileGrid_[ROAD_T + 2][c] = PK_DASH;
                tileGrid_[ROAD_B - 2][c] = PK_DASH;
            }
        }

        // 8) Bottom parking zone
        for (int r = PZONE_BOT_T; r <= PZONE_BOT_B; r++)
            for (int c = 0; c < MW; c++)
                tileGrid_[r][c] = PK_ZONE;

        // Bottom slot dividers
        for (int s = 0; s <= SLOT_CNT; s++) {
            int col = SLOT_START + s * SLOT_W;
            if (col >= MW) break;
            for (int r = PZONE_BOT_T; r <= PZONE_BOT_B; r++)
                tileGrid_[r][col] = PK_LINE_V;
        }

        // Bottom parked cars (skip slot 4 = empty target, no yellow)
        {
            int colors[][3] = {
                {90,90,100},{200,75,50},{130,60,180},{30,30,35},{/*empty*/0,0,0},
                {60,170,85},{245,245,245},{200,75,50},{65,105,200},{130,60,180},{60,170,85}
            };
            for (int s = 0; s < SLOT_CNT; s++) {
                if (s == 4) continue; // empty target slot
                float cx = (float)(SLOT_START + s * SLOT_W) + SLOT_W * 0.5f + 0.5f;
                float cy = (float)(PZONE_BOT_T + PZONE_BOT_B) * 0.5f + 0.5f;
                npcCars_.push_back(NpcCar(cx, cy, NPC_LEFT, 0.0f, colors[s][0], colors[s][1], colors[s][2]));
            }
        }

        // Bottom empty slot marker
        {
            int c0 = SLOT_START + 4 * SLOT_W + 1;
            int c1 = SLOT_START + 5 * SLOT_W - 1;
            for (int r = PZONE_BOT_T; r <= PZONE_BOT_B; r++)
                for (int c = c0; c <= c1 && c < MW; c++)
                    tileGrid_[r][c] = PK_EMPTY;
        }

        // 9) Curb bottom
        for (int c = 0; c < MW; c++) tileGrid_[CURB_BOT][c] = SW_CURB;

        // 10) Sidewalk bottom
        for (int c = 0; c < MW; c++) tileGrid_[SW_BOT][c] = SW_PLAIN;

        // 11) Grass/tree row 25
        for (int c = 0; c < MW; c++) {
            if (c % 6 == 1) tileGrid_[25][c] = NAT_TREE;
            else if (c % 6 == 4) tileGrid_[25][c] = NAT_GARDEN;
            else                  tileGrid_[25][c] = NAT_GRASS;
        }

        // 12) Bottom apartments - 5 buildings
        {
            int blds[][2] = { {1,9},{12,22},{25,33},{36,47},{50,62} };
            for (int b = 0; b < 5; b++) {
                int c0 = blds[b][0], c1 = blds[b][1];
                for (int r = APT_BOT_T; r <= APT_BOT_B; r++)
                    for (int c = c0; c <= c1 && c < MW; c++)
                        tileGrid_[r][c] = BLD_APART;
                for (int c = c0; c <= c1 && c < MW; c++)
                    tileGrid_[APT_BOT_T][c] = BLD_OFFICE;
                for (int r = APT_BOT_T + 1; r <= APT_BOT_B - 1; r++)
                    for (int c = c0 + 1; c <= c1 - 1 && c < MW; c++) {
                        int lr = r - APT_BOT_T - 1, lc = c - c0 - 1;
                        if (lr % 2 == 0 && lc % 3 == 1)
                            tileGrid_[r][c] = BLD_OFFICE;
                    }
                int mid = (c0 + c1) / 2;
                if (mid - 1 >= 0 && mid + 1 < MW) {
                    tileGrid_[APT_BOT_T][mid - 1] = BLD_SHOP;
                    tileGrid_[APT_BOT_T][mid] = BLD_SHOP;
                    tileGrid_[APT_BOT_T][mid + 1] = BLD_SHOP;
                }
            }
            for (int c = 10; c <= 11; c++)
                for (int r = APT_BOT_T; r <= APT_BOT_B - 1; r++) tileGrid_[r][c] = BLD_PARKING;
            for (int c = 34; c <= 35; c++)
                for (int r = APT_BOT_T; r <= APT_BOT_B - 1; r++) tileGrid_[r][c] = NAT_GARDEN;
            for (int c = 48; c <= 49; c++)
                for (int r = APT_BOT_T; r <= APT_BOT_B - 1; r++) tileGrid_[r][c] = BLD_PARKING;
        }

        // Build RC grid from tile grid
        grid_.assign(MH, std::vector<int>(MW, RC_ROAD));
        for (int r = 0; r < MH; r++)
            for (int c = 0; c < MW; c++)
                grid_[r][c] = tileToRc(tileGrid_[r][c]);

        hasRoundabout_ = false;
        hasTileGrid_ = true;

        // Spawn: right lane, heading east
        spawnX_ = 4.0f;
        spawnY_ = (float)(ROAD_MID + 3);
        spawnHeading_ = 0.0f;

        // Target: bottom empty slot 4 (same index as original)
        float cx = (float)(SLOT_START + 4 * SLOT_W + SLOT_W / 2) + 0.5f;
        float cy = (float)(PZONE_BOT_T + PZONE_BOT_B) / 2.0f;
        ParkingSlot target;
        target.id = 0;
        target.position = Vec2(cx, cy);
        target.angle = 0.0f; // parallel
        target.isTarget = true;
        parkingSlots_.push_back(target);
    }


    // generateTParkingMap - TileID based
    // MAP: 64 x 32 tiles  (wider, more slots, smaller slot width)

    void MapSystem::generateTParkingMap() {
        checkpoints_.clear();
        parkingSlots_.clear();
        npcCars_.clear();
        tileGrid_.clear();

        const int MW = 64, MH = 32;
        width_ = MW; height_ = MH;
        MAP_W_ = MW; MAP_H_ = MH;

        const int PK_L = 3, PK_R = 60;          // left/right curb col
        const int PK_T = 3, PK_B = 22;           // top/bottom curb row

        // ???? Slot zones ????????????????????????????????????????????????????????????????????????????????????????????
        const int TOP_SLOT_T = 4, TOP_SLOT_B = 10;  // top slot rows
        const int DRIVE_T = 11, DRIVE_B = 16;  // central aisle
        const int BOT_SLOT_T = 17, BOT_SLOT_B = 21; // bottom slot rows

        // ???? Slot geometry (narrower = 3 tiles wide) ??????????????????????????????????
        const int SLOT_W = 3;               // slot width in tiles
        const int SLOT_START = PK_L + 2;        // first divider col
        // how many slots fit between PK_L+1 and PK_R-1
        const int INNER_W = (PK_R - 1) - (PK_L + 1); // = 56
        const int SLOT_CNT = INNER_W / SLOT_W;          // = 18

        // ???? Gate (entrance/exit) ??????????????????????????????????????????????????????????????????????????
        // place gate roughly in the middle ? spans ~6 tiles
        const int GATE_MID = (PK_L + PK_R) / 2;        // = 31
        const int GATE_L = GATE_MID - 3;
        const int GATE_R = GATE_MID + 3;

        const int ROAD_ROW = 24;

        // ???? Target slot index (which top slot is empty/target) ??????????????
        const int TARGET_SLOT = 5;   // 0-based among top slots

        // 1) Init: all grass
        tileGrid_.assign(MH, std::vector<TileID>(MW, NAT_GRASS));

        // 2) Decorative trees / flowers above the lot
        for (int c = 0; c < MW; c++) {
            if (c % 5 == 0) tileGrid_[0][c] = NAT_TREE;
            if (c % 7 == 1) tileGrid_[1][c] = NAT_TREE;
            if (c % 4 == 2 && (c < PK_L || c > PK_R)) tileGrid_[2][c] = NAT_GARDEN;
        }

        // 3) Parking lot floor (inside curb)
        for (int r = PK_T + 1; r <= PK_B; r++)
            for (int c = PK_L + 1; c <= PK_R - 1; c++)
                tileGrid_[r][c] = PK_ZONE;

        // 4) Curb outline
        for (int c = PK_L; c <= PK_R; c++) tileGrid_[PK_T][c] = SW_CURB;
        for (int c = PK_L; c <= PK_R; c++)
            if (c < GATE_L || c > GATE_R) tileGrid_[PK_B + 1][c] = SW_CURB;
        for (int r = PK_T; r <= PK_B; r++) { tileGrid_[r][PK_L] = SW_CURB; tileGrid_[r][PK_R] = SW_CURB; }
        tileGrid_[PK_T][PK_L] = PK_CURB_CORNER;
        tileGrid_[PK_T][PK_R] = PK_CURB_CORNER;
        tileGrid_[PK_B + 1][PK_L] = PK_CURB_CORNER;
        tileGrid_[PK_B + 1][PK_R] = PK_CURB_CORNER;

        // 5) Gate entrance aisle (between curb and road)
        for (int r = PK_B + 1; r < ROAD_ROW; r++)
            for (int c = GATE_L; c <= GATE_R; c++)
                tileGrid_[r][c] = PK_ZONE;
        // Sidewalk flanking gate
        for (int c = 0; c < GATE_L; c++)
            if (tileGrid_[PK_B + 1][c] == NAT_GRASS) tileGrid_[PK_B + 1][c] = SW_PLAIN;
        for (int c = GATE_R + 1; c < MW; c++)
            if (tileGrid_[PK_B + 1][c] == NAT_GRASS) tileGrid_[PK_B + 1][c] = SW_PLAIN;

        // 6) TOP SLOTS ????????????????????????????????????????????????????????????????????????????????????????????
        for (int r = TOP_SLOT_T + 1; r <= TOP_SLOT_B; r++)
            for (int c = PK_L + 1; c <= PK_R - 1; c++)
                tileGrid_[r][c] = PK_STRIPE;

        // Top slot dividers
        for (int s = 0; s <= SLOT_CNT; s++) {
            int col = SLOT_START + s * SLOT_W;
            if (col > PK_R - 1) break;
            for (int r = TOP_SLOT_T + 1; r <= TOP_SLOT_B; r++)
                tileGrid_[r][col] = PK_LINE_V;
        }
        // Top slot bottom line
        for (int c = PK_L + 1; c <= PK_R - 1; c++)
            tileGrid_[TOP_SLOT_B][c] = PK_LINE_H;

        // Top slot cars (all filled except TARGET_SLOT)
        {
            int palette[][3] = {
                {65,105,200},{200,75,50},{60,170,85},{90,90,100},
                {130,60,180},{200,75,50},{245,245,245},{65,105,200},
                {60,170,85},{90,90,100},{130,60,180},{200,75,50},
                {65,105,200},{60,170,85},{245,245,245},{90,90,100},
                {200,75,50},{130,60,180}
            };
            for (int s = 0; s < SLOT_CNT; s++) {
                if (s == TARGET_SLOT) continue; // leave target empty
                float cx = (float)(SLOT_START + s * SLOT_W) + SLOT_W * 0.5f + 0.6f;
                float cy = (float)(TOP_SLOT_T + 1 + TOP_SLOT_B) * 0.5f;
                int pi = s % 18;
                npcCars_.push_back(NpcCar(cx, cy, NPC_DOWN, 0.0f, palette[pi][0], palette[pi][1], palette[pi][2]));
            }
        }

        // Mark target slot as PK_EMPTY
        {
            int c0 = SLOT_START + TARGET_SLOT * SLOT_W + 1;
            int c1 = SLOT_START + (TARGET_SLOT + 1) * SLOT_W - 1;
            for (int r = TOP_SLOT_T + 1; r < TOP_SLOT_B; r++)
                for (int c = c0; c <= c1 && c <= PK_R - 1; c++)
                    tileGrid_[r][c] = PK_EMPTY;
        }

        // 7) Central driving aisle
        for (int r = DRIVE_T; r <= DRIVE_B; r++)
            for (int c = PK_L + 1; c <= PK_R - 1; c++)
                tileGrid_[r][c] = PK_ZONE;
        // Aisle center dashed line
        int aisleM = (DRIVE_T + DRIVE_B) / 2;
        for (int c = PK_L + 1; c <= PK_R - 1; c++)
            if (c % 4 < 2) tileGrid_[aisleM][c] = PK_DASH;

        // 8) BOTTOM SLOTS ??????????????????????????????????????????????????????????????????????????????????????
        for (int r = BOT_SLOT_T; r <= BOT_SLOT_B; r++)
            for (int c = PK_L + 1; c <= PK_R - 1; c++)
                tileGrid_[r][c] = PK_STRIPE;
        // Bottom slot top line
        for (int c = PK_L + 1; c <= PK_R - 1; c++) {

            int slotIndex = (c - SLOT_START) / SLOT_W;
            // ?? 9??° ??? ?? ?? ??? (0-based ??????? 8)
            if (slotIndex == 7) continue;
            tileGrid_[BOT_SLOT_T][c] = PK_LINE_H;
        }
        // Determine which bottom slots are in the gate column range
        // Bottom slot dividers (skip slots that overlap the gate entrance)
        for (int s = 0; s <= SLOT_CNT; s++) {
            int col = SLOT_START + s * SLOT_W;
            if (col > PK_R - 1) break;
            // check if this divider column is inside gate area
            if (col >= GATE_L && col <= GATE_R) continue;
            for (int r = BOT_SLOT_T; r <= BOT_SLOT_B; r++)
                tileGrid_[r][col] = PK_LINE_V;
        }

        // Gate area bottom ?? asphalt (clear slot stripes)
        for (int r = BOT_SLOT_T; r <= BOT_SLOT_B; r++)
            for (int c = GATE_L; c <= GATE_R && c <= PK_R - 1; c++)
                tileGrid_[r][c] = PK_ZONE;

        // Bottom slot cars (skip slots overlapping gate)
        {
            int palette[][3] = {
                {200,75,50},{90,90,100},{65,105,200},{130,60,180},
                {60,170,85},{245,245,245},{130,60,180},{200,75,50},
                {65,105,200},{90,90,100},{60,170,85},{200,75,50},
                {65,105,200},{130,60,180},{245,245,245},{90,90,100},
                {200,75,50},{60,170,85}
            };
            for (int s = 0; s < SLOT_CNT; s++) {
                int slotL = SLOT_START + s * SLOT_W;
                int slotR = slotL + SLOT_W - 1;
                // skip if slot overlaps gate
                if (slotR >= GATE_L && slotL <= GATE_R) continue;
                float cx = (float)slotL + SLOT_W * 0.5f + 0.6f;
                float cy = (float)(BOT_SLOT_T + BOT_SLOT_B) * 0.5f + 0.5f;
                int pi = s % 18;
                npcCars_.push_back(NpcCar(cx, cy, NPC_UP, 0.0f, palette[pi][0], palette[pi][1], palette[pi][2]));
            }
        }

        // 9) Side greenery
        for (int r = PK_T; r <= PK_B; r++) {
            if (r % 4 == 0) { tileGrid_[r][0] = NAT_TREE;   tileGrid_[r][MW - 1] = NAT_TREE; }
            if (r % 4 == 2) { tileGrid_[r][1] = NAT_GARDEN; tileGrid_[r][MW - 2] = NAT_GARDEN; }
        }

        // 10) Road at bottom
        for (int r = ROAD_ROW; r < MH; r++)
            for (int c = 0; c < MW; c++)
                tileGrid_[r][c] = RD_PLAIN;
        // Center line
        for (int c = 0; c < MW; c++) tileGrid_[ROAD_ROW + 2][c] = PK_CENTER;
        // Dashed lanes
        for (int c = 0; c < MW; c++) {
            if (c % 5 < 3) {
                tileGrid_[ROAD_ROW + 1][c] = PK_DASH;
                tileGrid_[ROAD_ROW + 3][c] = PK_DASH;
            }
        }

        // Build RC grid from tile grid
        grid_.assign(MH, std::vector<int>(MW, RC_ROAD));
        for (int r = 0; r < MH; r++)
            for (int c = 0; c < MW; c++)
                grid_[r][c] = tileToRc(tileGrid_[r][c]);

        hasRoundabout_ = false;
        hasTileGrid_ = true;

        // Spawn: on road, heading west (right side of map)
        spawnX_ = (float)(MW - 8);
        spawnY_ = (float)(ROAD_ROW + 3);
        spawnHeading_ = (float)M_PI;

        // Target parking slot (top, TARGET_SLOT index)
        float tcx = (float)(SLOT_START + TARGET_SLOT * SLOT_W) + SLOT_W * 0.5f;
        float tcy = (float)(TOP_SLOT_T + 1 + TOP_SLOT_B) / 2.0f;
        ParkingSlot target;
        target.id = 0;
        target.position = Vec2(tcx, tcy);
        target.angle = (float)M_PI / 2.0f;
        target.isTarget = true;
        parkingSlots_.push_back(target);
    }

} // namespace bestdriver