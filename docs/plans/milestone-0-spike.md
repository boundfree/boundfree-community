---
title: Milestone 0：技术验证 Spike 实施计划
doc_type: plan
status: completed
owner: BoundFree
last_updated: 2026-07-06
milestone: M0
---

# Milestone 0：技术验证 Spike 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 2–3 天验证本架构的两个最高风险点（256ch GPU 波形渲染、C++↔Python 边车 IPC），并对旧 Python 版做性能画像，产出 go/no-go 结论。

**Architecture:** 一个最小 C++/Qt6 可执行程序做 OpenGL 折线渲染基准；一个最小 Python 边车经 stdio（JSON 控制 + 长度前缀二进制数据）响应 `open`/`get_slice`；一个 C++ 客户端测 IPC 延迟与吞吐；用现有 Python 仓库做性能画像。全部代码放在 `native/spike/` 与 `sidecar/spike/`，spike 结束后可丢弃或演进为 M1 骨架。

**Tech Stack:** C++17、Qt 6（Widgets + OpenGL）、CMake、vcpkg、Python 3、现有 `../boundfree-eeg-upper-computer`。

## Global Constraints

- 语言/框架 C++/Qt6，构建 CMake + vcpkg。
- IPC：JSON 控制 over stdio + 长度前缀二进制数据通道。
- 渲染：自研 QOpenGLWidget，仅上传可见窗口数据。
- EEG 数据 `[n_channels, n_samples]`，float32，单位 μV。
- 禁止 FFTW/KFR（GPL）。
- spike 代码隔离在 `native/spike/`、`sidecar/spike/`，不与后续正式代码耦合。

---

## Task 0：环境与工具链前置检查

**Files:**
- Create: `native/spike/README.md`

**Interfaces:**
- Produces: 一份确认过的工具链版本记录，供后续里程碑复用。

- [ ] **Step 1: 检查工具链是否就绪**

Run:

```bash
cmake --version && python3 --version && (qmake6 --version 2>/dev/null || qmake --version 2>/dev/null || echo "Qt qmake not on PATH")
```

Expected: CMake ≥ 3.21，Python ≥ 3.10。若 Qt 未安装，记录下来在 Step 2 处理。

- [ ] **Step 2: 确认 Qt6 与 vcpkg 可用**

Run:

```bash
(vcpkg version 2>/dev/null || echo "vcpkg missing") && ls ~/Qt 2>/dev/null || echo "Qt install dir not found"
```

若 Qt6/vcpkg 缺失，按各平台安装（Qt online installer 选 Qt 6.x + Qt OpenGL 模块；vcpkg 克隆并 bootstrap）。把最终确定的 Qt6 路径与 vcpkg 路径写入 `native/spike/README.md`。

- [ ] **Step 3: 记录环境**

在 `native/spike/README.md` 写下：OS、Qt 版本与路径、CMake 版本、编译器（MSVC/clang/gcc）版本、GPU 型号。这些是渲染基准结果的解释依据。

- [ ] **Step 4: Commit**

```bash
git add native/spike/README.md
git commit -m "spike: 记录 M0 工具链环境"
```

---

## Task 1：最小 CMake + Qt6 窗口

**Files:**
- Create: `native/spike/CMakeLists.txt`
- Create: `native/spike/main.cpp`

**Interfaces:**
- Produces: 可编译运行、能弹出空窗口的 Qt6 程序 `bf_spike`。

- [ ] **Step 1: 写 CMakeLists**

```cmake
cmake_minimum_required(VERSION 3.21)
project(bf_spike LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
add_executable(bf_spike main.cpp waveform_gl.cpp ipc_client.cpp)
target_link_libraries(bf_spike PRIVATE Qt6::Widgets Qt6::OpenGLWidgets)
```

- [ ] **Step 2: 写 main.cpp（先只开窗口）**

```cpp
#include <QApplication>
#include <QMainWindow>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QMainWindow win;
    win.resize(1280, 800);
    win.setWindowTitle("BoundFree Spike");
    win.show();
    return app.exec();
}
```

- [ ] **Step 3: 配置并编译**

Run:

```bash
cd native/spike && cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH="$QT6_DIR" && cmake --build build
```

