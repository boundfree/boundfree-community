#include "ipc_client.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc < 4) {
        std::printf("usage: ipc_bench <python> <sidecar.py> <sample_file>\n");
        return 1;
    }

    SidecarClient client;
    if (!client.start(argv[1], argv[2])) {
        std::printf("start failed\n");
        return 1;
    }

    QString meta;
    QElapsedTimer openTimer;
    openTimer.start();
    if (!client.open(argv[3], &meta)) {
        std::printf("open failed\n");
        client.stop();
        return 1;
    }
    const qint64 openMs = openTimer.elapsed();
    std::printf("meta: %s\n", meta.toUtf8().constData());
    std::printf("open round-trip: %lld ms\n", static_cast<long long>(openMs));

    QElapsedTimer timer;
    SliceResult slice;
    constexpr int kIterations = 100;
    timer.start();
    for (int i = 0; i < kIterations; ++i) {
        const double t0 = static_cast<double>(i) * 0.5;
        if (!client.getSlice(t0, t0 + 1.0, &slice)) {
            std::printf("get_slice failed at iteration %d\n", i);
            client.stop();
            return 1;
        }
    }
    const double avgMs = static_cast<double>(timer.elapsed()) / static_cast<double>(kIterations);
    const double mb = static_cast<double>(slice.uv.size() * sizeof(float)) / (1024.0 * 1024.0);
    const double throughput = mb / (avgMs / 1000.0);
    std::printf("avg get_slice(1s) round-trip: %.2f ms, last shape=%dx%d, payload=%.3f MiB, throughput=%.2f MiB/s\n",
                avgMs, slice.nCh, slice.nSamp, mb, throughput);

    client.stop();
    return 0;
}
