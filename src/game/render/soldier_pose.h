#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace disruptor::soldier_pose {

struct Pose {
    uint32_t packed_size_and_duration, middle_word;
    uint8_t width, height;
    std::array<int8_t, 4> towards_previous, towards_next;

    bool matches(uint32_t packed, uint32_t wide, uint32_t tall, uint32_t middle) const {
        return packed_size_and_duration == packed && width == wide && height == tall && middle_word == middle;
    }
};

// Registered upper-corner motion, in quarter texels of the original soldier artwork.
inline constexpr std::array<Pose, 38> poses{{
    {0x06001640u, 0x73737373u, 112, 160, {5, -2, -7, -14}, {4, -5, -8, -17}},
    {0x06001644u, 0x5D1AC32Au, 74, 152, {1, -7, -5, -12}, {1, -11, -8, -15}},
    {0x06001814u, 0x3C3C5647u, 96, 150, {-9, -5, -1, -12}, {-13, -8, 2, -14}},
    {0x06001594u, 0xC1737333u, 124, 156, {0, 0, 0, 0}, {6, -9, -6, -16}},
    {},
    {0x060015ACu, 0x1A1AA4A4u, 106, 152, {-4, 4, 8, 16}, {7, 6, -5, 0}},
    {0x060014ACu, 0x086F82D6u, 68, 146, {0, 11, 6, 14}, {2, 9, 2, 0}},
    {0x060014D4u, 0x3C744747u, 98, 142, {14, 8, -3, 13}, {-15, 9, 0, -1}},
    {0x060014C0u, 0xC7745656u, 120, 146, {-6, 8, 6, 15}, {1, 10, -11, -2}},
    {},
    {0x060015B4u, 0xF0F0B7B7u, 98, 146, {-7, -6, 5, 0}, {4, 2, -5, 9}},
    {0x06001268u, 0x95189898u, 64, 142, {0, -9, -5, 0}, {-10, 12, 4, 7}},
    {0x06001694u, 0x99D362B2u, 108, 142, {17, -8, 0, 1}, {-9, 11, 3, 8}},
    {0x060016FCu, 0x95232323u, 118, 144, {-1, -9, 11, 2}, {0, 0, 0, 0}},
    {},
    {0x060015C4u, 0x68680327u, 94, 160, {-4, -4, 5, -8}, {-6, -12, 6, -7}},
    {0x060013F8u, 0x61619B92u, 62, 160, {12, -12, -4, -8}, {12, -20, 0, -8}},
    {0x06001770u, 0xD6490C0Cu, 114, 154, {10, -12, -3, -8}, {12, -19, 2, -7}},
    {},
    {},
    {0x060015F8u, 0x94183030u, 100, 156, {6, 10, -6, 8}, {-5, 0, 5, 4}},
    {0x060014FCu, 0x5355FB8Fu, 66, 158, {-8, 19, -1, 8}, {-3, -10, -5, -3}},
    {0x060014F4u, 0x23232323u, 106, 156, {-11, 19, -3, 7}, {16, -1, 0, 3}},
    {},
    {},
    {0x06001604u, 0x52AB9D9Du, 108, 144, {6, 0, -6, -4}, {-5, 2, 7, 14}},
    {0x06001624u, 0x18995C3Au, 74, 148, {5, 10, 2, 2}, {0, 8, 5, 11}},
    {0x06001790u, 0xD6D1D649u, 96, 152, {-14, 1, 0, -4}, {11, 6, 0, 11}},
    {},
    {},
    {0x060016ACu, 0x7F04743Cu, 108, 162, {0, 0, 0, 0}, {1, 7, -11, 19}},
    {0x060015B8u, 0x36525252u, 92, 162, {0, -8, 12, -20}, {-6, 7, -18, 19}},
    {0x060014C8u, 0x88243562u, 62, 162, {7, -8, 19, -20}, {-20, 11, -14, 10}},
    {0x08001434u, 0x56565656u, 60, 162, {23, -8, 11, -15}, {-16, 8, -4, 20}},
    {0x06001388u, 0xFABFBF37u, 58, 162, {16, -9, 4, -21}, {-12, 10, -3, -2}},
    {0x0600138Cu, 0x49494949u, 60, 162, {13, -10, 1, 2}, {30, -37, 18, -25}},
    {0x06001538u, 0xB9B936B9u, 60, 162, {-30, 36, -18, 24}, {14, -31, 2, -19}},
    {0x06001534u, 0xD8AE8F36u, 66, 162, {-14, 31, -2, 19}, {0, 0, 0, 0}},
}};

