/*
 * The sky above the panorama's top is grown on the player's machine from the game's own panorama (sky_extension.h), once a
 * revision, on a worker: the renderer draws the top rows' colour there until it is handed over. A revision is the
 * panorama's indices with its base palette; an unchanged table is checked again every second, as the game may rewrite the
 * VRAM under it.
 */

#include "game/render/sky_grower.h"

#include "game/render/sky_extension.h"
#include "game/render/sky_look.h"
#include "gpu.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

namespace {

namespace sky = disruptor::sky_panorama;
namespace extension = disruptor::sky_extension;

constexpr uint32_t kRecheck = 60;

struct Job {
    uint64_t revision = 0;
    sky::Picture picture;
    extension::Palette palette{};
    int32_t rows = extension::kRows;
};

struct Grown {
    uint64_t revision = 0;
    extension::Extension made;
};

class Grower {
public:
    void ask(Job job) {
        {
            std::scoped_lock lock(mutex_);
            job_ = std::move(job);
            if (!thread_.joinable()) thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
        }
        wake_.notify_one();
    }

    std::optional<Grown> take() {
        std::scoped_lock lock(mutex_);
        std::optional<Grown> out = std::move(grown_);
        grown_.reset();
        return out;
    }

private:
    void run(std::stop_token stop) {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                if (!wake_.wait(lock, stop, [this] { return job_.has_value(); })) return;
                job = std::move(*job_);
                job_.reset();
            }
            Grown grown{job.revision, extension::grow(job.picture, job.palette, job.rows, job.revision)};
            std::scoped_lock lock(mutex_);
            grown_ = std::move(grown);
        }
    }

    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<Job> job_;
    std::optional<Grown> grown_;
    std::jthread thread_;
};

Grower g_grower;
uint64_t g_revision = 0;
uint32_t g_checked_at = 0;

}  // namespace

void disruptor::sky_grower::look(const sky::Cells &cells, uint32_t clut_x, uint32_t clut_y, bool changed, uint32_t vblanks) {
    if (cells.rows == 0) return;
    if (changed || g_revision == 0 || vblanks - g_checked_at >= kRecheck) {
        g_checked_at = vblanks;
        const uint16_t *vram = gpu_get_vram();
        if (vram && clut_x <= 1024u - 256u && clut_y <= 511u) {
            extension::Palette palette{};
            for (size_t index = 0; index < palette.size(); ++index) palette[index] = vram[clut_y * 1024u + clut_x + index];
            sky::Picture picture = sky::picture(cells, vram);
            const uint64_t revision = extension::revision(picture, palette);
            if (revision != g_revision) {
                g_revision = revision;
                const disruptor::sky_look::Look look = disruptor::sky_look::look(picture.hash);
                gpu_sky_look(look.size_per_mille, look.smooth ? 1 : 0);
                gpu_sky_extension(0, 0, 0, nullptr);
                const int32_t rows = disruptor::sky_look::grown_rows(look, extension::kRows);
                g_grower.ask({revision, std::move(picture), palette, rows});
            }
        }
    }
    if (std::optional<Grown> grown = g_grower.take(); grown && grown->revision == g_revision && grown->made.rows > 0)
        gpu_sky_extension(grown->made.width, grown->made.rows, grown->made.band, grown->made.indices.data());
}