Expected: 编译成功，产出 `bf_spike` 可执行文件（此时 `waveform_gl.cpp`/`ipc_client.cpp` 还不存在会失败——先把它们从 CMake 里去掉或创建空占位再逐步补；建议先只留 `main.cpp` 编过，再在 Task 2/4 加入）。

> 提示：为让本步单独可编，先把 `add_executable` 改为只含 `main.cpp`，Task 2 再加 `waveform_gl.cpp`，Task 4 再加 `ipc_client.cpp`。

- [ ] **Step 4: 运行确认窗口**

Run: `./build/bf_spike`（无头环境用 `QT_QPA_PLATFORM=offscreen ./build/bf_spike` 确认不崩溃）
Expected: 弹出 1280×800 空窗口，或 offscreen 正常退出。

- [ ] **Step 5: Commit**

```bash
git add native/spike/CMakeLists.txt native/spike/main.cpp
git commit -m "spike: 最小 Qt6 窗口可编译运行"
```

---

## Task 2：Spike-1 — 256ch OpenGL 波形渲染基准

**Files:**
- Create: `native/spike/waveform_gl.h`
- Create: `native/spike/waveform_gl.cpp`
- Modify: `native/spike/main.cpp`

**Interfaces:**
- Produces: `WaveformGL` — 一个 `QOpenGLWidget`，`setData(int nCh, int nSamp, const float* uv)` 渲染多通道折线，窗口标题实时显示 FPS。

- [ ] **Step 1: 写 WaveformGL 头文件**

```cpp
#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <vector>

class WaveformGL : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit WaveformGL(QWidget* parent = nullptr);
    void setData(int nCh, int nSamp, const float* uv);   // uv: [nCh*nSamp] μV, 行主序
protected:
    void initializeGL() override;
    void paintGL() override;
private:
    int nCh_ = 0, nSamp_ = 0;
    std::vector<float> verts_;         // 交错 (x_norm, ch_index, uv)
    QOpenGLShaderProgram prog_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    bool dirty_ = false;
};
```

- [ ] **Step 2: 实现 WaveformGL（顶点着色器做通道偏移+幅值缩放）**

```cpp
#include "waveform_gl.h"

static const char* kVert = R"(
attribute float aX;        // 归一化 [0,1]
attribute float aCh;       // 通道号
attribute float aUv;       // μV
uniform int uNCh;
uniform float uGain;       // 像素/μV
void main() {
    float laneH = 2.0 / float(uNCh);
    float yCenter = 1.0 - laneH * (aCh + 0.5);
    float y = yCenter + aUv * uGain;
    float x = aX * 2.0 - 1.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)";
static const char* kFrag = R"(
void main() { gl_FragColor = vec4(0.41, 0.14, 0.48, 1.0); }
)";

WaveformGL::WaveformGL(QWidget* p) : QOpenGLWidget(p) {}

void WaveformGL::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(1,1,1,1);
    prog_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVert);
    prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFrag);
    prog_.link();
    vbo_.create();
}

void WaveformGL::setData(int nCh, int nSamp, const float* uv) {
    nCh_ = nCh; nSamp_ = nSamp;
    verts_.resize(size_t(nCh) * nSamp * 3);
    for (int c = 0; c < nCh; ++c)
        for (int s = 0; s < nSamp; ++s) {
            size_t i = (size_t(c) * nSamp + s) * 3;
            verts_[i+0] = float(s) / float(nSamp - 1);
            verts_[i+1] = float(c);
            verts_[i+2] = uv[size_t(c) * nSamp + s];
        }
    dirty_ = true;
    update();
}

void WaveformGL::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);
    if (nCh_ == 0) return;
    prog_.bind();
    vbo_.bind();
    if (dirty_) { vbo_.allocate(verts_.data(), int(verts_.size()*sizeof(float))); dirty_ = false; }
    int stride = 3 * sizeof(float);
    prog_.enableAttributeArray("aX");  prog_.setAttributeBuffer("aX", GL_FLOAT, 0, 1, stride);
    prog_.enableAttributeArray("aCh"); prog_.setAttributeBuffer("aCh", GL_FLOAT, sizeof(float), 1, stride);
    prog_.enableAttributeArray("aUv"); prog_.setAttributeBuffer("aUv", GL_FLOAT, 2*sizeof(float), 1, stride);
    prog_.setUniformValue("uNCh", nCh_);
    prog_.setUniformValue("uGain", 0.0008f);
    for (int c = 0; c < nCh_; ++c) glDrawArrays(GL_LINE_STRIP, c * nSamp_, nSamp_);
    vbo_.release(); prog_.release();
}
```