inline constexpr std::array<Pose, 14> beast_attack{{
    {0x04001154u, 0x10101003u, 86, 82, {0, 0, 0, 0}, {44, -93, 32, -105}},
    {0x040011DCu, 0x03030D0Du, 86, 102, {-35, 82, -31, 86}, {-6, -29, -16, -30}},
    {0x040012A8u, 0x0D0D0D03u, 88, 128, {10, 37, 14, 25}, {3, -8, 7, -4}},
    {0x04001300u, 0x0D19190Du, 90, 130, {-3, 9, -7, 4}, {-4, 9, 8, 21}},
    {0x040013FCu, 0x0D190D0Du, 92, 120, {4, -9, -8, -21}, {-13, 63, -1, 75}},
    {0x0400141Cu, 0x0A151329u, 94, 104, {14, -71, 2, -83}, {-20, 71, -8, 83}},
    {0x040014BCu, 0x10151510u, 94, 134, {24, -94, 12, -106}, {-11, 35, -6, 47}},
    {0x040014A0u, 0x0D030A0Au, 94, 152, {11, -39, 7, -51}, {11, -39, 7, -51}},
    {0x040014BCu, 0x10151510u, 94, 134, {-11, 35, -6, 47}, {24, -94, 12, -106}},
    {0x0400141Cu, 0x0A151329u, 94, 104, {-20, 71, -8, 83}, {14, -71, 2, -83}},
    {0x040013FCu, 0x0D190D0Du, 92, 120, {-13, 63, -1, 75}, {4, -9, -8, -21}},
    {0x04001300u, 0x0D19190Du, 90, 130, {-4, 9, 8, 21}, {-3, 9, -7, 4}},
    {0x040012A8u, 0x0D0D0D03u, 88, 128, {3, -8, 7, -4}, {10, 37, 14, 25}},
    {0x040011DCu, 0x03030D0Du, 86, 102, {-6, -29, -16, -30}, {0, 0, 0, 0}},
}};

