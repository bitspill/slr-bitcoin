// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "chainparams.h"
#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

#include <cmath>

//unsigned int GetNextTargetRequired(const CBlockIndex* pindexLast, bool fProofOfStake)
//{
//    int DiffMode = 1;
//    if (fTestNet)
//    {
//        if (pindexLast->nHeight+1 >= 1) { DiffMode = 2; }
//    }
//    else
//    {
//        if (pindexLast->nHeight+1 >= 310000) { DiffMode = 2; }
//    }
//
//    if (DiffMode == 1)
//    {
//        return GetNextTargetRequiredV1(pindexLast, fProofOfStake);
//    }
//    else
//    {
//        return GetNextTargetRequiredV2(pindexLast, fProofOfStake);
//    }
//}

unsigned int static KimotoGravityWell(const CBlockIndex* pindexLast, const CBlockHeader *pblock, uint64_t TargetBlocksSpacingSeconds, uint64_t PastBlocksMin, uint64_t PastBlocksMax, const Consensus::Params& params) {
//
//    if (pindexLast->nHeight+1 == 160 && (params.NetworkIDString() != CBaseChainParams::TESTNET))
//        return UintToArith256(params.posLimit).GetCompact();

    const CBlockIndex *BlockLastSolved  = pindexLast;
    const CBlockIndex *BlockReading = pindexLast;
    uint64_t  PastBlocksMass  = 0;
    int64_t   PastRateActualSeconds   = 0;
    int64_t   PastRateTargetSeconds   = 0;
    double  PastRateAdjustmentRatio = double(1);
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;
    double  EventHorizonDeviation;
    double  EventHorizonDeviationFast;
    double  EventHorizonDeviationSlow;

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || (uint64_t)BlockLastSolved->nHeight < PastBlocksMin) { return UintToArith256(params.powLimit).GetCompact(); }

    int64_t LatestBlockTime = BlockLastSolved->GetBlockTime();

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        PastBlocksMass++;

        if (i == 1){
            PastDifficultyAverage.SetCompact(BlockReading->nBits);
        } else {
            PastDifficultyAverage = ((arith_uint256().SetCompact(BlockReading->nBits) - PastDifficultyAveragePrev) / i) + PastDifficultyAveragePrev;
        }

        PastDifficultyAveragePrev = PastDifficultyAverage;
        PastRateActualSeconds   = BlockLastSolved->GetBlockTime() - BlockReading->GetBlockTime();

        if (LatestBlockTime < BlockReading->GetBlockTime()) {
            if (BlockReading->nHeight > 16000) { // HARD Fork block number
                LatestBlockTime = BlockReading->GetBlockTime();
            }
        }
        PastRateActualSeconds   = LatestBlockTime - BlockReading->GetBlockTime();
        PastRateTargetSeconds   = TargetBlocksSpacingSeconds * PastBlocksMass;
        PastRateAdjustmentRatio = double(1);

        if (PastRateActualSeconds < 0) {
            PastRateActualSeconds = 0;
        }
        if (BlockReading->nHeight > 16000) { // HARD Fork block number
            if (PastRateActualSeconds < 1) {
                PastRateActualSeconds = 1;
            }
        } else {
            if (PastRateActualSeconds < 0) { PastRateActualSeconds = 0; }
        }
        if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
            PastRateAdjustmentRatio = double(PastRateTargetSeconds) / double(PastRateActualSeconds);
        }
        EventHorizonDeviation   = 1 + (0.7084 * std::pow((double(PastBlocksMass)/double(144)), -1.228));
        EventHorizonDeviationFast   = EventHorizonDeviation;
        EventHorizonDeviationSlow   = 1 / EventHorizonDeviation;

        if (PastBlocksMass >= PastBlocksMin) {
            if ((PastRateAdjustmentRatio <= EventHorizonDeviationSlow) || (PastRateAdjustmentRatio >= EventHorizonDeviationFast)) {
                assert(BlockReading);
                break;
            }
        }
        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    arith_uint256 bnNew(PastDifficultyAverage);
    arith_uint256 bnProofOfWorkLimit = UintToArith256(params.powLimit);
    if (PastRateActualSeconds != 0 && PastRateTargetSeconds != 0) {
        bnNew *= PastRateActualSeconds;
        bnNew /= PastRateTargetSeconds;
    }
    if (bnNew > bnProofOfWorkLimit) {
        bnNew = bnProofOfWorkLimit;
    }

    /// debug print
    printf("Difficulty Retarget - Gravity Well\n");
    printf("PastRateAdjustmentRatio = %g\n", PastRateAdjustmentRatio);
    printf("Before: %08x %s\n", BlockLastSolved->nBits, arith_uint256().SetCompact(BlockLastSolved->nBits).ToString().c_str());
    printf("After: %08x %s\n", bnNew.GetCompact(), bnNew.ToString().c_str());

    return bnNew.GetCompact();
}

