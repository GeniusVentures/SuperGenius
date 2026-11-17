/**
 * @file       cold_read_fallback.hpp
 * @brief      Ungated cold-read hook interface (READ-01, v1.1 Phase 9).
 *
 * ALWAYS COMPILED and deliberately free of every sgns-blockstore type:
 * this header is the seam-side mirror of the library's
 * StatusClass/BlockSource taxonomy - no library type ever crosses the
 * gated/ungated boundary (Phase-7/8 header-hygiene gate). The gated
 * implementation lives in crdt/globaldb/ColdReadService.hpp/.cpp
 * (compiled only under SGNS_COLD_STORAGE); GraphsyncDAGSyncer holds the
 * hook as a std::shared_ptr<ColdReadFallback> injected at its single
 * construction site (globaldb.cpp) - null when cold storage is disabled,
 * which keeps disabled builds byte-identical (Phase-7 bar).
 */

#ifndef SUPERGENIUS_CRDT_COLD_READ_FALLBACK_HPP
#define SUPERGENIUS_CRDT_COLD_READ_FALLBACK_HPP

#include <memory>

#include "base/buffer.hpp"
#include "crdt/graphsync_dagsyncer.hpp"

namespace sgns::crdt
{
    /**
     * @brief      Seam-side mirror of the library's failure taxonomy
     *             (READ-01/SC3; the library's BlockStatus never crosses
     *             the ungated boundary).
     *
     * Class carries the four honest failure classes exactly as the
     * library defines them (BlockStatus.hpp): NotFound is authoritative
     * absence ONLY - a transport-shaped failure is TemporarilyUnavailable
     * and must never surface as NotFound (SC1/D-03). fromRemoteTier
     * mirrors BlockSource attribution: false = the Local tier
     * (LocalBlockStore cold dir), true = the Remote tier (S3BlockStore).
     */
    struct ColdReadStatus
    {
        enum class Class
        {
            NotFound,               ///< authoritative absence only (SC1)
            TemporarilyUnavailable, ///< transport/outage/timeout - never NotFound (D-03)
            Corrupt,                ///< bytes failed CID verification; tier attributed (SC3)
            Unauthorized,           ///< authentication/permission failure (SC3)
        };

        Class cls{ Class::NotFound }; ///< the failure classification (meaningful on failure)
        bool   fromRemoteTier{ false }; ///< tier that produced the failure (false=Local, true=Remote)
    };

    /**
     * @brief      Seam-side classification of the node-local prune
     *             evidence for one CID (SERVE-03/D-04, v1.2 Phase 15).
     *
     * The ONE role-aware predicate every classifying cold-serve surface
     * (getNode's cold leg, GetNodeWithoutRequest, the re-warm worker)
     * derives from via LookupPruneEvidence; HasBlock projects the same
     * source onto bool without consulting it (D-04 source-consistency
     * ruling). Mirrors the ColdReadStatus discipline: NO transaction-layer
     * type (PrunedLookup, PrunedBlockLookup) ever crosses the gated/ungated
     * boundary. Role-aware by construction (A4): only pruning nodes (Light)
     * ever write /prune/blk rows, so the predicate is vacuously NoEvidence
     * on non-pruning roles - no explicit role flag is needed.
     */
    enum class PruneEvidenceClass : uint8_t
    {
        NoEvidence, ///< no /prune/blk row: cold bytes are restorable history (SC2)
        Pruned,     ///< row present: node-local terminal refusal - bytes are never served, never re-warmed (SC1)
        Malformed,  ///< row failed decode: fail-closed hard error, never a silent verdict (Pitfall 7)
    };

    /**
     * @brief      Cold-read fallback hook (READ-01): the D-01/D-02 chain
     *             (LocalBlockStore then S3BlockStore, both CID-verifying)
     *             plus the D-04 rehydrate path, behind an interface the
     *             ungated GraphsyncDAGSyncer TU can hold.
     */
    class ColdReadFallback
    {
    public:
        virtual ~ColdReadFallback() = default;

        /**
         * @brief       Fetch a block through the cold chain
         *              (LocalBlockStore -> S3BlockStore; D-01/D-02).
         * @param[in]   cid the content identifier
         * @param[out]  out receives the CID-verified bytes on success
         *              (the library's internal Get verification is the
         *              integrity control - never bypassed)
         * @return      the honest classification on failure; success is
         *              signaled by a non-empty out buffer (DAG blocks
         *              are never empty - the status payload is
         *              meaningful only on failure)
         * @note        A Local-tier NotFound is expected and non-terminal
         *              (the chain continues to S3); a Remote-tier NotFound
         *              is definitive absence at both cold tiers (SC1).
         *              A cold transport failure is classified
         *              TemporarilyUnavailable with the failing tier's
         *              attribution - never NotFound (D-03).
         */
        virtual ColdReadStatus Get( const CID &cid, sgns::common::Buffer &out ) = 0;

