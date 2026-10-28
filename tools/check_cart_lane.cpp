// Offline native-data probe; does not connect to a realm or install files.
#include "MapDefines.h"
#include "DetourAlloc.h"
#include "DetourNavMeshQuery.h"
#include "MapTree.h"
#include "VMapMgr2.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    if (argc != 2 && (argc != 3 || std::string(argv[2]) != "--profile"))
        return 2;
    try
    {
        bool profile = argc == 3;
        std::filesystem::path root(argv[1]);
        dtNavMeshParams params{};
        std::ifstream definition(root / "mmaps/890.mmap", std::ios::binary);
        if (!definition.read(reinterpret_cast<char*>(&params), sizeof(params)))
            throw std::runtime_error("invalid map params");
        auto* mesh = dtAllocNavMesh();
        if (!mesh || dtStatusFailed(mesh->init(&params)))
            throw std::runtime_error("cannot initialize navmesh");
        for (auto const& entry : std::filesystem::directory_iterator(root / "mmaps"))
        {
            if (entry.path().extension() != ".mmtile")
                continue;
            std::ifstream input(entry.path(), std::ios::binary);
            MmapTileHeader header{};
            if (!input.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
                header.mmapMagic != MMAP_MAGIC || header.dtVersion != DT_NAVMESH_VERSION ||
                header.mmapVersion != MMAP_VERSION || header.size > 16 * 1024 * 1024)
                throw std::runtime_error("invalid tile");
            auto* data = static_cast<unsigned char*>(dtAlloc(header.size, DT_ALLOC_PERM));
            if (!data || !input.read(reinterpret_cast<char*>(data), header.size) ||
                dtStatusFailed(mesh->addTile(data, header.size, DT_TILE_FREE_DATA, 0, nullptr)))
                throw std::runtime_error("cannot load navigation tile");
        }
        VMAP::StaticMapTree collision(890, (root / "vmaps").string() + "/");
        if (!collision.InitMap("890.vmtree"))
            throw std::runtime_error("cannot load collision map");
        for (auto const& entry : std::filesystem::directory_iterator(root / "vmaps"))
            if (entry.path().extension() == ".vmtile")
            {
                unsigned map = 0, x = 0, y = 0;
                if (std::sscanf(entry.path().filename().string().c_str(), "%u_%u_%u.vmtile", &map, &x, &y) != 3 ||
                    map != 890 || !collision.LoadMapTile(x, y))
                    throw std::runtime_error("cannot load collision tile");
            }
        auto* query = dtAllocNavMeshQuery();
        if (!query || dtStatusFailed(query->init(mesh, 32768)))
            throw std::runtime_error("cannot create nav query");
        dtQueryFilter filter;
        filter.setIncludeFlags(NAV_GROUND);
        filter.setExcludeFlags(NAV_GROUND_STEEP | NAV_MAGMA | NAV_SLIME | NAV_WATER);
        // Match the pinned core's non-headless player / walking creature
        // ground traversal while rejecting all liquid hazards for this lane.
        dtQueryFilter diagnosticFilter;
        diagnosticFilter.setIncludeFlags(NAV_GROUND | NAV_GROUND_STEEP);
        diagnosticFilter.setExcludeFlags(NAV_MAGMA | NAV_SLIME | NAV_WATER);
        // Spider Mine boss chamber lane (test-package/burning-cart.json); floor around z -6.5.
        std::array<float, 2> start{114.0f, -26.39f};
        std::array<float, 2> end{126.0f, -5.61f};
        unsigned failed = 0;
        dtPolyRef previous = 0;
        std::array<float, 3> previousPoint{};
        auto sample = [&](unsigned index, float offset, dtPolyRef from, std::array<float, 3> const& fromPoint)
        {
            float t = index / 30.0f;
            float dx = end[0] - start[0], dy = end[1] - start[1];
            float length = std::hypot(dx, dy);
            float x = start[0] + t * dx - offset * dy / length;
            float y = start[1] + t * dy + offset * dx / length;
            auto internal = VMAP::VMapMgr2::convertPositionToInternalRep(x, y, 0.0f);
            float floor = collision.getHeight(internal, 15.0f);
            std::array<float, 3> point{y, floor, x}, nearest{};
            float extents[3]{1, 2, 1};
            dtPolyRef polygon = 0;
            auto status = query->findNearestPoly(point.data(), extents, &filter, &polygon, nearest.data());
            bool good = (profile || !offset || from) && std::isfinite(floor) && floor > -10 && floor < -3 && !dtStatusFailed(status) && polygon &&
                std::hypot(nearest[0] - y, nearest[2] - x) < 0.75f && std::abs(nearest[1] - floor) < 1.5f;
            if (good && from)
            {
                dtPolyRef path[256];
                int count = 0;
                status = query->findPath(from, polygon, fromPoint.data(), nearest.data(), &diagnosticFilter, path, &count, 256);
                good = !dtStatusFailed(status) && count && path[count - 1] == polygon &&
                    !dtStatusDetail(status, DT_PARTIAL_RESULT | DT_OUT_OF_NODES | DT_BUFFER_TOO_SMALL);
            }
            float rayFraction = 1, steepRayFraction = 1;
            bool rayChecked = false;
            if (good && from)
            {
                float fraction = 0, normal[3]{};
                dtPolyRef traversed[256];
                int count = 0;
                status = query->raycast(from, fromPoint.data(), nearest.data(), &filter,
                    &fraction, normal, traversed, &count, 256);
                rayChecked = true;
                rayFraction = fraction;
                float steepNormal[3]{};
                dtPolyRef steepPath[256];
                int steepCount = 0;
                auto diagnosticStatus = query->raycast(from, fromPoint.data(), nearest.data(), &diagnosticFilter,
                    &steepRayFraction, steepNormal, steepPath, &steepCount, 256);
                if (dtStatusFailed(diagnosticStatus) || !steepCount ||
                    dtStatusDetail(diagnosticStatus, DT_PARTIAL_RESULT | DT_OUT_OF_NODES | DT_BUFFER_TOO_SMALL))
                    steepRayFraction = -1;
                // Real non-headless players and walking creatures include steep
                // ground in pinned core PathGenerator::CreateFilter. Keep the
                // stricter ground-only ray as separate diagnostic evidence.
                good = steepRayFraction >= 1;
            }
            if (profile)
            {
                float centerX = start[0] + t * dx, centerY = start[1] + t * dy;
                auto center = VMAP::VMapMgr2::convertPositionToInternalRep(centerX, centerY, 0);
                float centerFloor = collision.getHeight(center, 15);
                for (float height : {0.5f, 1.5f})
                {
                    auto a = VMAP::VMapMgr2::convertPositionToInternalRep(centerX, centerY, centerFloor + height);
                    auto b = VMAP::VMapMgr2::convertPositionToInternalRep(x, y, floor + height);
                    G3D::Vector3 hit;
                    bool blocked = collision.GetObjectHitPos(a, b, hit, 0);
                    std::cout << "{\"profileOffset\":" << offset << ",\"height\":" << height
                        << ",\"collisionBlocked\":" << (blocked ? "true" : "false") << "}\n";
                }
            }
            failed += !good;
            std::cout << "{\"sample\":" << index << ",\"offset\":" << offset << ",\"xyz\":[" << x << ',' << y << ',' << floor
                      << "],\"rayFraction\":" << (rayChecked ? std::to_string(rayFraction) : "null")
                      << ",\"steepRayFraction\":" << (rayChecked ? std::to_string(steepRayFraction) : "null") << ",\"ground\":" << (good ? "true" : "false") << "}\n";
            return std::pair<dtPolyRef, std::array<float, 3>>{good ? polygon : 0, nearest};
        };
        if (profile)
        {
            for (int offset = -10; offset <= 10; ++offset)
                sample(15, offset * 0.5f, 0, {});
        }
        else for (unsigned index = 0; index <= 30; ++index)
        {
            auto center = sample(index, 0, previous, previousPoint);
            previous = center.first;
            previousPoint = center.second;
            // Check both escape directions where the boss occupies the lane.
            // A ground projection alone is insufficient: require a complete path
            // from the lane center to each four-yard sidestep destination.
            if (index >= 14 && index <= 16)
            {
                sample(index, -4, center.first, center.second);
                sample(index, 4, center.first, center.second);
            }
        }
        collision.UnloadMap();
        dtFreeNavMeshQuery(query);
        dtFreeNavMesh(mesh);
        return failed ? 1 : 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
