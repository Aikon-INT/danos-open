# 附录 E. DPA vs SAI 对比示例（v1.1 P2-19）

> v1.1 评审 §5.1 P2 改进动作（v0.4 前完成）。
> DPA（Dataplane Abstraction）与 SAI（Switch Abstraction Interface）的
> 具体 API 对比，含代码示例与语义差异分析。

## E.1 设计哲学对比

| 维度 | SAI | DPA |
|------|-----|-----|
| **目标** | ASIC 抽象（交换芯片） | 通用数据面抽象（VPP/OVS/P4/ASIC） |
| **假设** | 硬件转发 | 软件 + 硬件转发 |
| **API 风格** | C 函数指针 + attribute 列表 | C ABI + 事务 + 对象 |
| **事务** | 无（逐 API 调用） | 原生事务（begin/commit/rollback） |
| **Capability** | 厂商扩展，无统一标准 | 一等公民，标准化查询 |
| **状态模型** | 隐式（调用即生效） | 显式（CONFIG→DESIRED→PROGRAMMED→OPER） |
| **Reconcile** | 无（上层负责） | 内置 Reconciler |
| **多 Backend** | 单一 ASIC SAI 实现 | 多 Backend（VPP/OVS/P4/ASIC） |

## E.2 场景对比：创建路由

### E.2.1 SAI 实现

```c
/* SAI: 创建路由需要手动管理 NH group 生命周期 */

/* 1. 创建 next hop */
sai_object_id_t nh_id;
sai_next_hop_attr_t nh_attrs[] = {
    { SAI_NEXT_HOP_ATTR_TYPE, SAI_NEXT_HOP_TYPE_IP },
    { SAI_NEXT_HOP_ATTR_IP, "10.0.0.1" },
    { SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID, rif_id },
};
sai_create_next_hop(&nh_id, switch_id, 3, nh_attrs);

/* 2. 创建 next hop group */
sai_object_id_t nhg_id;
sai_object_id_t nh_list[] = { nh_id };
sai_next_hop_group_attr_t nhg_attrs[] = {
    { SAI_NEXT_HOP_GROUP_ATTR_TYPE, SAI_NEXT_HOP_GROUP_TYPE_ECMP },
    { SAI_NEXT_HOP_GROUP_ATTR_NEXT_HOP_LIST, { .count = 1, .list = nh_list } },
};
sai_create_next_hop_group(&nhg_id, switch_id, 2, nhg_attrs);

/* 3. 创建 route entry */
sai_route_entry_t route_entry = {
    .switch_id = switch_id,
    .vr_id = vrf_id,
    .destination = "192.168.1.0/24",
};
sai_route_attr_t route_attrs[] = {
    { SAI_ROUTE_ATTR_NEXT_HOP_ID, nhg_id },
};
sai_create_route_entry(&route_entry, 1, route_attrs);

/* 无事务：每步立即生效，失败需手动回滚 */
/* 无 Capability 检查：假设 ASIC 支持 */
```

### E.2.2 DPA 实现

```c
/* DPA: 事务化、对象化、Capability 感知 */

/* 0. Capability 检查 */
danos_capability_t cap;
danos_capability_query("vpp", DANOS_OBJ_ROUTE, &cap);
if (!cap.supported) return DANOS_ERR_NOT_SUPPORTED;

/* 1. 开启事务 */
danos_tx_t tx;
danos_tx_begin(&tx, "add-route", NULL);

/* 2. 创建 next hop（事务内，不立即生效） */
danos_nexthop_t nh = {
    .gateway = { .addr = "10.0.0.1" },
    .ifindex = 1,
};
danos_nh_create(&tx, &nh);

/* 3. 创建 next hop group */
danos_nhgroup_t nhg = {
    .nh_count = 1,
    .nh_ids = { nh.id },
};
danos_nhgroup_create(&tx, &nhg);

/* 4. 创建 route */
danos_route_t route = {
    .vrf_id = 0,
    .prefix = { .addr = "192.168.1.0", .prefix_len = 24 },
    .protocol = DANOS_ROUTE_PROTO_STATIC,
    .nhgroup_id = nhg.id,
};
danos_route_create(&tx, &route);

/* 5. Prepare → Validate → Commit（原子） */
danos_status_t st = danos_tx_prepare(&tx);
if (st == DANOS_OK) st = danos_tx_validate(&tx);
if (st == DANOS_OK) st = danos_tx_commit(&tx);

if (st != DANOS_OK) {
    danos_tx_rollback(&tx);  /* 自动回滚，无半生效 */
    return st;
}

/* 6. Verify（异步，Reconciler 检查 Programmed = Oper） */
danos_tx_verify(&tx);
```