- [ ] **Step 3: 在 main.cpp 驱动 256ch@1kHz 合成信号 + FPS 计时**

```cpp
#include <QApplication>
#include <QMainWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <cmath>
#include <vector>
#include "waveform_gl.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QMainWindow win; win.resize(1280, 800);
    auto* gl = new WaveformGL(); win.setCentralWidget(gl); win.show();

    const int nCh = 256, fs = 1000, winSec = 4;
    const int nSamp = fs * winSec;                 // 一屏 4s 数据
    std::vector<float> buf(size_t(nCh) * nSamp);
    double phase = 0.0;

    auto* frameTimer = new QTimer(&win);
    QElapsedTimer clk; clk.start();
    int frames = 0; qint64 last = 0;
    QObject::connect(frameTimer, &QTimer::timeout, [&](){
        for (int c = 0; c < nCh; ++c)
            for (int s = 0; s < nSamp; ++s)
                buf[size_t(c)*nSamp + s] = 50.0f * std::sin(phase + 0.02*s + 0.1*c);
        phase += 0.05;
        gl->setData(nCh, nSamp, buf.data());
        if (++frames, clk.elapsed() - last >= 1000) {
            win.setWindowTitle(QString("Spike  %1ch@%2Hz  FPS=%3").arg(nCh).arg(fs).arg(frames));
            frames = 0; last = clk.elapsed();
        }
    });
    frameTimer->start(0);   // 尽可能快，测上限
    return app.exec();
}
```

- [ ] **Step 4: 编译运行，记录 FPS**

Run: `cmake --build build && ./build/bf_spike`
Expected: 标题栏显示 FPS。记录 256ch、128ch、64ch 三档的稳定 FPS 与 CPU/GPU 占用。

- [ ] **Step 5: 退出判据（Spike-1）**

- ✅ 通过：256ch@1kHz 一屏 4s 数据下**稳定 ≥ 55 FPS**（或至少 ≥ 30 FPS 且交互无卡顿）。
- ⚠️ 边界：若 <30 FPS，记录瓶颈（顶点量/上传/draw call），在报告中评估优化项（抽稀、instancing、单次 draw + primitive restart）。

把三档 FPS 结果写入 `native/spike/README.md`。

- [ ] **Step 6: Commit**

```bash
git add native/spike/waveform_gl.h native/spike/waveform_gl.cpp native/spike/main.cpp native/spike/README.md
git commit -m "spike: 256ch OpenGL 波形渲染基准与 FPS 记录"
```

---

## Task 3：Spike-2a — 最小 Python 边车（stdio + JSON + 二进制）

**Files:**
- Create: `sidecar/spike/sidecar.py`

**Interfaces:**
- Produces: 一个从 stdin 读「4 字节大端长度 + JSON 请求」、向 stdout 写「4 字节长度 + JSON 响应」或「4 字节长度 + 二进制数组」的边车。
  - `open {path}` → JSON `{fs, ch_names, duration, n_channels}`
  - `get_slice {t0,t1}` → 二进制 `[int32 nCh][int32 nSamp][float32 payload 行主序]`

- [ ] **Step 1: 写 sidecar.py（复用参考仓库读取器）**

