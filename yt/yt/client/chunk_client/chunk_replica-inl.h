#ifndef CHUNK_REPLICA_INL_H_
#error "Direct inclusion of this file is not allowed, include chunk_replica.h"
// For the sake of sane code completion.
#include "chunk_replica.h"
#endif

#include <yt/yt/client/object_client/helpers.h>

namespace NYT::NChunkClient {

////////////////////////////////////////////////////////////////////////////////

static_assert(
    ChunkReplicaIndexBound <= (1LL << 5),
    "Replica index must fit into 5 bits.");
static_assert(
    MediumIndexBound <= (1LL << 7),
    "Medium index must fit into 7 bits.");

Y_FORCE_INLINE TChunkReplicaWithMedium::TChunkReplicaWithMedium()
    : TChunkReplicaWithMedium(NNodeTrackerClient::InvalidNodeId)
{ }

Y_FORCE_INLINE TChunkReplicaWithMedium::TChunkReplicaWithMedium(ui64 value)
    : Value(value)
{ }

Y_FORCE_INLINE TChunkReplicaWithMedium::TChunkReplicaWithMedium(
    NNodeTrackerClient::TNodeId nodeId,
    int replicaIndex,
    int mediumIndex)
    : Value(
        static_cast<ui64>(nodeId) |
        (static_cast<ui64>(replicaIndex) << 24) |
        (static_cast<ui64>(mediumIndex) << 29))
{
    YT_ASSERT(nodeId >= 0 && nodeId <= static_cast<int>(NNodeTrackerClient::MaxNodeId));
    YT_ASSERT(replicaIndex >= 0 && replicaIndex < ChunkReplicaIndexBound);
    YT_ASSERT(mediumIndex >= 0 && mediumIndex < MediumIndexBound);
}

Y_FORCE_INLINE TChunkReplicaWithMedium::TChunkReplicaWithMedium(
    TChunkReplica replica)
    : TChunkReplicaWithMedium(
        replica.GetNodeId(),
        replica.GetReplicaIndex(),
        GenericMediumIndex)
{ }

Y_FORCE_INLINE NNodeTrackerClient::TNodeId TChunkReplicaWithMedium::GetNodeId() const
{
    return Value & 0x00ffffff;
}

Y_FORCE_INLINE int TChunkReplicaWithMedium::GetReplicaIndex() const
{
    return (Value & 0x1f000000) >> 24;
}

Y_FORCE_INLINE int TChunkReplicaWithMedium::GetMediumIndex() const
{
    return Value >> 29;
}

Y_FORCE_INLINE TChunkReplica TChunkReplicaWithMedium::ToChunkReplica() const
{
    return TChunkReplica(GetNodeId(), GetReplicaIndex());
}

Y_FORCE_INLINE void ToProto(ui64* value, TChunkReplicaWithMedium replica)
{
    *value = replica.Value;
}

Y_FORCE_INLINE void FromProto(TChunkReplicaWithMedium* replica, ui64 value)
{
    replica->Value = value;
}

////////////////////////////////////////////////////////////////////////////////

Y_FORCE_INLINE TChunkReplicaWithLocation::TChunkReplicaWithLocation()
    : TChunkReplicaWithMedium()
    , ChunkLocationUuid_(InvalidChunkLocationUuid)
{ }

Y_FORCE_INLINE TChunkReplicaWithLocation::TChunkReplicaWithLocation(
    TChunkReplicaWithMedium replica,
    TChunkLocationUuid locationUuid)
    : TChunkReplicaWithMedium(replica)
    , ChunkLocationUuid_(locationUuid)
{ }

Y_FORCE_INLINE TChunkReplicaWithLocation::TChunkReplicaWithLocation(
    NNodeTrackerClient::TNodeId nodeId,
    int replicaIndex,
    int mediumIndex,
    TChunkLocationUuid locationUuid)
    : TChunkReplicaWithMedium(nodeId, replicaIndex, mediumIndex)
    , ChunkLocationUuid_(locationUuid)
{ }

Y_FORCE_INLINE TChunkLocationUuid TChunkReplicaWithLocation::GetChunkLocationUuid() const
{
    return ChunkLocationUuid_;
}

////////////////////////////////////////////////////////////////////////////////

Y_FORCE_INLINE TChunkReplica::TChunkReplica()
    : TChunkReplica(NNodeTrackerClient::InvalidNodeId)
{ }

Y_FORCE_INLINE TChunkReplica::TChunkReplica(ui32 value)
    : Value(value)
{ }

Y_FORCE_INLINE TChunkReplica::TChunkReplica(int nodeId, int replicaIndex)
    : Value(static_cast<ui64>(nodeId) | (static_cast<ui64>(replicaIndex) << 24))
{
    YT_ASSERT(nodeId >= 0 && nodeId <= static_cast<int>(NNodeTrackerClient::MaxNodeId));
    YT_ASSERT(replicaIndex >= 0 && replicaIndex < ChunkReplicaIndexBound);
}

Y_FORCE_INLINE TChunkReplica::TChunkReplica(const TChunkReplicaWithMedium& replica)
    : Value(static_cast<ui64>(replica.GetNodeId()) | (static_cast<ui64>(replica.GetReplicaIndex()) << 24))
{ }

Y_FORCE_INLINE NNodeTrackerClient::TNodeId TChunkReplica::GetNodeId() const
{
    return Value & 0x00ffffff;
}

Y_FORCE_INLINE int TChunkReplica::GetReplicaIndex() const
{
    return (Value & 0x1f000000) >> 24;
}

Y_FORCE_INLINE void ToProto(ui32* value, TChunkReplica replica)
{
    *value = replica.Value;
}

Y_FORCE_INLINE void FromProto(TChunkReplica* replica, ui32 value)
{
    replica->Value = value;
}

////////////////////////////////////////////////////////////////////////////////

Y_FORCE_INLINE TChunkIdWithIndex::TChunkIdWithIndex()
    : ReplicaIndex(GenericChunkReplicaIndex)
{ }

Y_FORCE_INLINE TChunkIdWithIndex::TChunkIdWithIndex(TChunkId id, int replicaIndex)
    : Id(id)
    , ReplicaIndex(replicaIndex)
{ }

Y_FORCE_INLINE bool operator==(const TChunkIdWithIndex& lhs, const TChunkIdWithIndex& rhs)
{
    return lhs.Id == rhs.Id && lhs.ReplicaIndex == rhs.ReplicaIndex;
}

Y_FORCE_INLINE bool operator!=(const TChunkIdWithIndex& lhs, const TChunkIdWithIndex& rhs)
{
    return !(lhs == rhs);
}

Y_FORCE_INLINE bool operator<(const TChunkIdWithIndex& lhs, const TChunkIdWithIndex& rhs)
{
    if (lhs.Id == rhs.Id) {
        return lhs.ReplicaIndex < rhs.ReplicaIndex;
    }
    return lhs.Id < rhs.Id;
}

////////////////////////////////////////////////////////////////////////////////

Y_FORCE_INLINE TChunkIdWithIndexes::TChunkIdWithIndexes()
    : TChunkIdWithIndex()
    , MediumIndex(DefaultStoreMediumIndex)
{ }

Y_FORCE_INLINE TChunkIdWithIndexes::TChunkIdWithIndexes(const TChunkIdWithIndex& chunkIdWithIndex, int mediumIndex)
    : TChunkIdWithIndex(chunkIdWithIndex)
    , MediumIndex(mediumIndex)
{ }

Y_FORCE_INLINE TChunkIdWithIndexes::TChunkIdWithIndexes(TChunkId id, int replicaIndex, int mediumIndex)
    : TChunkIdWithIndex(id, replicaIndex)
    , MediumIndex(mediumIndex)
{ }

Y_FORCE_INLINE bool operator==(const TChunkIdWithIndexes& lhs, const TChunkIdWithIndexes& rhs)
{
    return static_cast<const TChunkIdWithIndex&>(lhs) == static_cast<const TChunkIdWithIndex&>(rhs) &&
        lhs.MediumIndex == rhs.MediumIndex;
}

Y_FORCE_INLINE bool operator!=(const TChunkIdWithIndexes& lhs, const TChunkIdWithIndexes& rhs)
{
    return !(lhs == rhs);
}

Y_FORCE_INLINE bool operator<(const TChunkIdWithIndexes& lhs, const TChunkIdWithIndexes& rhs)
{
    const auto& lhs_ = static_cast<const TChunkIdWithIndex&>(lhs);
    const auto& rhs_ = static_cast<const TChunkIdWithIndex&>(rhs);
    if (lhs_ == rhs_) {
        return lhs.MediumIndex < rhs.MediumIndex;
    }
    return lhs_ < rhs_;
}

////////////////////////////////////////////////////////////////////////////////

inline bool IsArtifactChunkId(TChunkId id)
{
    return NObjectClient::TypeFromId(id) == NObjectClient::EObjectType::Artifact;
}

inline bool IsPhysicalChunkType(NObjectClient::EObjectType type)
{
    return
        type == NObjectClient::EObjectType::Chunk ||
        type == NObjectClient::EObjectType::ErasureChunk ||
        type == NObjectClient::EObjectType::JournalChunk ||
        type == NObjectClient::EObjectType::ErasureJournalChunk;
}

inline bool IsJournalChunkType(NObjectClient::EObjectType type)
{
    return
        type == NObjectClient::EObjectType::JournalChunk ||
        type == NObjectClient::EObjectType::ErasureJournalChunk;
}

inline bool IsJournalChunkId(TChunkId id)
{
    return IsJournalChunkType(NObjectClient::TypeFromId(id));
}

inline bool IsBlobChunkType(NObjectClient::EObjectType type)
{
    return
        type == NObjectClient::EObjectType::Chunk ||
        type == NObjectClient::EObjectType::ErasureChunk;
}

inline bool IsBlobChunkId(TChunkId id)
{
    return IsBlobChunkType(NObjectClient::TypeFromId(id));
}

inline bool IsErasureChunkType(NObjectClient::EObjectType type)
{
    return
        type == NObjectClient::EObjectType::ErasureChunk ||
        type == NObjectClient::EObjectType::ErasureJournalChunk;
}

inline bool IsErasureChunkId(TChunkId id)
{
    return IsErasureChunkType(NObjectClient::TypeFromId(id));
}

inline bool IsErasureChunkPartId(TChunkId id)
{
    auto type = NObjectClient::TypeFromId(id);
    return
        type >= NObjectClient::MinErasureChunkPartType && type <= NObjectClient::MaxErasureChunkPartType ||
        type >= NObjectClient::MinErasureJournalChunkPartType && type <= NObjectClient::MaxErasureJournalChunkPartType;
}

inline bool IsRegularChunkType(NObjectClient::EObjectType type)
{
    return
        type == NObjectClient::EObjectType::Chunk ||
        type == NObjectClient::EObjectType::JournalChunk;
}

inline bool IsRegularChunkId(TChunkId id)
{
    return IsRegularChunkType(NObjectClient::TypeFromId(id));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient

inline size_t THash<NYT::NChunkClient::TChunkIdWithIndex>::operator()(const NYT::NChunkClient::TChunkIdWithIndex& value) const
{
    return THash<NYT::NChunkClient::TChunkId>()(value.Id) * 497 + value.ReplicaIndex;
}

inline size_t THash<NYT::NChunkClient::TChunkIdWithIndexes>::operator()(const NYT::NChunkClient::TChunkIdWithIndexes& value) const
{
    return THash<NYT::NChunkClient::TChunkId>()(value.Id) * 497 +
        value.ReplicaIndex + value.MediumIndex * 8;
}
