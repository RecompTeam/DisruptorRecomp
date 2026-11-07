#include "gpu_dma_step.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" {
uint32_t g_psx_ram_mask = 0x1FFFFFu;
}

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

constexpr uint32_t kUsed = PSX_MOD_GPU_DMA_APERTURE_SIZE;  /* all of it, as the render arena takes */

}  // namespace

int main() {
    require(psx_mod_gpu_dma_resolve_address_for(0x80800000u, kUsed) == 0x00800000u &&
                psx_mod_gpu_dma_resolve_address_for(0x80C20000u, kUsed) == 0x00C20000u,
            "Mars: both arena roots stay in the aperture, not the kernel at 0x0 or code at 0x20000");
    require(psx_gpu_dma_next_for(0x00800000u, 4, kUsed) == 0x00800004u &&
                psx_gpu_dma_next_for(0x00C20000u, 4, kUsed) == 0x00C20004u,
            "a forward step stays in the aperture");
    require(psx_gpu_dma_next_for(0x00C20004u, -4, kUsed) == 0x00C20000u, "a backward step stays there too");
    require(psx_gpu_dma_next_for(0x00C1FFFCu, 4, 0x00420000u) == psx_ram_canonical_offset(0x00C20000u),
            "a step past the allocated end folds into RAM, as the aperture's unallocated bytes do");
    require(psx_mod_gpu_dma_resolve_address_for(0x80800000u, 0u) == 0u, "no arena allocated: RAM's mirror");

    require(psx_gpu_dma_next_for(0x001FFFFCu, 4, kUsed) == 0u, "retail RAM wraps at its 2 MiB mirror forward");
    require(psx_gpu_dma_next_for(0u, -4, kUsed) == 0x001FFFFCu, "and backward");
    require(psx_gpu_dma_next_for(0x00010000u, 4, kUsed) == 0x00010004u, "plain RAM steps by the word");

    g_psx_ram_mask = 0x7FFFFFu;
    require(psx_gpu_dma_next_for(0x001FFFFCu, 4, kUsed) == 0x00200000u, "8 MiB RAM does not wrap at 2 MiB");
    require(psx_gpu_dma_next_for(0x007FFFFCu, 4, kUsed) == 0u, "it wraps at 8 MiB, never into the aperture");
    require(psx_gpu_dma_next_for(0x00800000u, 4, kUsed) == 0x00800004u, "the aperture beside 8 MiB RAM");

    std::puts("gpu_dma_step: ok");
    return 0;
}
