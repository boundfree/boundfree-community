---
title: BoundFree EEG 在线激活系统设计方案
doc_type: architecture
status: superseded
owner: BoundFree
last_updated: 2026-10-23
archived_at: 2026-10-23
---

# BoundFree EEG 在线激活系统设计方案

> 版本：v1.1
> 日期：2026-08-12
> 状态：已实施，待 Windows 发布机验收

---

## 1. 设计目标

将当前纯离线（手动运行 `issue_license.py` → 邮件/微信发送激活码 → 用户粘贴）的激活流程升级为**在线自动激活**，同时保留离线 fallback。

核心约束：
- **不改变激活码格式**——服务端签发的仍是现有 RSA-2048 PSS 激活码，客户端验证逻辑一字不改
- **不改变 license.dat 存储路径和格式**
- **不改变机器指纹算法**（SHA-256(Windows MachineGuid)[:32]）
- **license-authority/ 私钥永不进入客户端或前端仓库**

实施状态（2026-08-12）：
- Native 已实现自助申请、邀请码激活、审批后自动获取许可证和离线粘贴回退。
- 客户端已按服务端 `error.code + params` 契约处理重复注册、待审批、拒绝原因及网络失败；`license.dat` 使用原子提交，在线下发密钥在落盘前由本机公钥再次验证。
- 本地许可证增加单调 UTC 时间水位：每次本地校验或离线保存都会验证并推进 `.last_run`；缺失、损坏或超过 300 秒的时间回拨会阻止启动和离线覆盖。仅在本机重新验证服务端新签发授权后，才允许恢复该状态。
- 当前 WSL2 回归已覆盖本地 HTTP 协议状态转移、许可证模块、时间水位端到端路径与发布公钥门禁；RSA-PSS 真实验证和完整 GUI 激活仍须在 Windows 发布机使用正式密钥验收。

---

## 2. 两种激活模式共存

### 2.1 模式 A：自助申请 + 管理员审批（全自动）

```
用户打开客户端 → 未激活 → 看到激活对话框
    ├── [申请激活] → POST /api/v1/register (machine_id)
    │       → 服务端记录为 "pending_approval"
    │       → 提示用户："申请已提交，请等待管理员审批"
    │
    └── 后续每次启动 → POST /api/v1/status (machine_id)
            ├── 审批通过 → 自动下发激活码 → 写入 license.dat → 进入主界面
            └── 仍在等待 → 继续提示 + 显示排队状态

管理员侧：
    后台看到待审批列表 → 查看 machine_id、申请时间、IP
    → 设天数 + 备注 + [批准] → 激活码自动签发
    → 用户下次启动自动获得
```

**适用场景**：用户自己找上门想用软件、公网分发

### 2.2 模式 B：邀请码激活（管理员主动分发）

```
管理员在后台 → 生成邀请码 "BF-A3F1-B2C4"（可设天数、备注）
    → 通过微信/邮件发送给用户

用户打开客户端 → 未激活 → 看到激活对话框
    ├── [有邀请码] → 输入邀请码
    │       → POST /api/v1/activate (machine_id + invite_code)
    │       → 服务端验证邀请码有效且未使用
    │       → 绑定 machine_id → 签发激活码 → 写入 license.dat
    │       → 进入主界面
    │
    └── [申请激活] → 进入模式 A 流程
```

**适用场景**：内部团队批量部署、展会试用、定向分发

---

## 3. 两种模式的关系

| 维度 | 模式 A（自助申请） | 模式 B（邀请码） |
|------|:--:|:--:|
| 发起方 | 用户 | 管理员 |
| 是否需要管理员实时操作 | 是（审批一次） | 否（预生成邀请码） |
| 用户体验 | 需等待审批 | 即时激活 |
| 控制力 | 管理员可审核 | 管理员主动控制分发范围 |
| 数据库记录来源 | `source = "registration"` | `source = "invitation"` |

两者**共享同一张 License 表和同一套签名签发逻辑**，区别仅在于 license 记录的 `source` 字段和 `invite_code` 是否为空。

---

## 4. 架构总览

