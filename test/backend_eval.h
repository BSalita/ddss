/*
   DDS, a bridge double dummy solver.

   Backend evaluation helpers:
   random deal generation, CPU exact solving, and reports.
*/

#ifndef DTEST_BACKEND_EVAL_H
#define DTEST_BACKEND_EVAL_H

#include <string>
#include <vector>

#include "../include/dll.h"
#include "cst.h"

struct RandomDeal
{
  dealPBN deal;
};

bool RunBackendEvaluation(
  const OptionsType& options);

bool RunPbnEvaluation(
  const OptionsType& options);

// Loop --numthr from threadSweepMin..Max (step), write CSV + ranking.
// Requires randomDeals > 0.  Optional --verify compares OOB engines too.
bool RunThreadSweep(
  const OptionsType& options);

std::vector<RandomDeal> GenerateRandomDeals(
  int numDeals,
  int seed);

#endif