inline constexpr std::array<Pose, 40> robot_attack{{
    {0x0400122Cu, 0x110B0412u, 108, 142, {2, -10, 2, -10}, {0, -10, 0, -10}},
    {0x04000DC0u, 0x10070707u, 94, 144, {0, -10, 0, -10}, {2, -10, 2, -10}},
    {0x04000D90u, 0x12070707u, 100, 140, {1, -10, 1, -10}, {0, -10, 0, -10}},
    {0x04000FFCu, 0x0B110707u, 122, 134, {-1, -10, -1, -10}, {-1, -10, -1, -10}},
    {},
    {0x0400123Cu, 0x9614141Cu, 102, 140, {0, 10, 0, 10}, {2, -10, 2, -10}},
    {0x04000F50u, 0x03120404u, 96, 142, {-2, 10, -2, 10}, {0, -9, 0, -9}},
    {0x04000CBCu, 0x07070B11u, 94, 140, {0, 10, 0, 10}, {1, -10, 1, -10}},
    {0x04001004u, 0x14000000u, 116, 138, {1, 10, 1, 10}, {0, -10, 0, -10}},
    {},
    {0x04001228u, 0x05A9E70Bu, 94, 132, {-2, 9, -2, 9}, {1, 9, 1, 9}},
    {0x04000FCCu, 0x04040707u, 100, 136, {0, 9, 0, 9}, {3, 9, 3, 9}},
    {0x04000B70u, 0x12120707u, 86, 138, {-1, 10, -1, 10}, {1, 9, 1, 9}},
    {0x04000F78u, 0x0B040404u, 108, 138, {1, 10, 1, 10}, {0, 9, 0, 9}},
    {},
    {0x04001228u, 0x04242424u, 88, 134, {-1, -10, -1, -10}, {2, 9, 2, 9}},
    {0x04000FE4u, 0x54070404u, 102, 134, {-3, -9, -3, -9}, {2, 9, 2, 9}},
    {0x04000C24u, 0x040B1405u, 78, 140, {-1, -9, -1, -9}, {1, 9, 1, 9}},
    {0x04000E74u, 0x07076461u, 100, 140, {0, -9, 0, -9}, {0, 9, 0, 9}},
    {},
    {0x040011CCu, 0x1B239C78u, 82, 142, {-2, -10, -2, -10}, {-4, -10, -4, -10}},
    {0x04000FECu, 0x17001011u, 104, 134, {-2, -10, -2, -10}, {-6, -9, -6, -9}},
    {0x04000D40u, 0x120B1C14u, 84, 136, {-1, -9, -1, -9}, {0, -9, 0, -9}},
    {0x04000E70u, 0x172E0B03u, 92, 142, {0, -9, 0, -9}, {0, -9, 0, -9}},
    {},
    {0x04001218u, 0x147C409Cu, 88, 138, {4, 9, 4, 9}, {0, -10, 0, -10}},
    {0x04000FECu, 0x5EE39952u, 102, 132, {6, 9, 6, 9}, {1, -10, 1, -10}},
    {0x04000BACu, 0x1C120707u, 78, 136, {0, 9, 0, 9}, {-1, -9, -1, -9}},
    {0x04000EE0u, 0x220B0404u, 100, 136, {0, 9, 0, 9}, {0, -9, 0, -9}},
    {},
    {0x04001244u, 0x24290404u, 94, 134, {0, 9, 0, 9}, {0, 9, 0, 9}},
    {0x04000F80u, 0x07070704u, 100, 128, {-1, 9, -1, 9}, {-2, 9, -2, 9}},
    {0x04000B08u, 0x12120707u, 86, 130, {1, 8, 1, 8}, {-1, 10, -1, 10}},
    {0x04000FC8u, 0x030B1B0Bu, 108, 130, {0, 9, 0, 9}, {0, 9, 0, 9}},
    {},
    {0x04001228u, 0x2F040704u, 102, 136, {0, -9, 0, -9}, {-2, 10, -2, 10}},
    {0x04000E98u, 0x0B75525Eu, 96, 138, {2, -9, 2, -9}, {0, 10, 0, 10}},
    {0x04000D44u, 0x14110704u, 94, 136, {1, -10, 1, -10}, {-1, 10, -1, 10}},
    {0x04000FE8u, 0x0B0B0404u, 116, 132, {0, -9, 0, -9}, {1, 10, 1, 10}},
    {},
}};

inline constexpr std::array<Pose, 6> robot_shooting{{
    {0x04001218u, 0x22161B0Bu, 108, 142, {3, -10, 3, -10}, {0, 19, 0, 19}},
    {0x040011ACu, 0x04040404u, 114, 144, {0, -19, 0, -19}, {1, 20, 1, 20}},
    {0x04000FFCu, 0xA936E91Bu, 106, 142, {-1, -21, -1, -21}, {1, -10, 1, -10}},
    {0x040011E8u, 0x05070704u, 112, 144, {-1, 10, -1, 10}, {-1, -9, -1, -9}},
    {0x06000F54u, 0x24072424u, 66, 142, {1, 9, 1, 9}, {}},
    {0x06000F54u, 0x24072424u, 66, 142, {}, {1, -31, 1, -31}},
}};

inline constexpr Pose triton_soldier_first{0x09001370u, 0xD25CF76Du, 100, 134, {}, {}};
inline constexpr Pose chemical_fly_first{0x06000C28u, 0x660D0D5Cu, 126, 78, {}, {}};
inline constexpr Pose armored_soldier_first{0x06000D2Cu, 0xF14EE3CCu, 52, 130, {}, {}};
inline constexpr Pose ice_drone_first{0x0400086Cu, 0x2562424Cu, 72, 68, {}, {}};
inline constexpr Pose red_flyer_first{0x040014B0u, 0x2F05052Bu, 74, 148, {}, {}};
inline constexpr Pose red_eyed_beast_first{0x0600172Cu, 0x043B1231u, 116, 150, {}, {}};
inline constexpr Pose red_robot_first{0x0400102Cu, 0x764C0C34u, 102, 134, {}, {}};
inline constexpr Pose gold_helmet_soldier_first{0x06001928u, 0x920192C0u, 114, 162, {}, {}};
inline constexpr Pose tunnel_creature_first{0x060014E0u, 0x14813A7Eu, 82, 148, {}, {}};
inline constexpr Pose spiked_mine_first{0x04000468u, 0x8383554Du, 36, 36, {}, {}};
inline constexpr Pose shield_robot_first{0x04001B8Cu, 0x40404018u, 100, 156, {}, {}};