```
                         ┌─────────────────────────────────┐
                         │     云服务器 (Ubuntu 22.04)        │
                         │                                  │
                         │  Nginx :443 (Let's Encrypt)      │
                         │  ├── /        → React SPA 静态文件  │
                         │  ├── /api/v1/* → Django DRF      │
                         │  ├── /api/mgmt/* → Django DRF    │
                         │  └── /admin/ → Django Admin      │
                         │                                  │
                         │  Django (uWSGI / Gunicorn)       │
                         │  ├── SQLite 数据库                 │
                         │  └── license-authority/ 签名字模块  │
                         │      ├── private_key.pem（签名用）  │
                         │      └── public_key.pem （验证用）  │
                         └─────────────────────────────────┘
                                    │ HTTPS
                                    │
        ┌───────────────────────────┼───────────────────────────┐
        │                           │                           │
        ▼                           ▼                           ▼
   用户客户端 A                 用户客户端 B                 用户客户端 C
   (Windows)                   (Windows)                   (Windows)
   ┌─────────────────┐
   │ bf_app.exe       │
   │                  │
   │ LicenseManager   │ ← 现有离线验证，一字不改
   │   ├── checkSaved │
   │   ├── verifyKey  │
   │   └── saveLicense│
   │                  │
   │ OnlineActivation │ ← 新增：HTTP 客户端
   │   ├── register() │
   │   ├── activate() │
   │   └── status()   │
   │                  │
   │ ActivationDialog │ ← 改造：增加双模式入口
   └─────────────────┘
```

---

## 5. 数据库设计

### 5.1 License 表

```python
class License(models.Model):
    """激活记录，两种模式共用"""

    # 机器标识
    machine_id = models.CharField(max_length=32, unique=True, db_index=True)

    # 激活状态
    class Status(models.TextChoices):
        PENDING_APPROVAL = "pending_approval"  # 模式A：等待管理员审批
        ACTIVE = "active"                      # 已激活
        EXPIRED = "expired"                    # 已过期
        REVOKED = "revoked"                    # 已吊销

    status = models.CharField(max_length=20, choices=Status.choices, default=Status.PENDING_APPROVAL)

    # 来源
    class Source(models.TextChoices):
        REGISTRATION = "registration"  # 模式A：自助申请
        INVITATION = "invitation"      # 模式B：邀请码

    source = models.CharField(max_length=20, choices=Source.choices)

    # 邀请码（仅 source=invitation 时有值）
    invite_code = models.CharField(max_length=20, null=True, blank=True, unique=True, db_index=True)

    # 有效期
    expires_at = models.DateField()
    activated_at = models.DateTimeField(null=True, blank=True)

    # 管理员备注
    admin_note = models.TextField(blank=True, default="")

    # 时间戳
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        ordering = ["-created_at"]
```

### 5.2 ActivationLog 表

```python
class ActivationLog(models.Model):
    """激活事件日志（只读审计）"""

    class Action(models.TextChoices):
        REGISTERED = "registered"          # 用户提交申请
        APPROVED = "approved"              # 管理员审批
        REJECTED = "rejected"              # 管理员拒绝
        ACTIVATED = "activated"            # 客户端获取激活码成功
        RENEWED = "renewed"                # 续期
        REVOKED = "revoked"                # 吊销
        INVITE_GENERATED = "invite_generated"  # 生成邀请码

    license = models.ForeignKey(License, on_delete=models.CASCADE, related_name="logs")
    action = models.CharField(max_length=20, choices=Action.choices)
    detail = models.JSONField(default=dict)  # 额外信息（如天数变更、操作人）
    ip_address = models.GenericIPAddressField(null=True, blank=True)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        ordering = ["-created_at"]
```

### 5.3 InviteCode 表（可选，与 License 的 invite_code 字段冗余设计）

考虑到简单场景，邀请码可以直接存在 License 表的 `invite_code` 字段中。如果未来需要更复杂的邀请码管理（批量生成、使用次数限制等），再独立此表。

当前方案：**邀请码存于 License.invite_code**，一个邀请码对应一条 License 记录（预创建，machine_id 初始为空，激活时回填）。

---

## 6. API 设计

### 6.1 公开 API（客户端调用，无认证 / 限速）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/v1/register` | 提交激活申请（模式A） |
| POST | `/api/v1/activate` | 邀请码激活（模式B）+ 获批后首次获取激活码 |
| GET | `/api/v1/status?machine_id=xxx` | 查询申请/激活状态 |
| GET | `/api/v1/health` | 健康检查 |