### E.2.3 差异分析

| 方面 | SAI | DPA |
|------|-----|-----|
| **步骤数** | 3 步（NH + NHG + Route） | 5 步（含事务管理） |
| **原子性** | 无，中间状态可见 | 有，commit 前不生效 |
| **回滚** | 手动（需反向删除） | 自动（rollback） |
| **Capability** | 无检查 | 显式查询 |
| **错误处理** | 每步检查返回值 | 事务级错误 |
| **并发** | 需外部锁 | 事务引擎内置（多读单写） |

## E.3 场景对比：Capability 查询

### E.3.1 SAI

```c
/* SAI: Capability 查询语义不统一 */
sai_status_t st;
sai_attr_capability_t cap;

/* 查询 route 是否支持某属性 */
st = sai_query_attribute_capability(
    switch_id,
    SAI_OBJECT_TYPE_ROUTE_ENTRY,
    SAI_ROUTE_ATTR_NEXT_HOP_ID,
    &cap);

/* cap 的语义因厂商而异：
 * - 有的厂商返回 implementable=true 但实际不支持
 * - 有的厂商不支持 query，返回 SAI_STATUS_NOT_IMPLEMENTED
 * - 无统一的最大数量、性能等查询
 */
```

### E.3.2 DPA

```c
/* DPA: Capability 一等公民，标准化 */
danos_capability_t cap;
danos_status_t st = danos_capability_query(
    "vpp",                    /* backend name */
    DANOS_OBJ_ROUTE,         /* object type */
    &cap);

/* cap 结构标准化： */
/*   cap.supported          → 是否支持 */
/*   cap.max_objects        → 最大对象数 */
/*   cap.max_per_transaction→ 单事务最大操作数 */
/*   cap.features_mask      → 特性位掩码（ECMP/backup/...） */
/*   cap.backend_version    → 后端版本 */

if (cap.supported && cap.max_objects > 1000) {
    /* 安全使用 */
}
```

## E.4 场景对比：ACL 下发

### E.4.1 SAI

```c
/* SAI: ACL 通过 table + entry，attribute 列表冗长 */
sai_object_id_t acl_table_id;
sai_acl_table_attr_t table_attrs[] = {
    { SAI_ACL_TABLE_ATTR_ACL_BIND_POINT_TYPE_LIST, { .count = 1, .list = ... } },
    { SAI_ACL_TABLE_ATTR_ACL_STAGE, SAI_ACL_STAGE_INGRESS },
    { SAI_ACL_TABLE_ATTR_ACL_RULE_LIST, { .count = 0, .list = NULL } },
    /* ... 10+ 属性 ... */
};
sai_create_acl_table(&acl_table_id, switch_id, 3, table_attrs);

sai_object_id_t acl_entry_id;
sai_acl_entry_attr_t entry_attrs[] = {
    { SAI_ACL_ENTRY_ATTR_TABLE_ID, acl_table_id },
    { SAI_ACL_ENTRY_ATTR_PRIORITY, 100 },
    { SAI_ACL_ENTRY_ATTR_ACTION_COUNTER, ... },
    /* match 字段：每个字段一个属性 */
    { SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP, { .ip4 = "10.0.0.0", .mask = "255.0.0.0" } },
    { SAI_ACL_ENTRY_ATTR_ACTION_SET_PACKET_ACTION, SAI_PACKET_ACTION_DROP },
    /* ... 20+ 属性 ... */
};
sai_create_acl_entry(&acl_entry_id, switch_id, 6, entry_attrs);
```

