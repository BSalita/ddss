/*
   DDS, a bridge double dummy solver.

   Backend evaluation helpers:
   random deal generation, fast/hybrid/exact backends, and reports.
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
  int suitLen[DDS_HANDS][DDS_SUITS];
  int hcp[DDS_HANDS];
};

bool RunBackendEvaluation(
  const OptionsType& options);

bool RunPbnEvaluation(
  const OptionsType& options);

std::vector<RandomDeal> GenerateRandomDeals(
  int numDeals,
  int seed,
  int reducedCards);

#endif