#### POST /api/v1/register

```json
// Request
{
    "machine_id": "F807FDFB2C3A4D9B7E1F5A8C3D6B0E2F",
    "client_version": "0.1.50-dev"
}

// Response 202
{
    "status": "pending_approval",
    "message": "申请已提交，请等待管理员审批。",
    "request_id": "abc123"
}

// Response 409 (已有记录)
{
    "status": "already_exists",
    "message": "该设备已有激活记录。",
    "current_status": "pending_approval"  // 或 active/expired/revoked
}
```

#### POST /api/v1/activate

```json
// Request
{
    "machine_id": "F807FDFB2C3A4D9B7E1F5A8C3D6B0E2F",
    "invite_code": "BF-A3F1-B2C4",
    "client_version": "0.1.50-dev"
}

// Response 200 (成功)
{
    "status": "activated",
    "license_key": "aB3dEfGh...（完整激活码）",
    "expires_at": "2027-07-29"
}

// Response 404
{
    "status": "invalid_invite_code",
    "message": "邀请码无效或已被使用。"
}

// Response 403
{
    "status": "already_activated",
    "message": "该邀请码已被其他设备使用。"
}
```

> **关键**：`/api/v1/activate` 同时服务于两种场景：
> 1. 模式B：用户输入邀请码 → machine_id + invite_code → 签发
> 2. 模式A审批后：用户 next 启动 → machine_id only（无 invite_code）→ 查找 `machine_id` 且 `status=active` → 签发

#### GET /api/v1/status?machine_id=xxx

```json
// Response 200
{
    "machine_id": "F807FDFB...",
    "status": "pending_approval",  // pending_approval | active | expired | revoked | not_found
    "expires_at": null,            // 审批通过后才有
    "message": "等待管理员审批中"
}
```

### 6.2 管理 API（React 前端调用，JWT 认证）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/mgmt/auth/login` | 管理员登录 |
| GET | `/api/mgmt/licenses/` | 激活记录列表（分页+搜索+筛选） |
| POST | `/api/mgmt/licenses/` | 预创建授权（模式B：生成邀请码） |
| GET | `/api/mgmt/licenses/<id>/` | 详情 |
| PATCH | `/api/mgmt/licenses/<id>/` | 编辑（±天数、备注） |
| POST | `/api/mgmt/licenses/<id>/approve/` | 审批通过（模式A） |
| POST | `/api/mgmt/licenses/<id>/reject/` | 拒绝申请 |
| POST | `/api/mgmt/licenses/<id>/revoke/` | 吊销 |
| GET | `/api/mgmt/logs/` | 激活日志 |
| GET | `/api/mgmt/stats/` | 仪表盘统计 |

---

## 7. 客户端改造

### 7.1 新增类：OnlineActivationClient

```cpp
// native/src/license/online_activation_client.h
namespace bf {

class OnlineActivationClient : public QObject {
    Q_OBJECT
public:
    explicit OnlineActivationClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // 模式A：提交激活申请
    void registerDevice(const QString& machineId, const QString& clientVersion);

    // 模式B：邀请码激活
    void activateWithCode(const QString& machineId, const QString& inviteCode,
                          const QString& clientVersion);

    // 模式A：查询审批状态
    void checkStatus(const QString& machineId);

    // 审批通过后获取激活码（也可复用 activateWithCode，不带 invite_code）
    void fetchLicense(const QString& machineId);

signals:
    void registrationSubmitted();                    // 申请已提交
    void activationSuccess(const QString& licenseKey, const QString& expiresAt);
    void activationFailed(const QString& reason);
    void statusReceived(const QString& status, const QString& message);
    void networkError(const QString& detail);

private:
    QNetworkAccessManager* nam_;
    QUrl serverUrl_;
};

}  // namespace bf
```

### 7.2 改造：ActivationDialog（替代当前 QInputDialog）