inline constexpr std::array<Pose, 7> triton_soldier_attack{{
    {0x06001408u, 0x43881F27u, 94, 136, {1, 17, 1, 17}, {4, 0, 4, 0}},
    {0x060012BCu, 0xE17A1026u, 66, 136, {-4, 0, -4, 0}, {7, 13, 7, 13}},
    {0x06001184u, 0x57401557u, 90, 134, {-7, -13, -7, -13}, {1, 0, 1, 0}},
    {0x1C001194u, 0x9A47E091u, 94, 134, {-1, 0, -1, 0}, {-1, 0, -1, 0}},
    {0x060011B4u, 0x9F5454CFu, 92, 134, {1, 0, 1, 0}, {1, 0, 1, 0}},
    {0x06001210u, 0x18CEE599u, 74, 134, {2, 16, 2, 16}, {-7, 0, -7, 0}},
    {0x060013A0u, 0xAB9191ABu, 72, 134, {7, 0, 7, 0}, {1, 20, 1, 20}},
}};

inline constexpr std::array<Pose, 9> armored_soldier_attack{{
    {0x06000D58u, 0x0E283F1Du, 62, 118, {}, {18, -1, 18, -1}},
    {0x06000F18u, 0x7C7CC913u, 64, 118, {-18, 1, -18, 1}, {15, 2, 15, 2}},
    {0x06001104u, 0x44D89F25u, 80, 138, {-16, -3, -16, -3}, {8, 0, 8, 0}},
    {0x06000FB0u, 0xDC230864u, 66, 128, {-8, 0, -8, 0}, {7, 3, 7, 3}},
    {0x06000E50u, 0x1EEB1847u, 70, 138, {-7, -3, -7, -3}, {-2, -4, -2, -4}},
    {0x06000EACu, 0x0D839959u, 68, 134, {2, 4, 2, 4}, {-6, 1, -6, 1}},
    {0x06000FB0u, 0xDC230864u, 66, 128, {5, -1, 5, -1}, {-8, 0, -8, 0}},
    {0x06001104u, 0x44D89F25u, 80, 138, {8, 0, 8, 0}, {-16, -3, -16, -3}},
    {0x06000F18u, 0x7C7CC913u, 64, 118, {15, 2, 15, 2}, {}},
}};

inline constexpr std::array<Pose, 7> red_flyer_attack{{
    {0x04001498u, 0x0F2C2C05u, 74, 148, {}, {17, 10, 17, 10}},
    {0x040016ECu, 0x43353535u, 74, 160, {2, 1, 2, 1}, {15, 7, 15, 7}},
    {0x040017F8u, 0x0B323233u, 82, 158, {-16, -7, -16, -7}, {8, 10, 8, 10}},
    {0x04001540u, 0x166A7A9Au, 110, 142, {-8, -10, -8, -10}, {3, 2, 3, 2}},
    {0x040013D8u, 0x90D65959u, 114, 136, {-3, -2, -3, -2}, {-5, -7, -5, -7}},
    {0x04001450u, 0x0B0B0B0Bu, 86, 158, {5, 7, 5, 7}, {-17, -12, -17, -12}},
    {0x04001320u, 0x79BF2534u, 72, 158, {12, 11, 12, 11}, {-10, -5, -10, -5}},
}};