        /**
         * @brief       Cold-dir presence check, LOCAL TIER ONLY (D-08/D-09).
         * @param[in]   cid the content identifier
         * @return      true when the cold dir holds the CID; false on
         *              absence AND on any non-authoritative failure (the
         *              bool contract must never block or throw for the
         *              freeze-sensitive pubsub-path callers - D-08)
         * @note        NEVER performs S3 or network I/O: HasBlock runs on
         *              the pubsub receive path where remote I/O re-opens
         *              the whole-node-freeze hazard (D-08).
         */
        virtual bool HasLocalCold( const CID &cid ) = 0;

        /**
         * @brief       Disposed-marker point lookup (D-11, v1.1 Phase 11).
         * @param[in]   cid the content identifier
         * @return      true when the disposed-marker store holds the CID
         *              (the block was evicted locally and its re-delivery
         *              is already integrated); false on absence AND on
         *              any non-authoritative failure (the same
         *              freeze-safe bool contract as HasLocalCold)
         * @note        NEVER performs S3 or network I/O (the pubsub
         *              receive path calls this). D-12 split semantics: a
         *              marker hit answers HasBlock for DEDUP ONLY -
         *              direct reads (GetNodeWithoutRequest, the responder
         *              cold-serve chain) NEVER consult the marker as
         *              absence and always resolve the CID through the
         *              cold chain.
         */
        virtual bool HasDisposed( const CID &cid ) = 0;

        /**
         * @brief       Try-enqueue a background re-warm for a CID
         *              (D-05/D-06/D-07; the bounded executor arrives in
         *              09-02).
         * @param[in]   cid the content identifier to re-warm
         * @return      true when enqueued (or already in flight); false
         *              when no queue exists yet, the queue is full, or
         *              cold storage is disabled
         * @note        A real try-op, never a placeholder: Phase 9 plan
         *              09-01 ships it with no executor behind it (the
         *              queue exists from 09-02 on), so it returns false
         *              today. It NEVER blocks the caller and never runs
         *              the fetch inline (SC2).
         */
        virtual bool EnqueueRewarm( const CID &cid ) = 0;

        /**
         * @brief       Re-cache fetched bytes into the hot store through
         *              the dedicated rehydrate path (D-04/SC4).
         * @param[in]   cid the content identifier
         * @param[in]   bytes the CID-verified payload
         * @note        Writes BYTES ONLY through the Phase-8 decorator
         *              set() (journal + hot write): zero pubsub
         *              broadcast, zero route additions, zero head
         *              changes (SC4), and NO resolved/seal marker this
         *              phase - sealing an unprocessed crash-window block
         *              would make its delta permanently unmergeable
         *              (09-RESEARCH §2.4 ruling: bytes only).
         */
        virtual void Rehydrate( const CID &cid, const sgns::common::Buffer &bytes ) = 0;

        /**
         * @brief       Classify the node-local prune evidence for @p cid
         *              (SERVE-03/D-04, v1.2 Phase 15).
         * @param[in]   cid the content identifier
         * @return      NoEvidence when no /prune/blk row exists (cold bytes
         *              are restorable history - SC2); Pruned when a decoded
         *              row exists (node-local TERMINAL refusal - SC1);
         *              Malformed when the row failed decode (fail-closed
         *              hard error - Pitfall 7)
         * @note        The ONE role-aware predicate every classifying
         *              surface derives from (D-04 source-consistency);
         *              implemented over a GlobalDB-installed callable at
         *              the OpenColdStorageStack seam. A null hook is never
         *              consulted - disabled builds stay byte-identical
         *              (the Phase-7 bar). O(1) point lookup (WR-04: never
         *              a scan), stateless, safe on the CRDT DAG worker and
         *              the re-warm worker alike.
         */
        virtual PruneEvidenceClass LookupPruneEvidence( const CID &cid ) = 0;
    };

    /**
     * @brief       Map a ColdReadStatus onto the DAGSyncer error surface
     *              (SC3 mapping table, 09-RESEARCH §3.2).
     * @param[in]   status the cold-chain classification
     * @return      the honest GraphsyncDAGSyncer::Error value
     * @note        NotFound maps to CID_NOT_FOUND (an authoritative miss
     *              at both cold tiers is the only legitimate NotFound -
     *              SC1); transport-shaped failures surface as
     *              COLD_STORAGE_TEMPORARILY_UNAVAILABLE, never as
     *              CID_NOT_FOUND (D-03); Corrupt and Unauthorized keep
     *              their own classes so they are never conflated with
     *              absence or transport (SC3).
     */
    inline GraphsyncDAGSyncer::Error MapColdReadStatus( const ColdReadStatus &status )
    {
        switch ( status.cls )
        {
            case ColdReadStatus::Class::NotFound:
                return GraphsyncDAGSyncer::Error::CID_NOT_FOUND;
            case ColdReadStatus::Class::TemporarilyUnavailable:
                return GraphsyncDAGSyncer::Error::COLD_STORAGE_TEMPORARILY_UNAVAILABLE;
            case ColdReadStatus::Class::Corrupt:
                return GraphsyncDAGSyncer::Error::COLD_STORAGE_CORRUPT;
            case ColdReadStatus::Class::Unauthorized:
                return GraphsyncDAGSyncer::Error::COLD_STORAGE_UNAUTHORIZED;
        }
        return GraphsyncDAGSyncer::Error::CID_NOT_FOUND; // unreachable - exhaustive switch
    }

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_COLD_READ_FALLBACK_HPP