```
┌─────────────────────────────────────────────┐
│  🔐 BoundFree EEG 激活                      │
│                                              │
│  本设备机器码：                               │
│  ┌──────────────────────────────────────┐    │
│  │ F807-FDFB-2C3A-4D9B  [📋 复制]      │    │
│  └──────────────────────────────────────┘    │
│                                              │
│  ┌──────────────┐  ┌──────────────────┐     │
│  │  📝 申请激活   │  │  🎫 有邀请码      │     │
│  └──────────────┘  └──────────────────┘     │
│                                              │
│  ── 离线激活 ────────────────────────────   │
│  ┌──────────────────────────────────────┐    │
│  │ 粘贴激活码（离线/已收到管理员回复后使用）│    │
│  │ [                            ]      │    │
│  └──────────────────────────────────────┘    │
│  [激活]                                      │
│                                              │
│  ┌──────────────────────────────────────┐    │
│  │ 状态：等待管理员审批中...  [🔄 刷新]    │    │
│  └──────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
```

交互流程：
1. 首次打开 → 显示机器码 + 两个按钮
2. 点击"申请激活" → POST /register → 显示"等待审批"
3. 点击"有邀请码" → 弹出输入框 → POST /activate → 成功/失败提示
4. 离线粘贴 → 完全保留原有 QInputDialog 逻辑，作为三级 fallback
5. 下次启动 → 自动调用 /status 检查 → 审批通过则自动获取激活码

### 7.3 改造：main.cpp 激活门

```cpp
// 伪代码，示意层级
bool ensureLicense(...) {
    // 1. 检查本地 license.dat（零网络依赖，毫秒级）
    result = manager.checkSavedLicense();
    if (result.ok()) return true;

    // 2. 在线激活（新增）
    if (网络可用) {
        result = showOnlineActivationDialog(manager, machineId, version);
        if (result.ok()) return true;
    }

    // 3. 离线粘贴 fallback（原有逻辑，完全保留）
    while (true) {
        key = QInputDialog::getMultiLineText(...);
        result = manager.verifyKey(key);
        if (result.ok()) {
            manager.saveLicense(key);
            return true;
        }
    }
}
```

### 7.4 服务器地址配置

```
优先级：
1. 编译时 CMake 注入 BF_ACTIVATION_SERVER_URL
2. 运行时配置文件（%APPDATA%\BoundFreeEEG\config.json）
3. 硬编码默认值（发布时设定）
```

---

## 8. 管理后台

### 8.1 Django Admin（保底，零代码）

Django 自带 admin 注册 License 和 ActivationLog 模型后即可实现：
- 列表展示（machine_id、status、expires_at、source）
- 搜索 machine_id
- 按状态筛选
- 自定义 action：批量审批、延长天数、吊销

### 8.2 React 管理界面（增强体验）

技术栈：React 18 + Ant Design 5 + Vite + Axios

#### 页面结构

```
/admin                          ← React SPA
  ├── /login                     ← JWT 登录
  ├── /dashboard                 ← 仪表盘
  │   ├── 活跃设备数
  │   ├── 待审批数
  │   ├── 本月新增
  │   └── 过期趋势图
  ├── /licenses                  ← 激活记录列表
  │   ├── 搜索（machine_id / 备注）
  │   ├── 筛选（状态、来源）
  │   ├── 新建授权（模式B：生成邀请码）
  │   │   → 设天数 + 备注 → 生成邀请码 + 复制
  │   └── 每条记录操作：
  │       ├── 详情抽屉
  │       ├── 延长/减少天数
  │       ├── 修改备注
  │       ├── [审批] / [拒绝]（仅 pending_approval）
  │       └── [吊销]
  └── /logs                      ← 操作日志
```

---

## 9. 安全性

| 层级 | 措施 |
|------|------|
| 传输 | HTTPS 全链路（Let's Encrypt 自动续签） |
| 签名 | 私钥仅在服务端，签发逻辑复用 `issue_license.py` 的 RSA-2048 PSS |
| 客户端验证 | 嵌入的公钥验证签名，一字不改 |
| API 限速 | `/api/v1/*` 每个 IP 每分钟 10 次请求 |
| 管理认证 | JWT（access + refresh token），可选 2FA |
| 私钥保护 | 文件权限 600，不进入版本控制，不进入 Docker 镜像层（volume 挂载） |
| machine_id 安全 | 客户端与服务端均只存 SHA-256 前 32 位哈希，不存原始 MachineGuid |