inline constexpr std::array<Pose, 7> red_eyed_beast_attack{{
    {0x040116DCu, 0x183DF03Du, 116, 146, {}, {7, 24, 7, 24}},
    {0x040114F0u, 0x08080861u, 102, 136, {-7, -24, -7, -24}, {22, 37, 22, 37}},
    {0x060111E0u, 0x4E033244u, 78, 126, {-23, -38, -23, -38}, {5, 24, 5, 24}},
    {0x08010F44u, 0x1E6E6E6Eu, 72, 118, {-5, -25, -5, -25}, {26, 24, 26, 24}},
    {0x0A014F7Cu, 0x0D0D0D0Du, 128, 192, {-32, -30, -32, -30}, {19, 30, 19, 30}},
    {0x080158FCu, 0x47A10544u, 128, 192, {-19, -31, -19, -31}, {-63, -21, -63, -21}},
    {0x06010F58u, 0x444444DFu, 72, 118, {53, 18, 53, 18}, {}},
}};

inline constexpr std::array<Pose, 7> red_robot_shooting{{
    {0x04001000u, 0x300F1404u, 102, 134, {}, {0, 19, 0, 19}},
    {0x04000F94u, 0xBC7AF30Du, 106, 134, {0, -19, 0, -19}, {3, 18, 3, 18}},
    {0x04000E38u, 0x65DBC440u, 100, 134, {-4, -18, -4, -18}, {-1, -8, -1, -8}},
    {0x04000F78u, 0x2EA76A65u, 104, 134, {1, 8, 1, 8}, {1, -10, 1, -10}},
    {0x0A000D80u, 0x4CC14F01u, 62, 134, {0, -1, 0, -1}, {0, -6, 0, -6}},
    {0x06000DF8u, 0x09623434u, 62, 134, {0, 6, 0, 6}, {-2, 0, -2, 0}},
    {0x04001000u, 0x300F1404u, 102, 134, {4, 13, 4, 13}, {}},
}};

inline constexpr std::array<Pose, 11> gold_helmet_soldier_attack{{
    {0x04001864u, 0x68680B88u, 92, 164, {}, {-43, -24, -43, -24}},
    {0x04001750u, 0x687A730Bu, 106, 162, {41, 23, 41, 23}, {-25, -48, -25, -48}},
    {0x0A00170Cu, 0x25083640u, 120, 162, {23, 45, 23, 45}, {}},
    {0x0600170Cu, 0x25083640u, 120, 162, {-12, -12, -12, -12}, {-9, -10, -9, -10}},
    {0x080016CCu, 0xD71C141Cu, 128, 162, {9, 10, 9, 10}, {-6, -6, -6, -6}},
    {0x0400174Cu, 0x1C089D1Cu, 128, 162, {6, 6, 6, 6}, {-2, -10, -2, -10}},
    {0x04001840u, 0x14082020u, 128, 174, {2, 10, 2, 10}, {2, 10, 2, 10}},
    {0x0400174Cu, 0x1C089D1Cu, 128, 162, {-2, -10, -2, -10}, {15, 16, 15, 16}},
    {0x0400170Cu, 0x25083640u, 120, 162, {-16, -16, -16, -16}, {}},
    {0x0400170Cu, 0x25083640u, 120, 162, {}, {23, 45, 23, 45}},
    {0x04001750u, 0x687A730Bu, 106, 162, {-25, -48, -25, -48}, {}},
}};

inline constexpr std::array<Pose, 8> tunnel_creature_attack{{
    {0x06001568u, 0xDB2EAC0Eu, 94, 140, {0, 6, 0, 6}, {-2, -35, -2, -35}},
    {0x06001398u, 0x2E2E1B73u, 126, 136, {2, 32, 2, 32}, {0, -8, 0, -8}},
    {0x08001178u, 0x1B1B73A4u, 118, 140, {0, 8, 0, 8}, {8, 17, 8, 17}},
    {0x0A0010F8u, 0x495C675Cu, 108, 124, {-8, -16, -8, -16}, {3, 25, 3, 25}},
    {0x080010DCu, 0x76070707u, 94, 116, {-3, -26, -3, -26}, {-3, 5, -3, 5}},
    {0x06001144u, 0x4C6A4C4Cu, 90, 126, {3, -6, 3, -6}, {-2, 8, -2, 8}},
    {0x0600148Cu, 0x0D3AAC79u, 88, 152, {3, -9, 3, -9}, {4, -32, 4, -32}},
    {0x06001078u, 0x37166E37u, 102, 120, {-4, 27, -4, 27}, {-6, 16, -6, 16}},
}};
inline constexpr uint32_t tunnel_attack_start = 100u;
inline constexpr uint32_t tunnel_attack_end = tunnel_attack_start + 2u * tunnel_creature_attack.size();

