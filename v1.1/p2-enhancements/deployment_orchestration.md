# 第 24 章 部署与编排（v1.1 P2-17）

> v1.1 评审 §2.1 第 24 章 P2 改进动作（v0.4 前完成）。
> 单节点裸机、容器化、多节点集群、K8s CNI 集成、配置管理。

## 24.1 部署形态总览

| 形态 | 适用场景 | 控制面 | 数据面 | 编排 |
|------|---------|--------|--------|------|
| 单节点裸机 | 边缘/小规模 | FRR 单实例 | VPP 单进程 | Debian/Ubuntu 包 |
| 单节点容器 | CI/测试/边缘 | FRR 容器 | VPP 容器 | Docker Compose |
| 多节点集群 | 数据中心/云 | FRR 多实例 | VPP per-node | K8s DaemonSet |
| 分布式控制面 | 大规模/多租户 | FRR 多实例 + 集中 BGP RR | VPP per-node | K8s + Ansible |

## 24.2 单节点裸机部署

### 24.2.1 Debian/Ubuntu 包安装

```bash
# 添加 DANOS-Open 仓库
echo "deb [signed-by=/usr/share/keyrings/danos.gpg] \
  https://packages.danos-open.org/v0.4 stable main" \
  > /etc/apt/sources.list.d/danos.list

apt update && apt install danos-open
```

包内容：
- `danos-core`：核心引擎（事务、Reconciler、事件总线、WAL）
- `danos-dpa`：DPA API 库（C ABI + Protobuf）
- `danos-fib`：FRR FIB Adapter
- `danos-vpp`：VPP Backend
- `danos-mgmt`：CLI + gNMI + NETCONF
- `danos-models`：YANG 模型
- `danos-observability`：Prometheus exporter + 审计
- `danos-security`：CoPP + RBAC + mTLS

### 24.2.2 服务管理

```bash
systemctl enable --now danos-open
systemctl status danos-open
journalctl -u danos-open -f
```

systemd 单元依赖：
```
[Unit]
After=network-online.target vpp.service frr.service
Requires=vpp.service
Wants=frr.service
```

### 24.2.3 配置文件

```
/etc/danos/
├── danos.yaml          # 主配置
├── dpa/                # DPA 后端配置
│   ├── vpp.yaml
│   └── ovs.yaml
├── mgmt/               # 管理面配置
│   ├── gnmi.yaml
│   ├── netconf.yaml
│   └── cli.yaml
├── security/           # 安全配置
│   ├── copp.yaml
│   ├── rbac.yaml
│   └── tls/
└── models/             # YANG 模型覆盖
```

## 24.3 容器化部署

### 24.3.1 OCI 镜像

```dockerfile
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y danos-open && rm -rf /var/lib/apt/lists
COPY danos.yaml /etc/danos/danos.yaml
EXPOSE 57400 830 22
ENTRYPOINT ["/usr/sbin/danos-open", "--config", "/etc/danos/danos.yaml"]
```

镜像分层：
- `danos-open:base`：Debian + 运行时依赖
- `danos-open:vpp`：+ VPP + DPDK
- `danos-open:full`：+ FRR + 管理面 + 可观测性

### 24.3.2 Docker Compose（单节点）

```yaml
version: "3.9"
services:
  danos:
    image: danos-open:full
    privileged: true          # VPP 需访问 NIC
    volumes:
      - ./config:/etc/danos
      - /dev/hugepages:/dev/hugepages
    ports:
      - "57400:57400"         # gNMI
      - "830:830"             # NETCONF
      - "9100:9100"           # Prometheus
    networks:
      - data
      - mgmt
  vpp:
    image: danos-open:vpp
    privileged: true
    volumes:
      - /dev/hugepages:/dev/hugepages
    networks:
      - data
  frr:
    image: frrouting/frr:v8.4
    networks:
      - data
networks:
  data:
  mgmt:
```

### 24.3.3 K8s DaemonSet（多节点）

```yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: danos-open
spec:
  selector:
    matchLabels: { app: danos-open }
  template:
    metadata:
      labels: { app: danos-open }
    spec:
      hostNetwork: true
      containers:
      - name: danos
        image: danos-open:full
        securityContext:
          privileged: true
        volumeMounts:
        - name: hugepages
          mountPath: /dev/hugepages
        - name: config
          mountPath: /etc/danos
      volumes:
      - name: hugepages
        emptyDir: { medium: HugePages }
      - name: config
        configMap: { name: danos-config }
```

## 24.4 多节点集群

### 24.4.1 控制面分布式

```
┌─────────────┐     BGP/OSPF     ┌─────────────┐
│  Node A     │◄─────────────────►│  Node B     │
│  FRR + VPP  │                   │  FRR + VPP  │
│  danos-open │                   │  danos-open │
└──────┬──────┘                   └──────┬──────┘
       │                                 │
       └───────── BGP/OSPF ──────────────┘
                   │
             ┌─────┴─────┐
             │  Node C   │
             │  FRR+VPP  │
             └───────────┘
```

