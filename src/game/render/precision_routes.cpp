#include "game/render/precision_routes.h"

#include "gte_precision.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

/* Disruptor does not use SWC2 for its resident world-projection loops. These
 * two reviewed MFC2 sites snapshot SXY2. Six ordinary SW sites publish into
 * main RAM and one publishes into nine exact scratchpad slots, each only after
 * an exact instruction and committed-word check. Twenty-four reviewed LW/SW
 * pairs then carry those proven words into GP0 packets. Keeping every route
 * behind the game serial prevents another title at the same PCs from
 * inheriting it. */
int disruptor_precision_routes_configure(const char *game_id, size_t length) {
    gte_precision_store_pc_routes_reset();
    if (std::string_view(game_id, length) != "SLUS-00224") return 1;
    static const uint32_t mfc2_pcs[] = {
        0x8004633Cu,
        0x8004795Cu,
    };
    static const uint32_t store_pcs[] = {
        0x8004641Cu,
        0x800464BCu,
        0x80046524u,
        0x80046540u,
        0x80046594u,
        0x800465D8u,
    };
    struct PrecisionCopyRoute {
        uint32_t load_pc;
        uint32_t load_instruction;
        uint32_t store_pc;
        uint32_t store_instruction;
        uint8_t gpr;
    };
    static const PrecisionCopyRoute copy_routes[] = {
        {0x80046C4Cu, 0x8D0C0004u, 0x80046D04u, 0xAF2C0008u, 12u},
        {0x80046C50u, 0x8D2D0004u, 0x80046D08u, 0xAF2D0014u, 13u},
        {0x80046C54u, 0x8D4E0004u, 0x80046D10u, 0xAF2E002Cu, 14u},
        {0x80046C60u, 0x8D6F0004u, 0x80046D0Cu, 0xAF2F0020u, 15u},
        {0x80047A80u, 0x8CA80084u, 0x80047A90u, 0xAF280008u, 8u},
        {0x80047A84u, 0x8CA900A4u, 0x80047A94u, 0xAF290014u, 9u},
        {0x80047A88u, 0x8CAA00C4u, 0x80047A9Cu, 0xAF2A002Cu, 10u},
        {0x80047A8Cu, 0x8CAB00BCu, 0x80047A98u, 0xAF2B0020u, 11u},
        {0x80047B40u, 0x8CA800A4u, 0x80047B50u, 0xAF280008u, 8u},
        {0x80047B44u, 0x8CA9008Cu, 0x80047B54u, 0xAF290014u, 9u},
        {0x80047B48u, 0x8CAA00ACu, 0x80047B5Cu, 0xAF2A002Cu, 10u},
        {0x80047B4Cu, 0x8CAB00C4u, 0x80047B58u, 0xAF2B0020u, 11u},
        {0x80047C00u, 0x8CA800C4u, 0x80047C10u, 0xAF280008u, 8u},
        {0x80047C04u, 0x8CA900ACu, 0x80047C14u, 0xAF290014u, 9u},
        {0x80047C08u, 0x8CAA0094u, 0x80047C1Cu, 0xAF2A002Cu, 10u},
        {0x80047C0Cu, 0x8CAB00B4u, 0x80047C18u, 0xAF2B0020u, 11u},
        {0x80047CC4u, 0x8CA800BCu, 0x80047CD4u, 0xAF280008u, 8u},
        {0x80047CC8u, 0x8CA900C4u, 0x80047CD8u, 0xAF290014u, 9u},
        {0x80047CCCu, 0x8CAA00B4u, 0x80047CE0u, 0xAF2A002Cu, 10u},
        {0x80047CD0u, 0x8CAB009Cu, 0x80047CDCu, 0xAF2B0020u, 11u},
        {0x80047D6Cu, 0x8CA80084u, 0x80047D7Cu, 0xAF280008u, 8u},
        {0x80047D70u, 0x8CA9008Cu, 0x80047D80u, 0xAF290014u, 9u},
        {0x80047D74u, 0x8CAA0094u, 0x80047D88u, 0xAF2A002Cu, 10u},
        {0x80047D78u, 0x8CAB009Cu, 0x80047D84u, 0xAF2B0020u, 11u},
    };
    for (uint32_t pc : mfc2_pcs) {
        if (!gte_precision_mfc2_pc_route_add(
                pc, 0x480A7000u, 14u)) {
            std::fprintf(stderr,
                "psxrecomp: failed to register Disruptor SXY2 MFC2 PC "
                "0x%08X\n", pc);
            return 0;
        }
    }
    for (uint32_t pc : store_pcs) {
        if (!gte_precision_store_pc_route_add(
                pc, 0xACAA0004u, 14u)) {
            std::fprintf(stderr,
                "psxrecomp: failed to register Disruptor SXY2 store PC "
                "0x%08X\n", pc);
            return 0;
        }
    }
    if (!gte_precision_scratch_store_pc_route_add(
            0x80047A0Cu, 0xACAA0004u, 14u,
            0x1F800084u, 8u, 9u)) {
        std::fprintf(stderr,
            "psxrecomp: failed to register Disruptor scratchpad SXY2 "
            "store route 0x80047A0C\n");
        return 0;
    }
    /* 0x80047980..0x800479E0 pin x to -256..576 and y to -256..496 before that store. */
    if (!gte_precision_scratch_store_pc_route_clamp(
            0x80047A0Cu, 0xACAA0004u, -0x100, 0x240, -0x100, 0x1F0)) {
        std::fprintf(stderr,
            "psxrecomp: failed to register the guard band of Disruptor's "
            "scratchpad SXY2 store route 0x80047A0C\n");
        return 0;
    }
    for (const PrecisionCopyRoute& route : copy_routes) {
        if (!gte_precision_copy_pc_route_add(
                route.load_pc, route.load_instruction,
                route.store_pc, route.store_instruction, route.gpr)) {
            std::fprintf(stderr,
                "psxrecomp: failed to register Disruptor projection-copy "
                "route 0x%08X -> 0x%08X\n",
                route.load_pc, route.store_pc);
            return 0;
        }
    }
    std::fprintf(stdout,
        "psxrecomp: registered %zu MFC2 captures, %zu main-RAM projection "
        "stores, 1 scratchpad projection store and %zu packet-copy routes\n",
        sizeof(mfc2_pcs) / sizeof(mfc2_pcs[0]),
        sizeof(store_pcs) / sizeof(store_pcs[0]),
        sizeof(copy_routes) / sizeof(copy_routes[0]));
    return 1;
}