inline std::array<float, 4> displacement(uint32_t frame, uint32_t packed, uint32_t width, uint32_t height,
                                          uint32_t remaining, uint32_t middle_word, uint32_t game_tick = 0u,
                                          bool *preserve_art = nullptr) {
    if (preserve_art) *preserve_art = false;
    const uint32_t duration = packed >> 24;
    const bool beast = frame >= 80u && frame - 80u < beast_attack.size();
    const bool robot = duration == 4u && frame >= 40u && frame - 40u < robot_attack.size();
    const Pose *shot = frame >= 80u && frame - 80u < 2u * robot_shooting.size()
        ? &robot_shooting[(frame - 80u) % robot_shooting.size()] : nullptr;
    if (shot && !shot->matches(packed, width, height, middle_word)) shot = nullptr;
    const bool triton = frame >= 60u && frame - 60u < triton_soldier_attack.size() &&
                        triton_soldier_attack[frame - 60u].matches(packed, width, height, middle_word);
    if (triton) shot = &triton_soldier_attack[frame - 60u];
    if (frame >= 80u && frame - 80u < armored_soldier_attack.size() &&
        armored_soldier_attack[frame - 80u].matches(packed, width, height, middle_word)) {
        shot = &armored_soldier_attack[frame - 80u];
    }
    if (frame >= 80u && frame - 80u < red_eyed_beast_attack.size() &&
        red_eyed_beast_attack[frame - 80u].matches(packed, width, height, middle_word)) {
        shot = &red_eyed_beast_attack[frame - 80u];
    }
    if (frame >= 80u && frame - 80u < red_robot_shooting.size() &&
        red_robot_shooting[frame - 80u].matches(packed, width, height, middle_word)) {
        shot = &red_robot_shooting[frame - 80u];
    }
    if (frame >= 80u && frame - 80u < gold_helmet_soldier_attack.size() &&
        gold_helmet_soldier_attack[frame - 80u].matches(packed, width, height, middle_word)) {
        shot = &gold_helmet_soldier_attack[frame - 80u];
    }
    if (frame >= 48u && frame - 48u < red_flyer_attack.size() &&
        red_flyer_attack[frame - 48u].matches(packed, width, height, middle_word)) {
        shot = &red_flyer_attack[frame - 48u];
    }
    if (frame >= tunnel_attack_start && frame < tunnel_attack_end) {
        const auto &pose = tunnel_creature_attack[(frame - tunnel_attack_start) % tunnel_creature_attack.size()];
        if (pose.matches(packed, width, height, middle_word)) shot = &pose;
    }
    if (!shot && !beast && !robot && (frame < 30u || frame - 30u >= poses.size())) return {};
    const Pose& pose = shot ? *shot : beast ? beast_attack[frame - 80u]
                                         : robot ? robot_attack[frame - 40u] : poses[frame - 30u];
    if (!duration || !pose.matches(packed, width, height, middle_word) || remaining > duration) return {};
    if (preserve_art) *preserve_art = robot || shot != nullptr;
    // These attacks count two ticks at a time and hold the timer on odd ticks.
    const float elapsed = shot || beast || robot ? 0.5f + static_cast<float>(game_tick & 1u) : 0.0f;
    const float phase = std::clamp(0.5f - (static_cast<float>(remaining) - elapsed) / duration, -0.5f, 0.5f);
    const auto& corners = phase < 0.0f ? pose.towards_previous : pose.towards_next;
    const float amount = (phase < 0.0f ? -phase : phase) * 0.25f;
    if ((robot && !shot) || triton) {
        const auto offset = [&](std::size_t corner, uint32_t dimension) {
            const float anchor = 0.125f * (pose.towards_previous[corner] + pose.towards_next[corner]);
            return (0.6f * anchor + 0.4f * amount * corners[corner]) / dimension;
        };
        return {offset(0, width), offset(1, height), offset(2, width), offset(3, height)};
    }
    return {amount * corners[0] / width, amount * corners[1] / height,
            amount * corners[2] / width, amount * corners[3] / height};
}

} // namespace disruptor::soldier_pose