### 9.1 本地时间水位

客户端在与 `license.dat`（本地授权文件）相同目录保存 `.last_run`，其中包含 UTC 时间和基于机器标识派生的 HMAC-SHA256 校验值。状态写入使用原子替换，正常校验只允许把记录时间向前推进；300 秒以内的回拨不会降低记录，超过该范围会阻止离线授权使用。缺失或格式、校验值异常的状态不会被本地离线流程自动创建或覆盖。

成功在线激活在本机重新验证服务端 RSA 签名后，才会重建或覆盖时间水位，因此它是状态恢复的唯一入口。该文件用于阻止误删、意外损坏和非格式化修改导致的静默降级；它不等同于由硬件安全模块或 Windows DPAPI（Windows 数据保护 API）保护的不可伪造安全存储。具备本机授权文件和机器标识访问权的攻击者仍可能重构校验值；对此威胁模型必须依赖服务端状态校验和 Windows 发布环境的安全能力。
---

## 10. 部署清单

```
服务器要求：
├── Ubuntu 22.04 LTS
├── Docker + Docker Compose（推荐）或裸机部署
├── 已备案域名（如 license.boundfree.com）
└── Let's Encrypt 自动 HTTPS

Docker Compose 服务：
├── nginx       ← 端口 80/443，反向代理 + 静态文件
├── django      ← uWSGI/Gunicorn，端口 8000（内部）
└── （可选）PostgreSQL 替代 SQLite

部署步骤：
1. git clone 服务端仓库
2. 将 license-authority/private_key.pem 放入 server/secrets/
3. docker compose up -d
4. 创建 Django 超级用户
5. 配置 DNS A 记录 → 服务器 IP
6. certbot 签发 HTTPS 证书
```

---

## 11. 实施计划

### Phase 1：服务端核心（预计 1–1.5 周）

| 任务 | 内容 |
|------|------|
| 1.1 | Django 项目初始化（models、migrations、SQLite） |
| 1.2 | 公开 API：register、activate、status、health |
| 1.3 | 签发逻辑封装（复用 issue_license.py 签名核心） |
| 1.4 | 单元测试（API + 签名验证） |
| 1.5 | Docker Compose 配置（Nginx + Django） |
| 1.6 | 部署到服务器 + HTTPS |

### Phase 2：管理后台（预计 1 周）

| 任务 | 内容 |
|------|------|
| 2.1 | Django Admin 注册（立即可用保底） |
| 2.2 | 管理 API：licenses CRUD、approve、reject、revoke |
| 2.3 | JWT 认证 |
| 2.4 | React 项目初始化 + Ant Design |
| 2.5 | 仪表盘页面 |
| 2.6 | 激活记录列表 + 操作页面 |
| 2.7 | 日志查看页面 |

### Phase 3：客户端改造（预计 1 周）

| 任务 | 内容 |
|------|------|
| 3.1 | OnlineActivationClient 实现（QNetworkAccessManager） |
| 3.2 | ActivationDialog UI（替代 QInputDialog） |
| 3.3 | main.cpp 激活门改造（三级 fallback） |
| 3.4 | i18n 新增键值（中/英/日/繁） |
| 3.5 | 服务器地址配置机制 |
| 3.6 | 网络不可用降级测试 |

### Phase 4：集成测试与文档（预计 0.5 周）

| 任务 | 内容 |
|------|------|
| 4.1 | 端到端测试（注册 → 审批 → 激活 → 过期 → 续期） |
| 4.2 | 离线 fallback 测试 |
| 4.3 | 部署文档 + 管理员使用手册 |

---

## 12. 未决问题

| # | 问题 | 建议 | 状态 |
|---|------|------|:--:|
| 1 | 域名？ | 开发阶段先用 IP + 自签证书测试，后续绑定已备案域名 | ✅ |
| 2 | React 是否必要？ | ✅ v1 必须一起做（React 18 + Ant Design 5 + Vite） | ✅ |
| 3 | 服务端仓库 | ✅ 独立仓库 `boundfree-eeg-license-server` | ✅ |
| 4 | 到期提醒 | ✅ v1 暂不做（客户端启动时显示到期倒计时即可） | ✅ |
