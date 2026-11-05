#define GPU_TEMPORAL_CAMERA_SCALAR 1
#include "gpu_temporal_mesh.h"

int scalar_camera_search(const GpuTemporalCameraSamples *samples, int count, int voters,
                         const GpuTemporalCamera *prior, GpuTemporalCamera *camera) {
    return gpu_temporal_camera_search(samples, count, voters, prior, camera);
}

void scalar_camera_normal(const GpuTemporalCameraSamples *samples, int count,
                          double (*normal)[GPU_TEMPORAL_CAMERA_TERMS + 1]) {
    GpuTemporalCameraSums sums{};
    for (int index = 0; index < count; ++index) gpu_temporal_camera_sums_add(&sums, samples, index);
    gpu_temporal_camera_normal(&sums, normal);
}

int scalar_camera_count(const double *m, const GpuTemporalCameraSamples *samples, int count, double limit,
                        int floor) {
    return gpu_temporal_camera_count(m, samples, count, limit, floor);
}

int scalar_camera_vector_path() { return GPU_TEMPORAL_CAMERA_SSE2; }