### E.4.2 DPA

```c
/* DPA: 结构化对象，字段清晰 */
danos_tx_t tx;
danos_tx_begin(&tx, "add-acl", NULL);

danos_acl_table_t tbl = {
    .table_id = 1,
    .name = "ingress-filter",
    .bind_ifindex = 2,
    .ingress = true,
};
danos_acl_table_create(&tx, &tbl);

danos_acl_rule_t rule = {
    .rule_id = 100,
    .priority = 100,
    .match = {
        .fields_mask = DANOS_ACL_FIELD_SRC_IP,
        .src_ip = { .addr = "10.0.0.0", .prefix_len = 8 },
    },
    .act = {
        .action = DANOS_ACL_ACTION_DENY,
    },
};
danos_acl_rule_add(&tx, tbl.table_id, &rule);

danos_tx_prepare(&tx);
danos_tx_commit(&tx);  /* 原子下发 */
```

## E.5 场景对比：Backend 无关性

### E.5.1 SAI

SAI 假设单一 ASIC + SAI 实现。切换 Backend 需重新链接 SAI 库，且不同厂商 SAI 实现语义不一。

### E.5.2 DPA

```c
/* DPA: 同一 API，多 Backend */
/* VPP backend */
danos_backend_register(&(danos_backend_info_t){
    .name = "vpp",
    .version = { 24, 2, 0, 0 },
});

/* OVS backend (v0.3+) */
danos_backend_register(&(danos_backend_info_t){
    .name = "ovs",
    .version = { 3, 0, 0, 0 },
});

/* P4 backend (v0.4+) */
danos_backend_register(&(danos_backend_info_t){
    .name = "p4",
    .version = { 1, 0, 0, 0 },
});

/* 查询每个 backend 的 Capability */
danos_capability_t cap_vpp, cap_p4;
danos_capability_query("vpp", DANOS_OBJ_ROUTE, &cap_vpp);
danos_capability_query("p4", DANOS_OBJ_ROUTE, &cap_p4);

/* 同一事务可下发到不同 backend */
danos_tx_t tx;
danos_tx_begin(&tx, "multi-backend", "vpp");  /* 指定 backend */
danos_route_create(&tx, &route);
danos_tx_commit(&tx);
```

## E.6 场景对比：Reconciliation

### E.6.1 SAI

SAI 无内置 Reconciler。上层（如 SONiC orchagent）负责检测 Programmed ≠ Desired 并重编程，实现各异。

### E.6.2 DPA

```c
/* DPA: 内置 Reconciler，事件驱动 + 周期对账 */
/* 自动运行，无需上层干预 */

/* 事件触发：Backend down → up 时自动重编程 */
/* 周期对账：每 30s 全量检查 Programmed = Desired */
/* 防震荡：5s 窗口内最多 3 次，超过则冻结 + 告警 */

/* 用户可查询 Reconciler 状态 */
/* danos-cli show reconciler status */
```

## E.7 总结

| 场景 | SAI 代码行数 | DPA 代码行数 | DPA 优势 |
|------|------------|------------|---------|
| 创建路由 | ~20 行 | ~25 行 | 事务化、自动回滚、Capability |
| Capability 查询 | ~5 行（语义不一） | ~5 行（标准化） | 语义统一 |
| ACL 下发 | ~30 行 | ~20 行 | 结构化、事务化 |
| 多 Backend | 不支持 | ~10 行 | Backend 无关 |
| Reconciliation | 上层实现 | 0 行（内置） | 内置、标准化 |

**核心结论**：DPA 在保持 API 简洁性的同时，提供了 SAI 缺失的事务、
Capability、Reconciler、多 Backend 能力，更适合软件数据面 + 多后端场景。
SAI 在纯 ASIC 场景仍有优势（更贴近硬件、性能开销更低）。