每节点运行：
- `frr`（BGP/OSPF/IS-IS/Zebra）
- `vpp`（数据面）
- `danos-open`（DPA 核心 + FIB Adapter + 管理面）

节点间：
- 控制面：FRR 协议（BGP/OSPF）建立邻居
- 数据面：VPP 转发（物理链路或 VXLAN/GRE 隧道）
- 管理面：gNMI/NETCONF 集中配置（Ansible/Terraform）

### 24.4.2 BGP Route Reflector 拓扑

大规模部署采用 RR 架构减少全连接：

```
        ┌─────────┐
        │  RR-1   │  ← FRR BGP Route Reflector
        └──┬──┬───┘
           │  │
    ┌──────┘  └──────┐
    │                 │
┌───┴───┐         ┌───┴───┐
│ Node A│         │ Node B│  ← DANOS-Open 边界节点
└───────┘         └───────┘
```

## 24.5 K8s CNI 集成

### 24.5.1 Multus 多网卡

DANOS-Open 作为 K8s CNI 提供数据面转发：

```yaml
apiVersion: k8s.cni.cncf.io/v1
kind: NetworkAttachmentDefinition
metadata:
  name: danos-data
spec:
  config: |
    {
      "cniVersion": "0.3.1",
      "name": "danos-data",
      "type": "danos-cni",
      "vrf": "tenant-a",
      "vlan": 100
    }
```

Pod 使用多网卡：
```yaml
metadata:
  annotations:
    k8s.v1.cni.cncf.io/networks: danos-data,danos-mgmt
```

### 24.5.2 danos-cni 工作流

```
1. kubelet 调用 danos-cni ADD
2. danos-cni 通过 gNMI 调用 danos-open:
   - 创建 VPP sub-interface（VLAN tag）
   - 绑定到指定 VRF
   - 分配 IP 地址
3. 返回网卡信息给 Pod
4. Pod 启动，流量经 VPP 转发
```

### 24.5.3 与 Calico/Cilium 共存

| 流量类型 | CNI | 数据面 |
|---------|-----|--------|
| Pod-Pod（同节点） | Calico/Cilium | eBPF/kernel |
| Pod-External | DANOS-Open | VPP |
| Network Function | DANOS-Open | VPP + DPDK |

## 24.6 配置管理

### 24.6.1 Ansible Provider

```yaml
- name: Configure BGP on DANOS-Open
  hosts: danos_nodes
  tasks:
  - name: Set BGP neighbors
    danos_open_gnmi:
      path: /gnmi/bgp/neighbors
      method: SET
      body:
        neighbor: "{{ item.ip }}"
        remote_as: "{{ item.asn }}"
    loop: "{{ bgp_neighbors }}"
```

### 24.6.2 Terraform Provider

```hcl
provider "danos_open" {
  endpoint = "gnmi://node-a:57400"
  tls_cert = file("cert.pem")
}

resource "danos_open_vrf" "tenant_a" {
  vrf_id = 100
  name   = "tenant-a"
}

resource "danos_open_route" "default_route" {
  vrf_id      = danos_open_vrf.tenant_a.vrf_id
  prefix      = "0.0.0.0/0"
  protocol    = "static"
  nhgroup_id  = danos_open_nhgroup.egress.id
}
```

### 24.6.3 GitOps 工作流

```
1. 开发者提交配置到 Git 仓库
2. CI 验证 YANG 模型 + dry-run
3. ArgoCD/Flux 同步到 K8s ConfigMap
4. danos-open 监听 ConfigMap 变更，通过 gNMI Set 应用
5. Reconciler 验证 Programmed = Desired
6. 失败自动回滚到上一个 Git commit
```

## 24.7 部署验证

### 24.7.1 健康检查

```bash
# 进程健康
danos-cli show system health

# DPA 后端连通性
danos-cli show dpa backends

# Reconciler 状态
danos-cli show reconciler status

# 事务统计
danos-cli show transactions stats
```

### 24.7.2 部署测试矩阵

| 部署形态 | 测试内容 | 验收标准 |
|---------|---------|---------|
| 裸机 | 包安装 + 服务启动 | `systemctl is-active` = active |
| 容器 | docker run + gNMI 连通 | gNMI Get 返回 200 |
| K8s DaemonSet | 滚动更新 + 回滚 | 零丢包（BGP GR） |
| 多节点 | BGP 收敛 + ECMP | FIB 一致性 < 1s |
| CNI | Pod 创建 + 流量转发 | Pod 可 ping 外部 |

## 24.8 MVP 对齐

| 功能 | MVP 版本 |
|------|---------|
| Debian/Ubuntu 包 | v0.1（已实现） |
| OCI 容器镜像 | v0.1（已实现） |
| Docker Compose | v0.2 |
| K8s DaemonSet | v0.3 |
| 多节点 BGP 集群 | v0.3 |
| K8s CNI (Multus) | v0.4 |
| Ansible/Terraform Provider | v0.4 |
| GitOps | v0.5+ |