```python
import sys, os, json, struct, numpy as np

REF = os.path.join(os.path.dirname(__file__), "..", "..", "..", "boundfree-eeg-upper-computer", "boundfree_eeg")
sys.path.insert(0, os.path.abspath(REF))
from eeg_io.readers import read_file_lazy   # 复用旧版懒加载读取器

_reader = None

def _read_frame():
    hdr = sys.stdin.buffer.read(4)
    if len(hdr) < 4:
        return None
    n = struct.unpack(">I", hdr)[0]
    return json.loads(sys.stdin.buffer.read(n).decode("utf-8"))

def _write_json(obj):
    b = json.dumps(obj).encode("utf-8")
    sys.stdout.buffer.write(struct.pack(">I", len(b))); sys.stdout.buffer.write(b); sys.stdout.buffer.flush()

def _write_array(arr):
    arr = np.ascontiguousarray(arr, dtype=np.float32)
    payload = struct.pack(">ii", arr.shape[0], arr.shape[1]) + arr.tobytes()
    sys.stdout.buffer.write(struct.pack(">I", len(payload))); sys.stdout.buffer.write(payload); sys.stdout.buffer.flush()

def main():
    global _reader
    while True:
        req = _read_frame()
        if req is None:
            break
        m = req.get("method")
        try:
            if m == "open":
                _reader = read_file_lazy(req["path"])
                _write_json({"fs": _reader.fs, "n_channels": _reader.n_channels,
                             "duration": _reader.duration, "ch_names": list(_reader.ch_names)})
            elif m == "get_slice":
                _write_array(_reader.get_slice(float(req["t0"]), float(req["t1"])))
            elif m == "close":
                _reader = None; _write_json({"ok": True})
            else:
                _write_json({"error_code": "unknown_method", "params": {"method": m}})
        except Exception as e:
            _write_json({"error_code": "sidecar.exception", "params": {"msg": str(e)}})

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: 单独冒烟测试边车**

Run（用一个已有样本文件，路径按参考仓库 `samples/` 实际内容替换）:

```bash
cd sidecar/spike && python3 - <<'PY'
import subprocess, struct, json
p = subprocess.Popen(["python3","sidecar.py"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def send(o):
    b=json.dumps(o).encode(); p.stdin.write(struct.pack(">I",len(b))+b); p.stdin.flush()
def recv():
    n=struct.unpack(">I",p.stdout.read(4))[0]; return p.stdout.read(n)
send({"method":"open","path":"../../../boundfree-eeg-upper-computer/samples/mark.edf"})
print("open ->", recv()[:200])
send({"method":"get_slice","t0":0.0,"t1":1.0})
raw=recv(); import struct as s; print("slice shape", s.unpack(">ii", raw[:8]))
PY
```

Expected: 打印出 `open ->` 的 JSON 元数据与切片 shape `(n_ch, n_samp)`。

- [ ] **Step 3: Commit**

```bash
git add sidecar/spike/sidecar.py
git commit -m "spike: 最小 Python 边车 open/get_slice over stdio"
```

---

## Task 4：Spike-2b — C++ IPC 客户端与延迟/吞吐测量

**Files:**
- Create: `native/spike/ipc_client.h`
- Create: `native/spike/ipc_client.cpp`
- Create: `native/spike/ipc_bench_main.cpp`
- Modify: `native/spike/CMakeLists.txt`

**Interfaces:**
- Consumes: 边车协议（Task 3）。
- Produces: `SidecarClient` — `open(path)->Meta`、`getSlice(t0,t1)->(nCh,nSamp,vector<float>)`；一个 bench 程序打印往返延迟与吞吐。

- [ ] **Step 1: 写 SidecarClient 头文件**

```cpp
#pragma once
#include <QProcess>
#include <vector>
#include <string>

struct SliceResult { int nCh = 0, nSamp = 0; std::vector<float> uv; };

class SidecarClient {
public:
    bool start(const QString& python, const QString& script);
    bool open(const QString& path, QString* metaJsonOut);
    bool getSlice(double t0, double t1, SliceResult* out);
    void stop();
private:
    QProcess proc_;
    void writeJson(const QByteArray& json);
    QByteArray readFrame();   // 读 4 字节长度 + payload
};
```

- [ ] **Step 2: 实现 SidecarClient（QProcess + 帧协议）**

```cpp
#include "ipc_client.h"
#include <QByteArray>
#include <QtEndian>

static QByteArray u32be(quint32 n){ QByteArray b(4,0); qToBigEndian(n,(uchar*)b.data()); return b; }

bool SidecarClient::start(const QString& python, const QString& script){
    proc_.start(python, {script});
    return proc_.waitForStarted(3000);
}
void SidecarClient::writeJson(const QByteArray& j){
    proc_.write(u32be(j.size())); proc_.write(j); proc_.waitForBytesWritten(2000);
}
QByteArray SidecarClient::readFrame(){
    while (proc_.bytesAvailable() < 4) proc_.waitForReadyRead(5000);
    QByteArray h = proc_.read(4); quint32 n = qFromBigEndian<quint32>((const uchar*)h.constData());
    QByteArray p; while ((quint32)p.size() < n){ if(!proc_.waitForReadyRead(5000) && proc_.bytesAvailable()==0) break; p += proc_.read(n - p.size()); }
    return p;
}
bool SidecarClient::open(const QString& path, QString* out){
    writeJson(QByteArray("{\"method\":\"open\",\"path\":\"") + path.toUtf8() + "\"}");
    QByteArray r = readFrame(); if(out) *out = QString::fromUtf8(r); return !r.isEmpty();
}
bool SidecarClient::getSlice(double t0, double t1, SliceResult* o){
    QByteArray req = QString("{\"method\":\"get_slice\",\"t0\":%1,\"t1\":%2}").arg(t0).arg(t1).toUtf8();
    writeJson(req);
    QByteArray r = readFrame(); if(r.size() < 8) return false;
    o->nCh   = qFromBigEndian<qint32>((const uchar*)r.constData());
    o->nSamp = qFromBigEndian<qint32>((const uchar*)r.constData()+4);
    size_t cnt = size_t(o->nCh) * o->nSamp; o->uv.resize(cnt);
    memcpy(o->uv.data(), r.constData()+8, cnt*sizeof(float));   // 注意：边车用大端，正式版需统一字节序
    return true;
}
void SidecarClient::stop(){ proc_.closeWriteChannel(); proc_.waitForFinished(2000); proc_.kill(); }
```

> 记录问题：本 spike 边车用大端写 payload，x86 是小端；正式版应统一为小端零转换。此处仅测延迟/吞吐，数值正确性在 M3 用黄金文件保证。

- [ ] **Step 3: 写 bench main 测延迟与吞吐**

```cpp
#include "ipc_client.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstdio>

int main(int argc, char** argv){
    QCoreApplication app(argc, argv);
    if (argc < 4){ printf("usage: ipc_bench <python> <sidecar.py> <sample_file>\n"); return 1; }
    SidecarClient c;
    if(!c.start(argv[1], argv[2])){ printf("start failed\n"); return 1; }
    QString meta; c.open(argv[3], &meta); printf("meta: %s\n", meta.toUtf8().constData());
    QElapsedTimer t; SliceResult r;
    const int N = 100; t.start();
    for (int i=0;i<N;++i){ double s=i*0.5; c.getSlice(s, s+1.0, &r); }
    double ms = double(t.elapsed())/N;
    printf("avg get_slice(1s) round-trip: %.2f ms, last shape=%dx%d\n", ms, r.nCh, r.nSamp);
    c.stop(); return 0;
}
```

- [ ] **Step 4: 更新 CMake 增加 bench 目标**

在 `CMakeLists.txt` 追加：

```cmake
add_executable(ipc_bench ipc_client.cpp ipc_bench_main.cpp)
target_link_libraries(ipc_bench PRIVATE Qt6::Core)
```

（并把 `ipc_client.cpp` 一并加入 `bf_spike` 或保持独立均可。）

- [ ] **Step 5: 编译运行 bench**

Run:

```bash
cmake --build build && ./build/ipc_bench python3 ../../sidecar/spike/sidecar.py ../../../boundfree-eeg-upper-computer/samples/mark.edf
```

Expected: 打印元数据与平均 `get_slice(1s)` 往返延迟。

- [ ] **Step 6: 退出判据（Spike-2）**

- ✅ 通过：常见回放粒度（0.5–2s 切片）往返延迟 **< 30 ms**（软实时回放可接受，且可预取）。
- ⚠️ 边界：若延迟高或大切片吞吐不足，记录数据，在报告中评估共享内存升级。

结果写入 `native/spike/README.md`。

- [ ] **Step 7: Commit**

```bash
git add native/spike/ipc_client.h native/spike/ipc_client.cpp native/spike/ipc_bench_main.cpp native/spike/CMakeLists.txt native/spike/README.md
git commit -m "spike: C++ 边车 IPC 客户端与延迟吞吐基准"
```

---

## Task 5：旧 Python 版性能画像

**Files:**
- Create: `native/spike/profile_python_baseline.md`

**Interfaces:**
- Produces: 旧版在 64/128/256ch 场景下渲染/DSP/切片IO 的耗时占比基线。

- [ ] **Step 1: 确认参考仓库可运行测试**

Run:

```bash
cd ../../boundfree-eeg-upper-computer && ls .venv_build/bin/python && .venv_build/bin/python -m pytest tests/test_playback_performance.py tests/test_waveform_performance.py -q
```

Expected: 现有性能测试通过，作为对照基线。

- [ ] **Step 2: 用 cProfile 画像切片 + DSP 路径**

Run（在参考仓库根，用一个较大样本文件；无样本时用现有测试里的合成数据脚本思路）:

```bash
.venv_build/bin/python -c "
import cProfile, pstats, numpy as np, sys; sys.path.insert(0,'boundfree_eeg')
from processing.pipeline import ProcessingPipeline, ProcessingConfig, ChannelBandpassSetting
from processing.reference import ReferenceMode
fs=1000.0; nch=256; data=np.random.default_rng(0).normal(size=(nch, fs*10)).astype('float64')
p=ProcessingPipeline()
cfg=ProcessingConfig(fs=fs,n_channels=nch,reference_mode='car',notch_freqs=(50.0,100.0,150.0),bandpass=tuple(ChannelBandpassSetting(True,1.0,100.0) for _ in range(nch)))
p.rebuild(cfg)
cProfile.run('[p.process_offline(data) for _ in range(5)]','prof.out')
pstats.Stats('prof.out').sort_stats('cumulative').print_stats(15)
"
```

Expected: 打印 DSP 各函数累计耗时。记录 256ch 下重参考/陷波/带通/整体的耗时。

- [ ] **Step 3: 记录渲染侧观察**

启动旧版 GUI（若环境允许），打开一个高通道数文件，主观记录：256ch 实时波形帧率是否掉、seek 是否卡、内存随时长增长情况。无 GUI 环境则依据 `tests/test_waveform_performance.py` 的 64ch 预算测试推断。

- [ ] **Step 4: 写画像结论**

在 `native/spike/profile_python_baseline.md` 记录：渲染 vs DSP vs 切片IO 的耗时占比与主观卡顿点，明确「瓶颈是否系统性（支持走原生）」。

- [ ] **Step 5: Commit**

```bash
cd ../../boundfree-eeg-native && git add native/spike/profile_python_baseline.md
git commit -m "spike: 旧 Python 版性能画像基线"
```

---

## Task 6：Spike 报告与 go/no-go 决策

**Files:**
- Create: `native/spike/SPIKE_REPORT.md`

- [ ] **Step 1: 汇总结论**

在 `native/spike/SPIKE_REPORT.md` 写：
- Spike-1 渲染 FPS 三档结果 + 是否达标 + 若不达标的优化路径。
- Spike-2 IPC 延迟/吞吐结果 + 是否需要共享内存。
- Python 画像结论。
- **最终 go/no-go**：是否进入 M1；若需修正设计，明确修正点（回写 `docs/architecture-design.md`）。

- [ ] **Step 2: Commit**

```bash
git add native/spike/SPIKE_REPORT.md
git commit -m "spike: M0 验证报告与 go/no-go 结论"
```

## Acceptance Criteria（M0 通过条件）

- 256ch@1kHz OpenGL 渲染帧率有实测数据，且达标或有明确优化路径。
- C++↔Python 边车 `open`/`get_slice` 往返延迟有实测数据，且满足软实时回放或有共享内存升级路径。
- 旧版性能画像完成，瓶颈定位清晰。
- `SPIKE_REPORT.md` 给出明确 go/no-go；若 go，则 M1 可直接复用本 spike 的 CMake/IPC/渲染原型。

## Residual Risks

- Spike 用大端二进制、单向阻塞读，正式版需改小端零拷贝 + 异步读，M1 处理。
- 无头 CI 环境无法测真实 GPU FPS，需在有 GPU 的开发机上跑 Spike-1。
- 计划中使用参考仓库真实样本 `samples/mark.edf`（9MB EDF）；如需更大文件测吞吐可另取。