unsigned int static GetNextWorkRequired_V2(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params){
    // Kimoto Gravity Well (Patched for Time-warp exploit 4/5/14)
    static const int64_t  BlocksTargetSpacing = 60; // 1 minute
    unsigned int TimeDaySeconds  = 60 * 60 * 24;
    int64_t   PastSecondsMin  = TimeDaySeconds * 0.1;
    int64_t   PastSecondsMax  = TimeDaySeconds * 2.8;
    uint64_t  PastBlocksMin   = PastSecondsMin / BlocksTargetSpacing;
    uint64_t  PastBlocksMax   = PastSecondsMax / BlocksTargetSpacing;

    if (/*(params.NetworkIDString() == CBaseChainParams::TESTNET) &&*/ GetBoolArg("-zerogravity", false))
        return UintToArith256(params.powLimit).GetCompact();
    else
        return KimotoGravityWell(pindexLast, pblock, BlocksTargetSpacing, PastBlocksMin, PastBlocksMax, params);
}

unsigned int GetNextWorkRequired_V1(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Genesis block
    if (pindexLast == NULL)
        return nProofOfWorkLimit;

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval(pindexLast->nHeight+1) != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval(pindex->nHeight) != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int interval = params.DifficultyAdjustmentInterval(pindexLast->nHeight+1);
    int blockstogoback;
    if ((pindexLast->nHeight+1) != interval)
        // Litecoin: This fixes an issue where a 51% attack can change difficulty at will.
        // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
        blockstogoback = interval;
    else
        blockstogoback = interval-1;

    int nHeightFirst = pindexLast->nHeight - blockstogoback;
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params){
    int DiffMode = 1;
//    if (Params().NetworkIDString() == CBaseChainParams::TESTNET) {
//        if (pindexLast->nHeight+1 >= 1) { DiffMode = 2; }
//    }
//    else {
        if (pindexLast->nHeight+1 >= 310000) { DiffMode = 2; }
//    }

    if (DiffMode == 1) { return GetNextWorkRequired_V1(pindexLast, pblock, params); }
    else if (DiffMode == 2) { return GetNextWorkRequired_V2(pindexLast, pblock, params); }
    return GetNextWorkRequired_V2(pindexLast, pblock, params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    LogPrintf("  nActualTimespan = %d  before bounds\n", nActualTimespan);
    int64_t targetTimespan = params.PowTargetTimespan(pindexLast->nHeight + 1);
    if (nActualTimespan < targetTimespan/4)
        nActualTimespan = targetTimespan/4;
    if (nActualTimespan > targetTimespan*4)
        nActualTimespan = targetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    bnNew *= nActualTimespan;
    bnNew /= targetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    /// debug print
    LogPrintf("GetNextWorkRequired RETARGET\n");
    LogPrintf("targetTimespan = %d    nActualTimespan = %d\n", targetTimespan, nActualTimespan);
    LogPrintf("Before: %08x  %s\n", pindexLast->nBits, bnOld.ToString());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return error("CheckProofOfWork(): nBits below minimum work");

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return error("CheckProofOfWork(): hash doesn't match nBits");

    return true;
}

arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}
