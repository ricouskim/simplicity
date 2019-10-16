// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018-2019 The Simplicity developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "chain.h"
#include "chainparams.h"
#include "main.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

#include <math.h>

const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake)
{
    while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
        pindex = pindex->pprev;
    return pindex;
}

const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, unsigned int algo)
{
    while (pindex && pindex->pprev && (pindex->nBlockType != algo))
        pindex = pindex->pprev;
    return pindex;
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, bool fProofOfStake)
{
    /* current difficulty formula, pivx - DarkGravity v3, written by Evan Duffield - evan@dashpay.io */
    const CBlockIndex* BlockLastSolved = pindexLast;
    const CBlockIndex* BlockReading = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 24;
    int64_t PastBlocksMax = 24;
    int64_t CountBlocks = 0;
    uint256 PastDifficultyAverage;
    uint256 PastDifficultyAveragePrev;

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < PastBlocksMin) {
        return Params().ProofOfWorkLimit(pblock->nBlockType).GetCompact();
    }

    int height = pindexLast->nHeight + 1;
    if (height >= Params().WALLET_UPGRADE_BLOCK()) {
        uint256 bnTargetLimit = fProofOfStake ? Params().ProofOfStakeLimit() : Params().ProofOfWorkLimit(pblock->nBlockType);
        //if (!fProofOfStake)
            //return bnTargetLimit.GetCompact(); // for testing

        int64_t nActualSpacing = 0; // difficulty for PoW and PoS are calculated separately
        const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, pblock->nBlockType);
        const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, pblock->nBlockType);
        nActualSpacing = pindexPrev->GetBlockTime() - pindexPrevPrev->GetBlockTime();

        if (nActualSpacing <= 0)
            nActualSpacing = 1;

        // ppcoin: target change every block
        // ppcoin: retarget with exponential moving toward target spacing
        uint256 bnNew;
        bnNew.SetCompact(pindexPrev->nBits);

        bnNew *= ((Params().Interval() - 1) * ((ALGO_COUNT-1) * Params().TargetSpacing()) + nActualSpacing + nActualSpacing); // quark is disabled
        bnNew /= ((Params().Interval() + 1) * ((ALGO_COUNT-1) * Params().TargetSpacing())); // 160 second block time for PoW + 160 second block time for PoS = 80 second effective block time

        if (Params().NetworkID() == CBaseChainParams::MAIN)
            if (height < (Params().WALLET_UPGRADE_BLOCK()+10) && height >= Params().WALLET_UPGRADE_BLOCK())
                bnNew *= (int)pow(4.0, 10.0+Params().WALLET_UPGRADE_BLOCK()-height); // slash difficulty and gradually ramp back up over 10 blocks

        if (bnNew <= 0 || bnNew > bnTargetLimit)
            bnNew = bnTargetLimit;

        return bnNew.GetCompact();
    }

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) {
            break;
        }
        CountBlocks++;

        if (CountBlocks <= PastBlocksMin) {
            if (CountBlocks == 1) {
                PastDifficultyAverage.SetCompact(BlockReading->nBits);
            } else {
                PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks) + (uint256().SetCompact(BlockReading->nBits))) / (CountBlocks + 1);
            }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        if (LastBlockTime > 0) {
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += Diff;
        }
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    uint256 bnNew(PastDifficultyAverage);

    int64_t _nTargetTimespan = CountBlocks * Params().TargetSpacing();

    if (nActualTimespan < _nTargetTimespan / 3)
        nActualTimespan = _nTargetTimespan / 3;
    if (nActualTimespan > _nTargetTimespan * 3)
        nActualTimespan = _nTargetTimespan * 3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= _nTargetTimespan;

    if (bnNew > Params().ProofOfWorkLimit(pblock->nBlockType)) {
        bnNew = Params().ProofOfWorkLimit(pblock->nBlockType);
    }

    return bnNew.GetCompact();
}

unsigned int GetLegacyNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, bool fProofOfStake)
{
    int64_t nTargetSpacing = 80;
    int64_t nTargetTimespan = 20 * 60;

    uint256 bnTargetLimit = fProofOfStake ? ~uint256(0) >> 20 : Params().ProofOfWorkLimit(POW_QUARK);

    if (pindexLast == NULL)
        return bnTargetLimit.GetCompact(); // genesis block

    const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, fProofOfStake);
    if (pindexPrev->pprev == NULL)
        return bnTargetLimit.GetCompact(); // first block

    const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, fProofOfStake);
    if (pindexPrevPrev->pprev == NULL)
        return bnTargetLimit.GetCompact(); // second block

    int64_t nActualSpacing = pindexPrev->GetBlockTime() - pindexPrevPrev->GetBlockTime();

    if (nActualSpacing < 0)
        nActualSpacing = nTargetSpacing;

    uint256 bnNew;
    bnNew.SetCompact(pindexPrev->nBits);
    int64_t nInterval = nTargetTimespan / nTargetSpacing;
    bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
    bnNew /= ((nInterval + 1) * nTargetSpacing);

    if (bnNew <= 0 || bnNew > bnTargetLimit)
        bnNew = bnTargetLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(const CBlockHeader* pblock, uint256 hash)
{
    bool fNegative;
    bool fOverflow;
    uint256 bnTarget;

    if (Params().SkipProofOfWorkCheck())
        return true;

    bnTarget.SetCompact(pblock->nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > Params().ProofOfWorkLimit(pblock->nBlockType))
        return error("CheckProofOfWork() : nBits below minimum work");

    // Check proof of work matches claimed amount
    if (hash.GetCompact() > bnTarget) {
        if (Params().MineBlocksOnDemand())
            return false;
        else
            return error("CheckProofOfWork() : hash doesn't match nBits");
    }

    return true;
}

uint256 GetBlockProof(const CBlockIndex& block)
{
    uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}