#include "table_node.h"

#include "private.h"
#include "master_table_schema.h"
#include "mount_config_attributes.h"
#include "table_collocation.h"
#include "table_manager.h"

#include <yt/yt/server/master/chunk_server/chunk_list.h>

#include <yt/yt/server/master/tablet_server/helpers.h>
#include <yt/yt/server/master/tablet_server/hunk_storage_node.h>
#include <yt/yt/server/master/tablet_server/mount_config_storage.h>
#include <yt/yt/server/master/tablet_server/tablet.h>
#include <yt/yt/server/master/tablet_server/tablet_cell_bundle.h>

#include <yt/yt/server/master/object_server/object.h>
#include <yt/yt/server/master/object_server/public.h>

#include <yt/yt/server/master/cell_master/serialize.h>

#include <yt/yt/server/lib/misc/interned_attributes.h>

#include <yt/yt/server/lib/tablet_node/config.h>

#include <yt/yt/server/lib/tablet_balancer/config.h>

#include <yt/yt/client/table_client/schema.h>

#include <yt/yt/client/chaos_client/helpers.h>

#include <yt/yt/client/object_client/helpers.h>

namespace NYT::NTableServer {

using namespace NCellMaster;
using namespace NChunkClient::NProto;
using namespace NChunkClient;
using namespace NChunkServer;
using namespace NCypressServer;
using namespace NObjectServer;
using namespace NSecurityServer;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NTabletServer;
using namespace NChaosClient;
using namespace NObjectClient;
using namespace NTabletServer;
using namespace NTransactionClient;
using namespace NTransactionServer;
using namespace NYTree;
using namespace NYson;
using namespace NCrypto;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = TableServerLogger;

////////////////////////////////////////////////////////////////////////////////

void TDynamicTableLock::Persist(const NCellMaster::TPersistenceContext& context)
{
    using ::NYT::Persist;
    Persist(context, PendingTabletCount);
}

////////////////////////////////////////////////////////////////////////////////

TTableNode::TDynamicTableAttributes::TDynamicTableAttributes()
    : TabletBalancerConfig(New<NTabletBalancer::TMasterTableTabletBalancerConfig>())
    , MountConfigStorage(New<TMountConfigStorage>())
{ }

void TTableNode::TDynamicTableAttributes::Save(NCellMaster::TSaveContext& context) const
{
    using NYT::Save;
    Save(context, Atomicity);
    Save(context, CommitOrdering);
    Save(context, UpstreamReplicaId);
    Save(context, LastCommitTimestamp);
    Save(context, ForcedCompactionRevision);
    Save(context, ForcedStoreCompactionRevision);
    Save(context, ForcedHunkCompactionRevision);
    Save(context, ForcedChunkViewCompactionRevision);
    Save(context, Dynamic);
    Save(context, *TabletBalancerConfig);
    Save(context, DynamicTableLocks);
    Save(context, UnconfirmedDynamicTableLockCount);
    Save(context, EnableDynamicStoreRead);
    Save(context, MountedWithEnabledDynamicStoreRead);
    Save(context, ProfilingMode);
    Save(context, ProfilingTag);
    Save(context, EnableDetailedProfiling);
    Save(context, EnableConsistentChunkReplicaPlacement);
    Save(context, BackupState);
    Save(context, TabletCountByBackupState);
    Save(context, AggregatedTabletBackupState);
    Save(context, BackupCheckpointTimestamp);
    Save(context, BackupMode);
    Save(context, BackupError);
    Save(context, ReplicaBackupDescriptors);
    Save(context, QueueAgentStage);
    Save(context, TreatAsConsumer);
    Save(context, IsVitalConsumer);
    Save(context, *MountConfigStorage);
    Save(context, HunkStorageNode);
}

void TTableNode::TDynamicTableAttributes::Load(
    NCellMaster::TLoadContext& context,
    TTabletOwnerBase* tabletOwner)
{
    using NYT::Load;
    Load(context, Atomicity);
    Load(context, CommitOrdering);
    Load(context, UpstreamReplicaId);
    Load(context, LastCommitTimestamp);
    Load(context, ForcedCompactionRevision);
    Load(context, ForcedStoreCompactionRevision);
    Load(context, ForcedHunkCompactionRevision);
    // COMPAT(ifsmirnov)
    if (context.GetVersion() >= EMasterReign::InternedForcedChunkViewCompactionRevision) {
        Load(context, ForcedChunkViewCompactionRevision);
    }
    Load(context, Dynamic);
    Load(context, *TabletBalancerConfig);
    Load(context, DynamicTableLocks);
    Load(context, UnconfirmedDynamicTableLockCount);
    Load(context, EnableDynamicStoreRead);
    Load(context, MountedWithEnabledDynamicStoreRead);
    Load(context, ProfilingMode);
    Load(context, ProfilingTag);
    Load(context, EnableDetailedProfiling);
    Load(context, EnableConsistentChunkReplicaPlacement);
    Load(context, BackupState);
    Load(context, TabletCountByBackupState);
    Load(context, AggregatedTabletBackupState);
    Load(context, BackupCheckpointTimestamp);
    Load(context, BackupMode);
    Load(context, BackupError);
    Load(context, ReplicaBackupDescriptors);

    // COMPAT(achulkov2): Set QueueAgentStage to std::nullopt for old reigns.
    if (context.GetVersion() == EMasterReign::ZookeeperShards) {
        TString oldQueueAgentStage;
        Load(context, oldQueueAgentStage);
        QueueAgentStage = {};
    } else {
        Load(context, QueueAgentStage);
    }

    Load(context, TreatAsConsumer);
    Load(context, IsVitalConsumer);
    Load(context, *MountConfigStorage);

    // COMPAT(ifsmirnov)
    if (context.GetVersion() < EMasterReign::InternedForcedChunkViewCompactionRevision) {
        auto value = MountConfigStorage->Find("forced_chunk_view_compaction_revision");

        if (value) {
            auto node = ConvertToNode(value);
            switch (node->GetType()) {
                case ENodeType::Int64:
                    ForcedChunkViewCompactionRevision = node->AsInt64()->GetValue();
                    break;

                case ENodeType::Uint64:
                    ForcedChunkViewCompactionRevision = node->AsUint64()->GetValue();
                    break;

                default:
                    YT_LOG_ERROR("Found table with invalid type of \"forced_chunk_view_compaction_revision\" "
                        "attribute: expected integral, got %Qlv (TableId: %v, Value: %v)",
                        node->GetType(),
                        tabletOwner->GetId(),
                        ConvertToYsonString(node, EYsonFormat::Text));
            }

            MountConfigStorage->Remove("forced_chunk_view_compaction_revision");
        }
    }

    Load(context, HunkStorageNode);
}

#define FOR_EACH_COPYABLE_ATTRIBUTE(XX) \
    XX(Dynamic) \
    XX(Atomicity) \
    XX(CommitOrdering) \
    XX(UpstreamReplicaId) \
    XX(LastCommitTimestamp) \
    XX(EnableDynamicStoreRead) \
    XX(ProfilingMode) \
    XX(ProfilingTag) \
    XX(EnableDetailedProfiling) \
    XX(EnableConsistentChunkReplicaPlacement) \
    XX(QueueAgentStage) \

void TTableNode::TDynamicTableAttributes::CopyFrom(const TDynamicTableAttributes* other)
{
    #define XX(attr) attr = other->attr;
    FOR_EACH_COPYABLE_ATTRIBUTE(XX)
    #undef XX

    TabletBalancerConfig = CloneYsonStruct(other->TabletBalancerConfig);
    *MountConfigStorage = *other->MountConfigStorage;
}

void TTableNode::TDynamicTableAttributes::BeginCopy(TBeginCopyContext* context) const
{
    using NYT::Save;

    #define XX(attr) Save(*context, attr);
    FOR_EACH_COPYABLE_ATTRIBUTE(XX)
    #undef XX

    Save(*context, ConvertToYsonString(TabletBalancerConfig));
    Save(*context, ConvertToYsonString(MountConfigStorage));
}

void TTableNode::TDynamicTableAttributes::EndCopy(TEndCopyContext* context)
{
    using NYT::Load;

    #define XX(attr) Load(*context, attr);
    FOR_EACH_COPYABLE_ATTRIBUTE(XX)
    #undef XX

    TabletBalancerConfig = ConvertTo<NTabletBalancer::TMasterTableTabletBalancerConfigPtr>(Load<TYsonString>(*context));
    MountConfigStorage = ConvertTo<TMountConfigStoragePtr>(Load<TYsonString>(*context));
}

#undef FOR_EACH_COPYABLE_ATTRIBUTE

////////////////////////////////////////////////////////////////////////////////

TTableNode::TTableNode(TVersionedNodeId id)
    : TTabletOwnerBase(id)
{
    if (IsTrunk()) {
        SetOptimizeFor(EOptimizeFor::Lookup);
        SetHunkErasureCodec(NErasure::ECodec::None);
    }
}

TTableNode* TTableNode::GetTrunkNode()
{
    return TTabletOwnerBase::GetTrunkNode()->As<TTableNode>();
}

const TTableNode* TTableNode::GetTrunkNode() const
{
    return TTabletOwnerBase::GetTrunkNode()->As<TTableNode>();
}

void TTableNode::EndUpload(const TEndUploadContext& context)
{
    if (IsDynamic()) {
        if (SchemaMode_ != context.SchemaMode ||
            *GetSchema()->AsTableSchema() != *context.Schema->AsTableSchema())
        {
            YT_LOG_ALERT("Schema of a dynamic table changed during end upload (TableId: %v, TransactionId: %v, "
                "OriginalSchemaMode: %v, NewSchemaMode: %v, OriginalSchema: %v, NewSchema: %v)",
                GetId(),
                GetTransaction()->GetId(),
                SchemaMode_,
                context.SchemaMode,
                GetSchema()->AsTableSchema(),
                context.Schema->AsTableSchema());
        }
    }

    SchemaMode_ = context.SchemaMode;
    YT_VERIFY(context.Schema);

    auto* schema = GetSchema();
    if (IsExternal() && schema != context.Schema) {
        auto externalCellTag = GetExternalCellTag();

        context.Schema->ExportRef(externalCellTag);
        schema->UnexportRef(externalCellTag);
    }

    const auto& tableManager = context.Bootstrap->GetTableManager();
    tableManager->SetTableSchema(this, context.Schema);

    if (context.OptimizeFor) {
        OptimizeFor_.Set(*context.OptimizeFor);
    }
    if (context.ChunkFormat) {
        ChunkFormat_.Set(*context.ChunkFormat);
    }

    TTabletOwnerBase::EndUpload(context);
}

TDetailedMasterMemory TTableNode::GetDetailedMasterMemoryUsage() const
{
    auto result = TTabletOwnerBase::GetDetailedMasterMemoryUsage();
    if (const auto* storage = FindMountConfigStorage()) {
        result[EMasterMemoryType::Attributes] += storage->GetMasterMemoryUsage();
    }
    return result;
}

bool TTableNode::IsSorted() const
{
    return GetSchema()->AsTableSchema()->IsSorted();
}

bool TTableNode::IsUniqueKeys() const
{
    return GetSchema()->AsTableSchema()->IsUniqueKeys();
}

TAccount* TTableNode::GetAccount() const
{
    return TCypressNode::Account().Get();
}

bool TTableNode::IsReplicated() const
{
    return GetType() == EObjectType::ReplicatedTable;
}

bool TTableNode::IsPhysicallyLog() const
{
    return IsLogTableType(GetType());
}

bool TTableNode::IsPhysicallySorted() const
{
    return IsSorted() && !IsPhysicallyLog();
}

TReplicationCardId TTableNode::GetReplicationCardId() const
{
    return ReplicationCardIdFromUpstreamReplicaIdOrNull(GetUpstreamReplicaId());
}

void TTableNode::Save(NCellMaster::TSaveContext& context) const
{
    TTabletOwnerBase::Save(context);

    using NYT::Save;
    SaveTableSchema(context);
    Save(context, SchemaMode_);
    Save(context, OptimizeFor_);
    Save(context, ChunkFormat_);
    Save(context, HunkErasureCodec_);
    Save(context, RetainedTimestamp_);
    Save(context, UnflushedTimestamp_);
    Save(context, ReplicationCollocation_);
    TUniquePtrSerializer<>::Save(context, DynamicTableAttributes_);
}

void TTableNode::Load(NCellMaster::TLoadContext& context)
{
    TTabletOwnerBase::Load(context);

    using NYT::Load;
    LoadTableSchema(context);
    Load(context, SchemaMode_);
    Load(context, OptimizeFor_);
    // COMPAT(babenko)
    if (context.GetVersion() >= EMasterReign::ChunkFormat) {
        Load(context, ChunkFormat_);
    }
    Load(context, HunkErasureCodec_);
    Load(context, RetainedTimestamp_);
    Load(context, UnflushedTimestamp_);
    Load(context, ReplicationCollocation_);

    // COMPAT(gritukan): Use TUniquePtrSerializer.
    if (Load<bool>(context)) {
        DynamicTableAttributes_ = std::make_unique<TDynamicTableAttributes>();
        DynamicTableAttributes_->Load(context, this);
    } else {
        DynamicTableAttributes_.reset();
    }
}

void TTableNode::LoadTableSchema(NCellMaster::TLoadContext& context)
{
    using NYT::Load;

    Load(context, Schema_);
}

void TTableNode::SaveTableSchema(NCellMaster::TSaveContext& context) const
{
    using NYT::Save;

    Save(context, Schema_);
}

bool TTableNode::IsDynamic() const
{
    return GetTrunkNode()->GetDynamic();
}

bool TTableNode::IsQueue() const
{
    return IsDynamic() && !IsSorted();
}

bool TTableNode::IsTrackedQueueObject() const
{
    return IsNative() && IsTrunk() && IsQueue();
}

bool TTableNode::IsConsumer() const
{
    return GetTreatAsConsumer();
}

bool TTableNode::IsTrackedConsumerObject() const
{
    return IsNative() && IsTrunk() && IsConsumer();
}

bool TTableNode::IsEmpty() const
{
    return ComputeTotalStatistics().chunk_count() == 0;
}

bool TTableNode::IsLogicallyEmpty() const
{
    const auto* chunkList = GetChunkList();
    YT_VERIFY(chunkList);
    return chunkList->Statistics().LogicalRowCount == 0;
}

TTimestamp TTableNode::GetCurrentUnflushedTimestamp(
    TTimestamp latestTimestamp) const
{
    // COMPAT(savrus) Consider saved value only for non-trunk nodes.
    return !IsTrunk() && UnflushedTimestamp_ != NullTimestamp
        ? UnflushedTimestamp_
        : CalculateUnflushedTimestamp(latestTimestamp);
}

TTimestamp TTableNode::GetCurrentRetainedTimestamp() const
{
    // COMPAT(savrus) Consider saved value only for non-trunk nodes.
    return !IsTrunk() && RetainedTimestamp_ != NullTimestamp
        ? RetainedTimestamp_
        : CalculateRetainedTimestamp();
}

TTimestamp TTableNode::CalculateUnflushedTimestamp(
    TTimestamp latestTimestamp) const
{
    auto* trunkNode = GetTrunkNode();
    if (!trunkNode->IsDynamic()) {
        return NullTimestamp;
    }

    auto result = MaxTimestamp;
    for (const auto* tablet : trunkNode->Tablets()) {
        auto timestamp = tablet->GetState() != ETabletState::Unmounted
            ? static_cast<TTimestamp>(tablet->As<TTablet>()->NodeStatistics().unflushed_timestamp())
            : latestTimestamp;
        result = std::min(result, timestamp);
    }
    return result;
}

TTimestamp TTableNode::CalculateRetainedTimestamp() const
{
    auto* trunkNode = GetTrunkNode();
    if (!trunkNode->IsDynamic()) {
        return NullTimestamp;
    }

    auto result = MinTimestamp;
    for (const auto* tablet : trunkNode->Tablets()) {
        auto timestamp = tablet->As<TTablet>()->GetRetainedTimestamp();
        result = std::max(result, timestamp);
    }
    return result;
}

TMasterTableSchema* TTableNode::GetSchema() const
{
    return Schema_;
}

void TTableNode::SetSchema(TMasterTableSchema* schema)
{
    Schema_ = schema;
}

void TTableNode::ValidateNotBackup(TStringBuf message) const
{
    if (GetBackupState() == ETableBackupState::BackupCompleted) {
        THROW_ERROR_EXCEPTION(NTabletClient::EErrorCode::InvalidBackupState, "%v", message);
    }
}

std::optional<bool> TTableNode::GetEnableTabletBalancer() const
{
    return TabletBalancerConfig()->EnableAutoReshard
        ? std::nullopt
        : std::make_optional(false);
}

void TTableNode::SetEnableTabletBalancer(std::optional<bool> value)
{
    MutableTabletBalancerConfig()->EnableAutoReshard = value.value_or(true);
}

std::optional<i64> TTableNode::GetMinTabletSize() const
{
    return TabletBalancerConfig()->MinTabletSize;
}

void TTableNode::SetMinTabletSize(std::optional<i64> value)
{
    MutableTabletBalancerConfig()->SetMinTabletSize(value);
}

std::optional<i64> TTableNode::GetMaxTabletSize() const
{
    return TabletBalancerConfig()->MaxTabletSize;
}

void TTableNode::SetMaxTabletSize(std::optional<i64> value)
{
    MutableTabletBalancerConfig()->SetMaxTabletSize(value);
}

std::optional<i64> TTableNode::GetDesiredTabletSize() const
{
    return TabletBalancerConfig()->DesiredTabletSize;
}

void TTableNode::SetDesiredTabletSize(std::optional<i64> value)
{
    MutableTabletBalancerConfig()->SetDesiredTabletSize(value);
}

std::optional<int> TTableNode::GetDesiredTabletCount() const
{
    return TabletBalancerConfig()->DesiredTabletCount;
}

void TTableNode::SetDesiredTabletCount(std::optional<int> value)
{
    MutableTabletBalancerConfig()->DesiredTabletCount = value;
}

void TTableNode::AddDynamicTableLock(
    TTransactionId transactionId,
    TTimestamp timestamp,
    int pendingTabletCount)
{
    YT_VERIFY(MutableDynamicTableLocks().emplace(
        transactionId, TDynamicTableLock{timestamp, pendingTabletCount}).second);
    SetUnconfirmedDynamicTableLockCount(GetUnconfirmedDynamicTableLockCount() + 1);
}

void TTableNode::ConfirmDynamicTableLock(TTransactionId transactionId)
{
    if (auto it = MutableDynamicTableLocks().find(transactionId)) {
        YT_VERIFY(it->second.PendingTabletCount > 0);
        --it->second.PendingTabletCount;
        if (it->second.PendingTabletCount == 0) {
            SetUnconfirmedDynamicTableLockCount(GetUnconfirmedDynamicTableLockCount() - 1);
        }
    }
}

void TTableNode::RemoveDynamicTableLock(TTransactionId transactionId)
{
    if (auto it = MutableDynamicTableLocks().find(transactionId)) {
        if (it->second.PendingTabletCount > 0) {
            SetUnconfirmedDynamicTableLockCount(GetUnconfirmedDynamicTableLockCount() - 1);
        }
        MutableDynamicTableLocks().erase(it);
    }
}

void TTableNode::ValidateMount() const
{
    TTabletOwnerBase::ValidateMount();

    if (!IsDynamic()) {
        THROW_ERROR_EXCEPTION("Cannot mount a static table");
    }

    ValidateNotBackup("Cannot mount backup table");
}

void TTableNode::ValidateUnmount() const
{
    TTabletOwnerBase::ValidateUnmount();

    if (!IsDynamic()) {
        THROW_ERROR_EXCEPTION("Cannot unmount a static table");
    }
}

void TTableNode::ValidateRemount() const
{
    TTabletOwnerBase::ValidateRemount();

    if (!IsDynamic()) {
        THROW_ERROR_EXCEPTION("Cannot remount a static table");
    }
}

void TTableNode::ValidateFreeze() const
{
    TTabletOwnerBase::ValidateFreeze();

    if (!IsDynamic()) {
        THROW_ERROR_EXCEPTION("Cannot freeze a static table");
    }
}

void TTableNode::ValidateUnfreeze() const
{
    TTabletOwnerBase::ValidateUnfreeze();

    if (!IsDynamic()) {
        THROW_ERROR_EXCEPTION("Cannot unfreeze a static table");
    }
}

void TTableNode::ValidateReshard(
    const TBootstrap* bootstrap,
    int firstTabletIndex,
    int lastTabletIndex,
    int newTabletCount,
    const std::vector<TLegacyOwningKey>& pivotKeys) const
{
    TTabletOwnerBase::ValidateReshard(
        bootstrap,
        firstTabletIndex,
        lastTabletIndex,
        newTabletCount,
        pivotKeys);

    // First, check parameters with little knowledge of the table.
    // Primary master must ensure that the table could be created.

    if (!IsDynamic()) {
        THROW_ERROR_EXCEPTION("Cannot reshard a static table");
    }

    if (newTabletCount <= 0) {
        THROW_ERROR_EXCEPTION("Tablet count must be positive");
    }

    if (newTabletCount > MaxTabletCount) {
        THROW_ERROR_EXCEPTION("Tablet count cannot exceed the limit of %v",
            MaxTabletCount);
    }

    if (DynamicTableLocks().size() > 0) {
        THROW_ERROR_EXCEPTION("Dynamic table is locked by some bulk insert");
    }

    ValidateNotBackup("Cannot reshard backup table");

    if (IsSorted()) {
        // NB: We allow reshard without pivot keys.
        // Pivot keys will be calculated when ReshardTable is called so we don't need to check them.
        if (!pivotKeys.empty()) {
            if (std::ssize(pivotKeys) != newTabletCount) {
                THROW_ERROR_EXCEPTION("Wrong pivot key count: %v instead of %v",
                    pivotKeys.size(),
                    newTabletCount);
            }

            // Validate first pivot key (on primary master before the table is created).
            if (firstTabletIndex == 0 && pivotKeys[0] != EmptyKey()) {
                THROW_ERROR_EXCEPTION("First pivot key must be empty");
            }

            for (int index = 0; index < std::ssize(pivotKeys) - 1; ++index) {
                if (pivotKeys[index] >= pivotKeys[index + 1]) {
                    THROW_ERROR_EXCEPTION("Pivot keys must be strictly increasing");
                }
            }

            // Validate pivot keys against table schema.
            for (const auto& pivotKey : pivotKeys) {
                ValidatePivotKey(pivotKey, *GetSchema()->AsTableSchema());
            }
        }

        if (!IsPhysicallySorted() && pivotKeys.empty()) {
            THROW_ERROR_EXCEPTION("Pivot keys must be provided to reshard a replicated table");
        }
    } else {
        if (!pivotKeys.empty()) {
            THROW_ERROR_EXCEPTION("Table is ordered; must provide tablet count");
        }
    }

    if (IsExternal()) {
        return;
    }

    if (IsPhysicallyLog() && !IsLogicallyEmpty()) {
        THROW_ERROR_EXCEPTION("Cannot reshard non-empty table of type %Qlv",
            GetType());
    }

    ParseTabletRangeOrThrow(this, &firstTabletIndex, &lastTabletIndex); // may throw

    if (IsSorted()) {
        // NB: We allow reshard without pivot keys.
        // Pivot keys will be calculated when ReshardTable is called so we don't need to check them.
        if (!pivotKeys.empty()) {
            const auto& tablets = Tablets();
            if (pivotKeys[0] != tablets[firstTabletIndex]->As<TTablet>()->GetPivotKey()) {
                THROW_ERROR_EXCEPTION(
                    "First pivot key must match that of the first tablet "
                    "in the resharded range");
            }

            if (lastTabletIndex != std::ssize(tablets) - 1) {
                YT_VERIFY(lastTabletIndex + 1 >= 0 && lastTabletIndex + 1 < std::ssize(tablets));
                if (pivotKeys.back() >= tablets[lastTabletIndex + 1]->As<TTablet>()->GetPivotKey()) {
                    THROW_ERROR_EXCEPTION(
                        "Last pivot key must be strictly less than that of the tablet "
                        "which follows the resharded range");
                }
            }
        }
    }
}

void TTableNode::CheckInvariants(NCellMaster::TBootstrap* bootstrap) const
{
    TChunkOwnerBase::CheckInvariants(bootstrap);

    // TODO(gritukan): extend this to non-trunk nodes.
    if (auto* chunkList = GetChunkList(); IsObjectAlive(chunkList) && IsObjectAlive(this) && IsTrunk()) {
        if (GetDynamic()) {
            if (IsPhysicallySorted()) {
                YT_VERIFY(chunkList->GetKind() == EChunkListKind::SortedDynamicRoot);
            } else {
                // Ordered dynamic table.
                YT_VERIFY(chunkList->GetKind() == EChunkListKind::OrderedDynamicRoot);
            }
        } else {
            // Static table.
            YT_VERIFY(chunkList->GetKind() == EChunkListKind::Static);
        }
    }

    if (DynamicTableAttributes_ && DynamicTableAttributes_->HunkStorageNode) {
        auto id = GetVersionedId();
        YT_VERIFY(DynamicTableAttributes_->HunkStorageNode->AssociatedNodeIds().contains(id));
    }

    if (IsObjectAlive(this)) {
        // NB: Const-cast due to const-correctness rabbit-hole, which led to TTableNode* being stored in the set.
        YT_VERIFY(bootstrap->GetTableManager()->GetQueues().contains(const_cast<TTableNode*>(this)) == IsTrackedQueueObject());
        YT_VERIFY(bootstrap->GetTableManager()->GetConsumers().contains(const_cast<TTableNode*>(this)) == IsTrackedConsumerObject());
    }
}

const TMountConfigStorage* TTableNode::FindMountConfigStorage() const
{
    return DynamicTableAttributes_
        ? DynamicTableAttributes_->MountConfigStorage.Get()
        : nullptr;
}

TMountConfigStorage* TTableNode::GetMutableMountConfigStorage()
{
    INITIALIZE_EXTRA_PROPERTY_HOLDER(DynamicTableAttributes);
    return DynamicTableAttributes_->MountConfigStorage.Get();
}

void TTableNode::ResetHunkStorageNode()
{
    if (!DynamicTableAttributes_ || !DynamicTableAttributes_->HunkStorageNode) {
        return;
    }

    auto id = GetVersionedId();
    EraseOrCrash(DynamicTableAttributes_->HunkStorageNode->AssociatedNodeIds(), id);

    DynamicTableAttributes_->HunkStorageNode.Reset();
}

void TTableNode::SetHunkStorageNode(THunkStorageNode* node)
{
    ResetHunkStorageNode();

    if (!node) {
        return;
    }

    INITIALIZE_EXTRA_PROPERTY_HOLDER(DynamicTableAttributes);

    THunkStorageNodePtr hunkStorageNodePtr(node);
    DynamicTableAttributes_->HunkStorageNode = std::move(hunkStorageNodePtr);
    auto id = GetVersionedId();
    InsertOrCrash(DynamicTableAttributes_->HunkStorageNode->AssociatedNodeIds(), id);
}

THunkStorageNode* TTableNode::GetHunkStorageNode() const
{
    return DynamicTableAttributes_
        ? DynamicTableAttributes_->HunkStorageNode.Get()
        : nullptr;
}

DEFINE_EXTRA_PROPERTY_HOLDER(TTableNode, TTableNode::TDynamicTableAttributes, DynamicTableAttributes);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableServer
