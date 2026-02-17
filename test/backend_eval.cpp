/*
   DDS, a bridge double dummy solver.

   Backend evaluation helpers:
   random deal generation, fast/hybrid/exact backends, and reports.
*/

#include "backend_eval.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string.h>
#include <vector>

#ifdef _WIN32
  #include <direct.h>
  #include <windows.h>
#else
  #include <dlfcn.h>
  #include <unistd.h>
#endif
#include <sys/stat.h>

using namespace std;

namespace
{
  struct Card
  {
    int suit;
    int rank;
  };

  struct TargetResult
  {
    string name;
    vector<ddTableResults> tables;
    vector<array<double, 20> > confidence;
    double elapsedMs;
    int cpuFallbackDeals;
    string runtimeTag;
  };

  static const char SEAT_CHARS[DDS_HANDS] = {'N', 'E', 'S', 'W'};
  static const char RANK_CHARS[13] = {'A', 'K', 'Q', 'J', 'T', '9', '8', '7',
    '6', '5', '4', '3', '2'};
  static const int RANK_VALUES[13] = {14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2};

  inline int clamp_int(
    const int x,
    const int lo,
    const int hi)
  {
    if (x < lo)
      return lo;
    if (x > hi)
      return hi;
    return x;
  }

  inline double clamp_double(
    const double x,
    const double lo,
    const double hi)
  {
    if (x < lo)
      return lo;
    if (x > hi)
      return hi;
    return x;
  }

  void PrintProgress(
    const string& stage,
    const int done,
    const int total,
    const chrono::high_resolution_clock::time_point& t0)
  {
    const int pct = static_cast<int>(
      floor(100.0 * static_cast<double>(done) / static_cast<double>(total)));
    const chrono::high_resolution_clock::time_point now =
      chrono::high_resolution_clock::now();
    const double ms = chrono::duration_cast<chrono::duration<double, milli> >(
      now - t0).count();
    cout << "[" << stage << "] "
      << done << "/" << total << " (" << pct << "%) "
      << "elapsed=" << fixed << setprecision(1) << ms << " ms\n";
  }

  bool ShouldPrintProgress(
    const int done,
    const int total)
  {
    if (done == total)
      return true;
    const int step = max(1, total / 20); // 5% increments
    return (done % step) == 0;
  }

  string RuntimeTagFast(
    const int totalDeals)
  {
    return "CPU_FAST_HEURISTIC";
  }

  string RuntimeTagHybrid(
    const int fallbackDeals,
    const int totalDeals)
  {
    ostringstream oss;
    if (fallbackDeals > 0)
      oss << "CPU_HYBRID+CPU_FALLBACK(fallback_deals=" << fallbackDeals << "/" << totalDeals << ")";
    else
      oss << "CPU_HYBRID_ONLY(fallback_deals=0/" << totalDeals << ")";
    return oss.str();
  }

  string RuntimeTagExact(
    const int cpuFallbackDeals)
  {
    if (cpuFallbackDeals <= 0)
      return "CPU_EXACT";
    {
      ostringstream oss;
      oss << "CPU_EXACT(deals=" << cpuFallbackDeals << ")";
      return oss.str();
    }
  }

  string SeatOrderPBN(
    const vector<Card>& cards)
  {
    bool have[DDS_SUITS][13];
    for (int s = 0; s < DDS_SUITS; s++)
      for (int r = 0; r < 13; r++)
        have[s][r] = false;

    for (unsigned i = 0; i < cards.size(); i++)
    {
      const Card c = cards[i];
      for (int r = 0; r < 13; r++)
      {
        if (RANK_VALUES[r] == c.rank)
        {
          have[c.suit][r] = true;
          break;
        }
      }
    }

    string hand = "";
    for (int s = 0; s < DDS_SUITS; s++)
    {
      bool empty = true;
      for (int r = 0; r < 13; r++)
      {
        if (have[s][r])
        {
          hand += RANK_CHARS[r];
          empty = false;
        }
      }
      if (empty)
        hand += "-";
      if (s != DDS_SUITS - 1)
        hand += ".";
    }

    return hand;
  }

  int HcpFromRank(
    const int rank)
  {
    if (rank == 14)
      return 4;
    if (rank == 13)
      return 3;
    if (rank == 12)
      return 2;
    if (rank == 11)
      return 1;
    return 0;
  }

  bool MakeDirIfNeeded(
    const string& dir)
  {
#ifdef _WIN32
    const int ret = _mkdir(dir.c_str());
#else
    const int ret = mkdir(dir.c_str(), 0755);
#endif
    if (ret == 0)
      return true;

    struct stat sb;
    if (stat(dir.c_str(), &sb) == 0)
      return true;

    return false;
  }

  int SolveCpuExact(
    const dealPBN& deal,
    ddTableResults& outTable)
  {
    ddTableDealPBN td;
    memset(&td, 0, sizeof(td));
    strncpy(td.cards, deal.remainCards, sizeof(td.cards)-1);
    return CalcDDtablePBN(td, &outTable);
  }

  bool SolveCpuExactBatched(
    const vector<dealPBN>& deals,
    int nDeals,
    vector<ddTableResults>& outTables,
    const string& progressLabel)
  {
    outTables.resize(static_cast<unsigned>(nDeals));

    const chrono::high_resolution_clock::time_point t0 =
      chrono::high_resolution_clock::now();

    vector<ddTableDealPBN> cards(static_cast<size_t>(nDeals));
    for (int j = 0; j < nDeals; j++)
    {
      memset(&cards[static_cast<size_t>(j)], 0, sizeof(ddTableDealPBN));
      strncpy(cards[static_cast<size_t>(j)].cards,
              deals[static_cast<unsigned>(j)].remainCards,
              sizeof(cards[0].cards) - 1);
    }

    int trumpFilter[DDS_STRAINS] = {0, 0, 0, 0, 0};

    const int ret = CalcAllTablesPBNx(
      nDeals, cards.data(), -1, trumpFilter,
      outTables.data(), nullptr);

    if (ret != RETURN_NO_FAULT)
    {
      cout << "CalcAllTablesPBNx failed, ret " << ret << "\n";
      return false;
    }

    const chrono::high_resolution_clock::time_point t1 =
      chrono::high_resolution_clock::now();
    const double ms =
      chrono::duration_cast<chrono::duration<double, milli> >(t1 - t0).count();
    cout << progressLabel << " solved " << nDeals
         << " deals in " << ms << " ms ("
         << (nDeals * 1000.0 / ms) << " tables/s)\n";

    return true;
  }

  void SolveFastHeuristic(
    const RandomDeal& deal,
    ddTableResults& table,
    array<double, 20>& confidence)
  {
    for (int strain = 0; strain < DDS_STRAINS; strain++)
    {
      for (int declarer = 0; declarer < DDS_HANDS; declarer++)
      {
        const int partner = (declarer + 2) % DDS_HANDS;
        const int hcpSide = deal.hcp[declarer] + deal.hcp[partner];

        double fitBonus = 0.0;
        int fit = 0;
        if (strain < DDS_SUITS)
        {
          fit = deal.suitLen[declarer][strain] + deal.suitLen[partner][strain];
          if (fit >= 8)
            fitBonus = 0.7 + 0.35 * static_cast<double>(fit - 8);
          else
            fitBonus = -0.25 * static_cast<double>(8 - fit);
        }

        double raw = 6.0 + 0.22 * static_cast<double>(hcpSide - 20) + fitBonus;
        const int tricks = clamp_int(static_cast<int>(floor(raw + 0.5)), 0, 13);
        table.resTable[strain][declarer] = tricks;

        double conf = 0.35 + min(0.45, 0.04 * static_cast<double>(abs(hcpSide - 20)));
        if (strain < DDS_SUITS)
        {
          if (fit >= 8)
            conf += 0.2;
          else
            conf -= 0.15;
        }
        const double boundary = fabs(raw - floor(raw + 0.5));
        conf -= 0.4 * boundary;
        confidence[4 * strain + declarer] = clamp_double(conf, 0.05, 0.99);
      }
    }
  }

  void SolveHybrid(
    const RandomDeal& deal,
    const double threshold,
    ddTableResults& outTable,
    array<double, 20>& confidence,
    int& fallbackCells,
    bool& usedFallback)
  {
    SolveFastHeuristic(deal, outTable, confidence);

    bool anyFallback = false;
    bool replaceMask[20];
    for (int i = 0; i < 20; i++)
    {
      replaceMask[i] = confidence[i] < threshold;
      if (replaceMask[i])
      {
        anyFallback = true;
        fallbackCells++;
      }
    }

    if (! anyFallback)
    {
      usedFallback = false;
      return;
    }

    ddTableResults exactTable;
    const int ret = SolveCpuExact(deal.deal, exactTable);
    if (ret != RETURN_NO_FAULT)
    {
      usedFallback = false;
      return;
    }

    for (int strain = 0; strain < DDS_STRAINS; strain++)
    {
      for (int declarer = 0; declarer < DDS_HANDS; declarer++)
      {
        const int idx = 4 * strain + declarer;
        if (replaceMask[idx])
          outTable.resTable[strain][declarer] = exactTable.resTable[strain][declarer];
      }
    }
    usedFallback = true;
  }

  int ContractValue(
    const ddTableResults& t)
  {
    int best = t.resTable[0][0];
    for (int strain = 0; strain < DDS_STRAINS; strain++)
      for (int declarer = 0; declarer < DDS_HANDS; declarer++)
        best = max(best, t.resTable[strain][declarer]);
    return best;
  }

  int ParProxy(
    const ddTableResults& t)
  {
    int low = t.resTable[0][0];
    int high = t.resTable[0][0];
    for (int strain = 0; strain < DDS_STRAINS; strain++)
    {
      for (int declarer = 0; declarer < DDS_HANDS; declarer++)
      {
        low = min(low, t.resTable[strain][declarer]);
        high = max(high, t.resTable[strain][declarer]);
      }
    }
    return high - low;
  }

  void WriteSummaryJson(
    const string& path,
    const string& target,
    const int deals,
    const int cells,
    const double exactRate,
    const double mae,
    const double off1,
    const double off2,
    const double contractDisagreement,
    const double parDisagreement,
    const double elapsedMs,
    const double tablesPerSec,
    const double solutionsPerSec,
    const string& runtimeTag,
    const vector<string>& failures)
  {
    ofstream f(path.c_str());
    f << "{\n";
    f << "  \"compare_target\": \"" << target << "\",\n";
    f << "  \"num_deals\": " << deals << ",\n";
    f << "  \"num_cells\": " << cells << ",\n";
    f << "  \"exact_match_rate\": " << fixed << setprecision(8) << exactRate << ",\n";
    f << "  \"mae\": " << mae << ",\n";
    f << "  \"off_by_one_rate\": " << off1 << ",\n";
    f << "  \"off_by_two_plus_rate\": " << off2 << ",\n";
    f << "  \"contract_disagreement_rate\": " << contractDisagreement << ",\n";
    f << "  \"par_disagreement_rate\": " << parDisagreement << ",\n";
    f << "  \"elapsed_ms\": " << elapsedMs << ",\n";
    f << "  \"dd_tables_per_sec\": " << tablesPerSec << ",\n";
    f << "  \"dd_solutions_per_sec\": " << solutionsPerSec << ",\n";
    f << "  \"runtime_tag\": \"" << runtimeTag << "\"";
    if (failures.size() > 0)
    {
      f << ",\n  \"policy_fail_reasons\": [\n";
      for (unsigned i = 0; i < failures.size(); i++)
      {
        f << "    \"" << failures[i] << "\"";
        if (i + 1 != failures.size())
          f << ",";
        f << "\n";
      }
      f << "  ]\n";
    }
    else
      f << "\n";
    f << "}\n";
  }
}


vector<RandomDeal> GenerateRandomDeals(
  const int numDeals,
  const int seed,
  const int reducedCards)
{
  vector<RandomDeal> out;
  out.resize(static_cast<unsigned>(numDeals));

  mt19937 rng(static_cast<unsigned>(seed));
  const int cardsPerHand = clamp_int(reducedCards, 1, 13);

  vector<Card> deck;
  deck.reserve(52U);
  for (int suit = 0; suit < DDS_SUITS; suit++)
    for (int rank = 2; rank <= 14; rank++)
      deck.push_back(Card{suit, rank});

  for (int d = 0; d < numDeals; d++)
  {
    shuffle(deck.begin(), deck.end(), rng);

    vector<Card> handCards[DDS_HANDS];
    for (int seat = 0; seat < DDS_HANDS; seat++)
      handCards[seat].reserve(13U);

    for (unsigned i = 0; i < deck.size(); i++)
      handCards[static_cast<int>(i % 4U)].push_back(deck[i]);

    RandomDeal rd;
    memset(&rd, 0, sizeof(rd));
    rd.deal.trump = 0;
    rd.deal.first = 0;
    for (int k = 0; k < 3; k++)
    {
      rd.deal.currentTrickSuit[k] = 0;
      rd.deal.currentTrickRank[k] = 0;
    }

    const int dealer = static_cast<int>(rng() % 4U);
    string pbn = "";
    pbn += SEAT_CHARS[dealer];
    pbn += ":";
    for (int off = 0; off < DDS_HANDS; off++)
    {
      const int seat = (dealer + off) % DDS_HANDS;
      vector<Card> reduced;
      reduced.reserve(static_cast<size_t>(cardsPerHand));
      for (int c = 0; c < cardsPerHand; c++)
        reduced.push_back(handCards[seat][static_cast<size_t>(c)]);
      pbn += SeatOrderPBN(reduced);
      if (off + 1 != DDS_HANDS)
        pbn += " ";
    }

    strncpy(rd.deal.remainCards, pbn.c_str(), sizeof(rd.deal.remainCards)-1);
    rd.deal.remainCards[sizeof(rd.deal.remainCards)-1] = '\0';

    for (int seat = 0; seat < DDS_HANDS; seat++)
    {
      for (int i = 0; i < cardsPerHand; i++)
      {
        const Card c = handCards[seat][static_cast<size_t>(i)];
        rd.suitLen[seat][c.suit]++;
        rd.hcp[seat] += HcpFromRank(c.rank);
      }
    }

    out[static_cast<unsigned>(d)] = rd;
  }

  return out;
}


bool RunBackendEvaluation(
  const OptionsType& options)
{
  if (options.randomDeals <= 0)
  {
    cout << "Random-deals mode disabled\n";
    return true;
  }

  if (! MakeDirIfNeeded(options.reportDir))
  {
    cout << "Could not create report directory '" << options.reportDir << "'\n";
    return false;
  }

  vector<RandomDeal> deals = GenerateRandomDeals(
    options.randomDeals, options.randomSeed, options.reducedCards);
  const int nDeals = static_cast<int>(deals.size());
  const int cells = nDeals * 20;
  cout << "Generating baseline + backend runs for " << nDeals << " random deals\n";

  vector<dealPBN> dealPbns(static_cast<unsigned>(nDeals));
  for (int i = 0; i < nDeals; i++)
    dealPbns[static_cast<unsigned>(i)] = deals[static_cast<unsigned>(i)].deal;

  vector<ddTableResults> cpuExact;
  if (!SolveCpuExactBatched(dealPbns, nDeals, cpuExact, "cpu-exact-baseline"))
    return false;

  vector<TargetResult> targets;

  auto runFast = [&]() -> TargetResult
  {
    TargetResult tr;
    tr.name = "fast";
    tr.cpuFallbackDeals = 0;
    tr.tables.resize(static_cast<unsigned>(nDeals));
    tr.confidence.resize(static_cast<unsigned>(nDeals));
    const chrono::high_resolution_clock::time_point t0 = chrono::high_resolution_clock::now();
    tr.cpuFallbackDeals = nDeals;
    for (int i = 0; i < nDeals; i++)
    {
      SolveFastHeuristic(deals[static_cast<unsigned>(i)], tr.tables[static_cast<unsigned>(i)],
        tr.confidence[static_cast<unsigned>(i)]);
      if (ShouldPrintProgress(i + 1, nDeals))
        PrintProgress("fast", i + 1, nDeals, t0);
    }
    const chrono::high_resolution_clock::time_point t1 = chrono::high_resolution_clock::now();
    tr.elapsedMs = chrono::duration_cast<chrono::duration<double, milli> >(t1-t0).count();
    tr.runtimeTag = RuntimeTagFast(nDeals);
    cout << "runtime-tag[" << tr.name << "]: " << tr.runtimeTag << "\n";
    return tr;
  };

  auto runHybrid = [&]() -> TargetResult
  {
    TargetResult tr;
    tr.name = "hybrid";
    tr.cpuFallbackDeals = 0;
    tr.tables.resize(static_cast<unsigned>(nDeals));
    tr.confidence.resize(static_cast<unsigned>(nDeals));
    int fallbackDeals = 0;
    int fallbackCells = 0;
    const chrono::high_resolution_clock::time_point t0 = chrono::high_resolution_clock::now();
    for (int i = 0; i < nDeals; i++)
    {
      bool usedFallback = false;
      SolveHybrid(deals[static_cast<unsigned>(i)], options.confidenceThreshold,
        tr.tables[static_cast<unsigned>(i)], tr.confidence[static_cast<unsigned>(i)],
        fallbackCells, usedFallback);
      if (usedFallback)
      {
        fallbackDeals++;
        tr.cpuFallbackDeals++;
      }
      if (ShouldPrintProgress(i + 1, nDeals))
        PrintProgress("hybrid", i + 1, nDeals, t0);
    }
    const chrono::high_resolution_clock::time_point t1 = chrono::high_resolution_clock::now();
    tr.elapsedMs = chrono::duration_cast<chrono::duration<double, milli> >(t1-t0).count();
    tr.runtimeTag = RuntimeTagHybrid(fallbackDeals, nDeals);
    cout << "Hybrid fallback used on " << fallbackDeals << "/" << nDeals <<
      " deals (" << fallbackCells << " cells)\n";
    cout << "runtime-tag[" << tr.name << "]: " << tr.runtimeTag << "\n";
    return tr;
  };

  auto runExact = [&]() -> TargetResult
  {
    TargetResult tr;
    tr.name = "exact";
    tr.cpuFallbackDeals = 0;
    tr.tables.resize(static_cast<unsigned>(nDeals));
    tr.confidence.resize(static_cast<unsigned>(nDeals));
    const chrono::high_resolution_clock::time_point t0 = chrono::high_resolution_clock::now();
#ifdef DDS_BATCH_FALLBACKS
    {
      vector<dealPBN> fbDeals(static_cast<size_t>(nDeals));
      for (int i = 0; i < nDeals; i++)
        fbDeals[static_cast<size_t>(i)] = deals[static_cast<unsigned>(i)].deal;
      vector<ddTableResults> fbResults;
      if (SolveCpuExactBatched(fbDeals, nDeals, fbResults, "exact"))
      {
        for (int i = 0; i < nDeals; i++)
        {
          tr.tables[static_cast<unsigned>(i)] = fbResults[static_cast<unsigned>(i)];
          tr.cpuFallbackDeals++;
          for (int j = 0; j < 20; j++)
            tr.confidence[static_cast<unsigned>(i)][j] = 1.0;
        }
      }
      PrintProgress("exact", nDeals, nDeals, t0);
    }
#else
    for (int i = 0; i < nDeals; i++)
    {
      const int ret = SolveCpuExact(deals[static_cast<unsigned>(i)].deal,
        tr.tables[static_cast<unsigned>(i)]);
      if (ret != RETURN_NO_FAULT)
      {
        cout << "Exact solve failed for deal " << i << " ret " << ret << "\n";
        break;
      }
      tr.cpuFallbackDeals++;
      for (int j = 0; j < 20; j++)
        tr.confidence[static_cast<unsigned>(i)][j] = 1.0;
      if (ShouldPrintProgress(i + 1, nDeals))
        PrintProgress("exact", i + 1, nDeals, t0);
    }
#endif
    const chrono::high_resolution_clock::time_point t1 = chrono::high_resolution_clock::now();
    tr.elapsedMs = chrono::duration_cast<chrono::duration<double, milli> >(t1-t0).count();
    tr.runtimeTag = RuntimeTagExact(tr.cpuFallbackDeals);
    cout << "runtime-tag[" << tr.name << "]: " << tr.runtimeTag << "\n";
    return tr;
  };

  if (options.backend == DTEST_BACKEND_COMPARE)
  {
    if (options.compareTarget == DTEST_COMPARE_FAST)
      targets.push_back(runFast());
    else if (options.compareTarget == DTEST_COMPARE_HYBRID)
      targets.push_back(runHybrid());
    else if (options.compareTarget == DTEST_COMPARE_EXACT)
      targets.push_back(runExact());
    else
    {
      targets.push_back(runFast());
      targets.push_back(runHybrid());
      targets.push_back(runExact());
    }
  }
  else
  {
    cout << "Backend is CPU only; exact tables generated.\n";
    return true;
  }

  const double MAX_OFF1 = 0.03;
  const double MAX_OFF2 = 0.001;
  bool allPass = true;

  for (unsigned t = 0; t < targets.size(); t++)
  {
    const TargetResult& tr = targets[t];
    cout << "Comparing target '" << tr.name << "' against CPU baseline...\n";
    int mismatchCount = 0;
    int off1 = 0;
    int off2 = 0;
    long long absSum = 0;

    int contractDisagree = 0;
    int parDisagree = 0;

    map<int, int> deltaHist;
    map<int, int> absHist;
    int confusion[14][14];
    for (int i = 0; i < 14; i++)
      for (int j = 0; j < 14; j++)
        confusion[i][j] = 0;

    double calibrationCount[10];
    double calibrationAcc[10];
    double calibrationConf[10];
    for (int i = 0; i < 10; i++)
    {
      calibrationCount[i] = 0.0;
      calibrationAcc[i] = 0.0;
      calibrationConf[i] = 0.0;
    }

    vector<double> deltaAll;
    deltaAll.reserve(static_cast<unsigned>(cells));

    const string mismatchPath = options.reportDir + "/dds_compare_mismatches_" + tr.name + ".csv";
    ofstream mismatchFile(mismatchPath.c_str());
    mismatchFile << "deal_idx,combo_idx,cpu_value,candidate_value,delta\n";

    const string slicesPath = options.reportDir + "/dds_compare_slices_" + tr.name + ".csv";
    ofstream slicesFile(slicesPath.c_str());
    slicesFile << "combo_idx,strain,declarer,exact_rate,mae,off_by_one_rate,off_by_two_plus_rate\n";

    int comboExact[20];
    int comboAbs[20];
    int comboOff1[20];
    int comboOff2[20];
    int comboCnt[20];
    for (int i = 0; i < 20; i++)
    {
      comboExact[i] = 0;
      comboAbs[i] = 0;
      comboOff1[i] = 0;
      comboOff2[i] = 0;
      comboCnt[i] = 0;
    }

    const chrono::high_resolution_clock::time_point cmpT0 =
      chrono::high_resolution_clock::now();
    for (int i = 0; i < nDeals; i++)
    {
      const ddTableResults& cput = cpuExact[static_cast<unsigned>(i)];
      const ddTableResults& cand = tr.tables[static_cast<unsigned>(i)];
      if (ContractValue(cput) != ContractValue(cand))
        contractDisagree++;
      if (ParProxy(cput) != ParProxy(cand))
        parDisagree++;

      for (int strain = 0; strain < DDS_STRAINS; strain++)
      {
        for (int declarer = 0; declarer < DDS_HANDS; declarer++)
        {
          const int idx = 4 * strain + declarer;
          const int cpuv = cput.resTable[strain][declarer];
          const int candv = cand.resTable[strain][declarer];
          const int d = candv - cpuv;
          const int ad = abs(d);

          deltaAll.push_back(static_cast<double>(d));
          deltaHist[d]++;
          absHist[ad]++;
          const int cpuClip = clamp_int(cpuv, 0, 13);
          const int candClip = clamp_int(candv, 0, 13);
          confusion[cpuClip][candClip]++;

          comboCnt[idx]++;
          comboAbs[idx] += ad;
          if (d == 0)
            comboExact[idx]++;
          else
          {
            mismatchCount++;
            mismatchFile << i << "," << idx << "," << cpuv << "," << candv <<
              "," << d << "\n";
          }
          if (ad == 1)
          {
            off1++;
            comboOff1[idx]++;
          }
          else if (ad >= 2)
          {
            off2++;
            comboOff2[idx]++;
          }
          absSum += ad;

          const double conf = tr.confidence[static_cast<unsigned>(i)][idx];
          int bin = static_cast<int>(floor(conf * 10.0));
          if (bin < 0)
            bin = 0;
          if (bin > 9)
            bin = 9;
          calibrationCount[bin] += 1.0;
          calibrationAcc[bin] += (d == 0 ? 1.0 : 0.0);
          calibrationConf[bin] += conf;
        }
      }
      if (ShouldPrintProgress(i + 1, nDeals))
        PrintProgress("compare-" + tr.name, i + 1, nDeals, cmpT0);
    }

    mismatchFile.close();

    for (int idx = 0; idx < 20; idx++)
    {
      const double cnt = static_cast<double>(comboCnt[idx]);
      const double exactRate = static_cast<double>(comboExact[idx]) / cnt;
      const double mae = static_cast<double>(comboAbs[idx]) / cnt;
      const double ob1 = static_cast<double>(comboOff1[idx]) / cnt;
      const double ob2 = static_cast<double>(comboOff2[idx]) / cnt;
      slicesFile << idx << "," << idx/4 << "," << idx%4 << ","
        << exactRate << "," << mae << "," << ob1 << "," << ob2 << "\n";
    }
    slicesFile.close();

    sort(deltaAll.begin(), deltaAll.end());
    const int len = static_cast<int>(deltaAll.size());
    const double exactRate = static_cast<double>(cells - mismatchCount) /
      static_cast<double>(cells);
    const double mae = static_cast<double>(absSum) / static_cast<double>(cells);
    const double off1Rate = static_cast<double>(off1) / static_cast<double>(cells);
    const double off2Rate = static_cast<double>(off2) / static_cast<double>(cells);
    const double contractRate = static_cast<double>(contractDisagree) /
      static_cast<double>(nDeals);
    const double parRate = static_cast<double>(parDisagree) /
      static_cast<double>(nDeals);
    const double secs = tr.elapsedMs / 1000.0;
    const double tablesPerSec = (secs > 0.0 ? static_cast<double>(nDeals) / secs : 0.0);
    const double solutionsPerSec = (secs > 0.0 ? static_cast<double>(cells) / secs : 0.0);

    vector<string> failures;
    if (tr.name == "exact")
    {
      if (mismatchCount > 0)
      {
        allPass = false;
        failures.push_back("Exact mode mismatch count > 0");
      }
    }
    else
    {
      if (off1Rate > MAX_OFF1)
      {
        allPass = false;
        failures.push_back("off_by_one_rate exceeds threshold");
      }
      if (off2Rate > MAX_OFF2)
      {
        allPass = false;
        failures.push_back("off_by_two_plus_rate exceeds threshold");
      }
    }

    {
      const string summaryPath = options.reportDir + "/dds_compare_summary_" + tr.name + ".json";
      WriteSummaryJson(summaryPath, tr.name, nDeals, cells, exactRate, mae, off1Rate,
        off2Rate, contractRate, parRate, tr.elapsedMs, tablesPerSec, solutionsPerSec,
        tr.runtimeTag, failures);
    }

    {
      const string histPath = options.reportDir + "/dds_compare_histograms_" + tr.name + ".json";
      ofstream f(histPath.c_str());
      f << "{\n  \"delta_histogram\": {\n";
      unsigned h = 0;
      for (map<int, int>::const_iterator it = deltaHist.begin();
          it != deltaHist.end(); ++it, ++h)
      {
        f << "    \"" << it->first << "\": " << it->second;
        if (h + 1U != deltaHist.size())
          f << ",";
        f << "\n";
      }
      f << "  },\n  \"abs_delta_histogram\": {\n";
      h = 0;
      for (map<int, int>::const_iterator it = absHist.begin();
          it != absHist.end(); ++it, ++h)
      {
        f << "    \"" << it->first << "\": " << it->second;
        if (h + 1U != absHist.size())
          f << ",";
        f << "\n";
      }
      f << "  }\n}\n";
    }

    if (tr.name != "exact")
    {
      const string statsPath = options.reportDir + "/dds_compare_stats_" + tr.name + ".json";
      ofstream f(statsPath.c_str());
      double sum = 0.0;
      double sq = 0.0;
      for (int i = 0; i < len; i++)
      {
        sum += deltaAll[static_cast<unsigned>(i)];
        sq += deltaAll[static_cast<unsigned>(i)] * deltaAll[static_cast<unsigned>(i)];
      }
      const double mean = sum / static_cast<double>(len);
      const double variance = max(0.0, sq / static_cast<double>(len) - mean * mean);
      const double stddev = sqrt(variance);

      auto pct = [&](const double p) -> double
      {
        int idx = static_cast<int>(floor(p * static_cast<double>(len - 1)));
        idx = clamp_int(idx, 0, len-1);
        return deltaAll[static_cast<unsigned>(idx)];
      };
      f << "{\n";
      f << "  \"mean_error\": " << mean << ",\n";
      f << "  \"median_error\": " << pct(0.50) << ",\n";
      f << "  \"std_error\": " << stddev << ",\n";
      f << "  \"min_error\": " << deltaAll.front() << ",\n";
      f << "  \"max_error\": " << deltaAll.back() << ",\n";
      f << "  \"percentiles\": {\n";
      f << "    \"p5\": " << pct(0.05) << ",\n";
      f << "    \"p25\": " << pct(0.25) << ",\n";
      f << "    \"p50\": " << pct(0.50) << ",\n";
      f << "    \"p75\": " << pct(0.75) << ",\n";
      f << "    \"p95\": " << pct(0.95) << ",\n";
      f << "    \"p99\": " << pct(0.99) << "\n";
      f << "  },\n";
      f << "  \"mae\": " << mae << ",\n";
      f << "  \"rmse\": " << sqrt(sq / static_cast<double>(len)) << ",\n";
      f << "  \"tail_rates\": {\n";
      f << "    \"|delta|>=1\": " << off1Rate + off2Rate << ",\n";
      f << "    \"|delta|>=2\": " << off2Rate << "\n";
      f << "  }\n";
      f << "}\n";

      const string cmPath = options.reportDir + "/dds_compare_confusion_matrix_" + tr.name + ".csv";
      ofstream cm(cmPath.c_str());
      for (int i = 0; i < 14; i++)
      {
        for (int j = 0; j < 14; j++)
        {
          cm << confusion[i][j];
          if (j != 13)
            cm << ",";
        }
        cm << "\n";
      }

      const string calPath = options.reportDir + "/dds_compare_calibration_" + tr.name + ".json";
      ofstream cal(calPath.c_str());
      double ece = 0.0;
      cal << "{\n  \"reliability_table\": [\n";
      bool first = true;
      for (int b = 0; b < 10; b++)
      {
        if (calibrationCount[b] <= 0.0)
          continue;
        const double acc = calibrationAcc[b] / calibrationCount[b];
        const double avgConf = calibrationConf[b] / calibrationCount[b];
        ece += fabs(acc - avgConf) * (calibrationCount[b] / static_cast<double>(cells));
        if (! first)
          cal << ",\n";
        first = false;
        cal << "    {\"bin\": " << b <<
          ", \"count\": " << static_cast<long long>(calibrationCount[b]) <<
          ", \"accuracy\": " << acc <<
          ", \"avg_confidence\": " << avgConf << "}";
      }
      cal << "\n  ],\n";
      cal << "  \"ece\": " << ece << "\n";
      cal << "}\n";
    }

    cout << tr.name << ": exact=" << fixed << setprecision(6) << exactRate
      << " mae=" << mae << " off1=" << off1Rate << " off2=" << off2Rate
      << " runtime_ms=" << tr.elapsedMs
      << " tables_per_sec=" << tablesPerSec
      << " solutions_per_sec=" << solutionsPerSec << "\n";
  }

  return allPass;
}


namespace
{
  struct PbnDealEntry
  {
    dealPBN deal;
    bool hasReferenceTable;
    ddTableResults referenceTable;
    string boardLabel;
    string dealText;
  };

  bool IsUrl(
    const string& source)
  {
    return source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0;
  }

  string Trim(
    const string& s)
  {
    size_t a = 0;
    while (a < s.size() && isspace(static_cast<unsigned char>(s[a])))
      a++;
    size_t b = s.size();
    while (b > a && isspace(static_cast<unsigned char>(s[b-1])))
      b--;
    return s.substr(a, b-a);
  }

  bool ExtractQuoted(
    const string& s,
    string& out)
  {
    const size_t q1 = s.find('\"');
    if (q1 == string::npos)
      return false;
    const size_t q2 = s.find_last_of('\"');
    if (q2 == string::npos || q2 <= q1)
      return false;
    out = s.substr(q1 + 1, q2 - q1 - 1);
    return true;
  }

  bool Parse20Ints(
    const string& s,
    ddTableResults& table)
  {
    string cleaned = s;
    for (size_t i = 0; i < cleaned.size(); i++)
    {
      const char c = cleaned[i];
      if (! (isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || isspace(static_cast<unsigned char>(c))))
        cleaned[i] = ' ';
    }

    vector<int> vals;
    vals.reserve(20U);
    istringstream iss(cleaned);
    int x;
    while (iss >> x)
      vals.push_back(x);

    if (vals.size() < 20U)
      return false;

    for (int strain = 0; strain < DDS_STRAINS; strain++)
      for (int declarer = 0; declarer < DDS_HANDS; declarer++)
        table.resTable[strain][declarer] = vals[static_cast<size_t>(4 * strain + declarer)];

    return true;
  }

  vector<string> SplitWs(
    const string& s)
  {
    istringstream iss(s);
    vector<string> out;
    string tok;
    while (iss >> tok)
      out.push_back(tok);
    return out;
  }

  int SeatIndex(
    const char c);

  int StrainIndexFromToken(
    const string& tok)
  {
    if (tok == "S" || tok == "s")
      return 0;
    if (tok == "H" || tok == "h")
      return 1;
    if (tok == "D" || tok == "d")
      return 2;
    if (tok == "C" || tok == "c")
      return 3;
    if (tok == "NT" || tok == "Nt" || tok == "nT" || tok == "nt" || tok == "N")
      return 4;
    return -1;
  }

  bool ParseOptimumResultRow(
    const string& line,
    int& hand,
    int& strain,
    int& tricks)
  {
    const vector<string> t = SplitWs(line);
    if (t.size() < 3U)
      return false;

    hand = SeatIndex(t[0][0]);
    strain = StrainIndexFromToken(t[1]);
    if (hand < 0 || strain < 0)
      return false;

    char * endp = nullptr;
    const long v = strtol(t[2].c_str(), &endp, 10);
    if (endp == nullptr || *endp != '\0')
      return false;
    tricks = static_cast<int>(v);
    return true;
  }

  bool ParseDealFromCards(
    const string& cards,
    dealPBN& deal)
  {
    memset(&deal, 0, sizeof(deal));
    deal.trump = 0;
    deal.first = 0;
    strncpy(deal.remainCards, cards.c_str(), sizeof(deal.remainCards)-1);
    deal.remainCards[sizeof(deal.remainCards)-1] = '\0';
    return (strlen(deal.remainCards) > 2);
  }

  int SeatIndex(
    const char c)
  {
    if (c == 'N' || c == 'n') return 0;
    if (c == 'E' || c == 'e') return 1;
    if (c == 'S' || c == 's') return 2;
    if (c == 'W' || c == 'w') return 3;
    return -1;
  }

  int RankFromChar(
    const char c)
  {
    if (c == 'A' || c == 'a') return 14;
    if (c == 'K' || c == 'k') return 13;
    if (c == 'Q' || c == 'q') return 12;
    if (c == 'J' || c == 'j') return 11;
    if (c == 'T' || c == 't') return 10;
    if (c >= '2' && c <= '9') return static_cast<int>(c - '0');
    return -1;
  }

  bool FillFeaturesFromDeal(
    const dealPBN& deal,
    RandomDeal& rd)
  {
    rd.deal = deal;
    for (int h = 0; h < DDS_HANDS; h++)
    {
      rd.hcp[h] = 0;
      for (int s = 0; s < DDS_SUITS; s++)
        rd.suitLen[h][s] = 0;
    }

    const string cards = deal.remainCards;
    const size_t colon = cards.find(':');
    if (colon == string::npos || colon == 0)
      return false;

    const int dealer = SeatIndex(cards[0]);
    if (dealer < 0)
      return false;

    const string rest = cards.substr(colon + 1);
    istringstream hs(rest);
    vector<string> hands;
    string hand;
    while (hs >> hand)
      hands.push_back(hand);
    if (hands.size() != 4U)
      return false;

    for (int rel = 0; rel < 4; rel++)
    {
      const int seat = (dealer + rel) % 4;
      const string& htxt = hands[static_cast<size_t>(rel)];
      vector<string> suits;
      string part = "";
      for (size_t i = 0; i < htxt.size(); i++)
      {
        if (htxt[i] == '.')
        {
          suits.push_back(part);
          part = "";
        }
        else
          part += htxt[i];
      }
      suits.push_back(part);
      if (suits.size() != 4U)
        return false;

      for (int s = 0; s < 4; s++)
      {
        const string& ranks = suits[static_cast<size_t>(s)];
        if (ranks == "-" || ranks == "")
          continue;
        for (size_t k = 0; k < ranks.size(); k++)
        {
          const int r = RankFromChar(ranks[k]);
          if (r < 2)
            continue;
          rd.suitLen[seat][s]++;
          rd.hcp[seat] += HcpFromRank(r);
        }
      }
    }

    return true;
  }

  bool LoadTextFromSource(
    const string& source,
    string& textOut)
  {
    if (! IsUrl(source))
    {
      ifstream f(source.c_str(), ios::in | ios::binary);
      if (! f)
        return false;
      ostringstream oss;
      oss << f.rdbuf();
      textOut = oss.str();
      return true;
    }

#ifdef _WIN32
    string safeUrl = source;
    for (size_t i = 0; i < safeUrl.size(); i++)
      if (safeUrl[i] == '\'')
        safeUrl.insert(i++, "'");
    const string psExpr =
      "\"$ProgressPreference='SilentlyContinue'; "
      "(Invoke-WebRequest -UseBasicParsing -Uri '" + safeUrl + "').Content\"";

    auto RunReadCmd = [&](const string& cmd) -> bool
    {
      FILE * fp = _popen(cmd.c_str(), "r");
      if (! fp)
        return false;
      char buf[4096];
      textOut = "";
      while (fgets(buf, sizeof(buf), fp))
        textOut += buf;
      const int rc = _pclose(fp);
      return rc == 0 && textOut != "";
    };

    char psPathBuf[MAX_PATH];
    const DWORD psPathLen = SearchPathA(nullptr, "powershell.exe", nullptr,
      MAX_PATH, psPathBuf, nullptr);
    if (psPathLen > 0 && psPathLen < MAX_PATH)
    {
      if (RunReadCmd("powershell -NoProfile -Command " + psExpr))
        return true;
    }

    const char * winDir = getenv("WINDIR");
    if (winDir != nullptr && winDir[0] != '\0')
    {
      const string psExe = string(winDir) + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
      if (RunReadCmd(psExe + " -NoProfile -Command " + psExpr))
        return true;
    }

    if (RunReadCmd("curl.exe -fsSL \"" + source + "\" 2>nul"))
      return true;

    return false;
#else
    string safeUrl = source;
    for (size_t i = 0; i < safeUrl.size(); i++)
    {
      if (safeUrl[i] == '\'')
      {
        safeUrl.replace(i, 1, "'\"'\"'");
        i += 4;
      }
    }

    const string curlCmd = "curl -fsSL '" + safeUrl + "' 2>/dev/null";
    FILE * fp = popen(curlCmd.c_str(), "r");
    if (! fp)
      return false;
    char buf[4096];
    textOut = "";
    while (fgets(buf, sizeof(buf), fp))
      textOut += buf;
    int rc = pclose(fp);
    if (rc == 0 && textOut != "")
      return true;

    const string wgetCmd = "wget -qO- '" + safeUrl + "' 2>/dev/null";
    fp = popen(wgetCmd.c_str(), "r");
    if (! fp)
      return false;
    textOut = "";
    while (fgets(buf, sizeof(buf), fp))
      textOut += buf;
    rc = pclose(fp);
    return rc == 0 && textOut != "";
#endif
  }

  bool ParsePbnSourceText(
    const string& text,
    vector<PbnDealEntry>& entries)
  {
    entries.clear();
    istringstream in(text);
    string line;
    string currentBoard = "";
    bool inOptimumTable = false;
    ddTableResults optTable;
    bool optSeen[5][4];
    int optRows = 0;
    memset(&optTable, 0, sizeof(optTable));
    memset(optSeen, 0, sizeof(optSeen));

    auto finalizeOptimum = [&]()
    {
      if (! inOptimumTable)
        return;
      bool complete = true;
      for (int s = 0; s < DDS_STRAINS; s++)
      {
        for (int h = 0; h < DDS_HANDS; h++)
        {
          if (! optSeen[s][h])
            complete = false;
        }
      }
      if (complete && entries.size() > 0)
      {
        entries.back().referenceTable = optTable;
        entries.back().hasReferenceTable = true;
      }
      inOptimumTable = false;
      optRows = 0;
      memset(&optTable, 0, sizeof(optTable));
      memset(optSeen, 0, sizeof(optSeen));
    };

    while (getline(in, line))
    {
      const string t = Trim(line);
      if (t == "")
      {
        finalizeOptimum();
        continue;
      }

      if (inOptimumTable)
      {
        if (t[0] == '[' || t.rfind("PBN ", 0) == 0 || t.rfind("TABLE ", 0) == 0)
          finalizeOptimum();
        else
        {
          int hand = -1, strain = -1, tricks = -1;
          if (ParseOptimumResultRow(t, hand, strain, tricks))
          {
            optTable.resTable[strain][hand] = tricks;
            if (! optSeen[strain][hand])
            {
              optSeen[strain][hand] = true;
              optRows++;
            }
            if (optRows == 20)
              finalizeOptimum();
          }
          continue;
        }
      }

      string quoted;
      if (t.rfind("[Board ", 0) == 0)
      {
        if (ExtractQuoted(t, quoted))
          currentBoard = quoted;
        continue;
      }

      if (t.rfind("PBN ", 0) == 0)
      {
        if (! ExtractQuoted(t, quoted))
          continue;
        PbnDealEntry e;
        memset(&e, 0, sizeof(e));
        e.hasReferenceTable = false;
        e.boardLabel = currentBoard;
        e.dealText = quoted;
        if (ParseDealFromCards(quoted, e.deal))
          entries.push_back(e);
      }
      else if (t.rfind("TABLE ", 0) == 0)
      {
        if (entries.size() == 0)
          continue;
        ddTableResults tab;
        if (Parse20Ints(t.substr(6), tab))
        {
          entries.back().referenceTable = tab;
          entries.back().hasReferenceTable = true;
        }
      }
      else if (t.rfind("[Deal ", 0) == 0 || t.rfind("[Deal\t", 0) == 0)
      {
        if (! ExtractQuoted(t, quoted))
          continue;
        PbnDealEntry e;
        memset(&e, 0, sizeof(e));
        e.hasReferenceTable = false;
        e.boardLabel = currentBoard;
        e.dealText = quoted;
        if (ParseDealFromCards(quoted, e.deal))
          entries.push_back(e);
      }
      else if (t.find("[DoubleDummy") == 0 || t.find("[OptimumResultTable") == 0 ||
          t.find("[DDTable") == 0)
      {
        if (entries.size() == 0)
          continue;

        ddTableResults tab;
        string parseText = t;
        if (ExtractQuoted(t, quoted))
          parseText = quoted;
        if (Parse20Ints(parseText, tab))
        {
          entries.back().referenceTable = tab;
          entries.back().hasReferenceTable = true;
          continue;
        }

        inOptimumTable = true;
        optRows = 0;
        memset(&optTable, 0, sizeof(optTable));
        memset(optSeen, 0, sizeof(optSeen));
      }
    }

    finalizeOptimum();

    return entries.size() > 0;
  }

  string HtmlEscape(
    const string& s)
  {
    string out;
    out.reserve(s.size() + 16U);
    for (size_t i = 0; i < s.size(); i++)
    {
      const char c = s[i];
      if (c == '&') out += "&amp;";
      else if (c == '<') out += "&lt;";
      else if (c == '>') out += "&gt;";
      else if (c == '\"') out += "&quot;";
      else out += c;
    }
    return out;
  }

  string TableCompact(
    const ddTableResults& t)
  {
    ostringstream oss;
    for (int strain = 0; strain < DDS_STRAINS; strain++)
    {
      for (int hand = 0; hand < DDS_HANDS; hand++)
      {
        if (strain != 0 || hand != 0)
          oss << " ";
        oss << t.resTable[strain][hand];
      }
    }
    return oss.str();
  }

  bool LaunchHtmlReport(
    const string& path)
  {
#ifdef _WIN32
    char fullPath[MAX_PATH];
    const DWORD n = GetFullPathNameA(path.c_str(), MAX_PATH, fullPath, nullptr);
    const char * target = ((n > 0 && n < MAX_PATH) ? fullPath : path.c_str());
    const HINSTANCE rc = ShellExecuteA(nullptr, "open", target, nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(rc) > 32;
#else
    string safePath = path;
    for (size_t i = 0; i < safePath.size(); i++)
    {
      if (safePath[i] == '\'')
      {
        safePath.replace(i, 1, "'\"'\"'");
        i += 4;
      }
    }
#if defined(__APPLE__)
    const string cmd = "open '" + safePath + "' >/dev/null 2>&1 &";
#else
    const string cmd = "xdg-open '" + safePath + "' >/dev/null 2>&1 &";
#endif
    return system(cmd.c_str()) == 0;
#endif
  }
}


bool RunPbnEvaluation(
  const OptionsType& options)
{
  if (options.pbnSource == "")
    return false;

  if (! MakeDirIfNeeded(options.reportDir))
  {
    cout << "Could not create report directory '" << options.reportDir << "'\n";
    return false;
  }

  string text;
  if (! LoadTextFromSource(options.pbnSource, text))
  {
    cout << "Could not read PBN source '" << options.pbnSource << "'\n";
    return false;
  }

  vector<PbnDealEntry> entries;
  if (! ParsePbnSourceText(text, entries))
  {
    cout << "No deals parsed from PBN source\n";
    return false;
  }

  vector<RandomDeal> deals;
  deals.reserve(entries.size());
  vector<int> referenceIdx;
  vector<ddTableResults> referenceTables;
  referenceTables.reserve(entries.size());
  vector<string> boardLabels;
  vector<string> dealTexts;
  boardLabels.reserve(entries.size());
  dealTexts.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); i++)
  {
    RandomDeal rd;
    if (! FillFeaturesFromDeal(entries[i].deal, rd))
      continue;
    deals.push_back(rd);
    boardLabels.push_back(entries[i].boardLabel);
    dealTexts.push_back(entries[i].dealText);
    ddTableResults dummy;
    memset(&dummy, 0, sizeof(dummy));
    referenceTables.push_back(dummy);
    if (entries[i].hasReferenceTable)
    {
      referenceIdx.push_back(static_cast<int>(deals.size() - 1));
      referenceTables.back() = entries[i].referenceTable;
    }
  }

  const int nDeals = static_cast<int>(deals.size());
  if (nDeals == 0)
  {
    cout << "Parsed deals had invalid structure\n";
    return false;
  }

  cout << "Loaded " << nDeals << " deals from PBN source";
  cout << " (" << referenceIdx.size() << " with embedded DD tables)\n";

  vector<dealPBN> pbnDeals(static_cast<unsigned>(nDeals));
  for (int i = 0; i < nDeals; i++)
    pbnDeals[static_cast<unsigned>(i)] = deals[static_cast<unsigned>(i)].deal;

  vector<ddTableResults> exact;
  if (!SolveCpuExactBatched(pbnDeals, nDeals, exact, "pbn-exact"))
    return false;

  auto runTarget = [&](const string& name,
      vector<ddTableResults>& outTables,
      vector<array<double, 20> >& outConf,
      double& elapsedMs,
      int& cpuFallbackDeals,
      string& runtimeTag) -> bool
  {
    cpuFallbackDeals = 0;
    outTables.resize(static_cast<unsigned>(nDeals));
    outConf.resize(static_cast<unsigned>(nDeals));
    const chrono::high_resolution_clock::time_point t0 = chrono::high_resolution_clock::now();
    int fallbackDeals = 0;
    int fallbackCells = 0;

    if (name == "exact")
    {
#ifdef DDS_BATCH_FALLBACKS
      vector<dealPBN> fbDeals(static_cast<size_t>(nDeals));
      for (int i = 0; i < nDeals; i++)
        fbDeals[static_cast<size_t>(i)] = deals[static_cast<unsigned>(i)].deal;
      vector<ddTableResults> fbResults;
      if (!SolveCpuExactBatched(fbDeals, nDeals, fbResults, "pbn-exact"))
        return false;
      for (int i = 0; i < nDeals; i++)
      {
        outTables[static_cast<unsigned>(i)] = fbResults[static_cast<unsigned>(i)];
        fallbackDeals++;
        cpuFallbackDeals++;
        for (int j = 0; j < 20; j++)
          outConf[static_cast<unsigned>(i)][j] = 1.0;
      }
      PrintProgress("pbn-" + name, nDeals, nDeals, t0);
#else
      for (int i = 0; i < nDeals; i++)
      {
        const int ret = SolveCpuExact(deals[static_cast<unsigned>(i)].deal,
          outTables[static_cast<unsigned>(i)]);
        if (ret != RETURN_NO_FAULT)
          return false;
        fallbackDeals++;
        cpuFallbackDeals++;
        for (int j = 0; j < 20; j++)
          outConf[static_cast<unsigned>(i)][j] = 1.0;
        if (ShouldPrintProgress(i + 1, nDeals))
          PrintProgress("pbn-" + name, i + 1, nDeals, t0);
      }
#endif
    }
    else
    {
      for (int i = 0; i < nDeals; i++)
      {
        if (name == "fast")
        {
          SolveFastHeuristic(deals[static_cast<unsigned>(i)], outTables[static_cast<unsigned>(i)],
            outConf[static_cast<unsigned>(i)]);
        }
        else if (name == "hybrid")
        {
          bool usedFallback = false;
          SolveHybrid(deals[static_cast<unsigned>(i)], options.confidenceThreshold,
            outTables[static_cast<unsigned>(i)], outConf[static_cast<unsigned>(i)],
            fallbackCells, usedFallback);
          if (usedFallback)
          {
            fallbackDeals++;
            cpuFallbackDeals++;
          }
        }

        if (ShouldPrintProgress(i + 1, nDeals))
          PrintProgress("pbn-" + name, i + 1, nDeals, t0);
      }
    }

    const chrono::high_resolution_clock::time_point t1 = chrono::high_resolution_clock::now();
    elapsedMs = chrono::duration_cast<chrono::duration<double, milli> >(t1-t0).count();
    if (name == "hybrid")
      cout << "PBN hybrid fallback on " << fallbackDeals << "/" << nDeals <<
        " deals (" << fallbackCells << " cells)\n";
    if (name == "exact")
      cout << "PBN exact on " << fallbackDeals << "/" << nDeals << " deals\n";
    if (name == "fast")
      runtimeTag = RuntimeTagFast(nDeals);
    else if (name == "hybrid")
      runtimeTag = RuntimeTagHybrid(fallbackDeals, nDeals);
    else
      runtimeTag = RuntimeTagExact(cpuFallbackDeals);
    cout << "runtime-tag[pbn-" << name << "]: " << runtimeTag << "\n";
    return true;
  };

  vector<string> targets;
  if (options.backend == DTEST_BACKEND_COMPARE || options.backend == DTEST_BACKEND_CPU)
  {
    if (options.compareTarget == DTEST_COMPARE_FAST)
      targets.push_back("fast");
    else if (options.compareTarget == DTEST_COMPARE_HYBRID)
      targets.push_back("hybrid");
    else if (options.compareTarget == DTEST_COMPARE_EXACT)
      targets.push_back("exact");
    else
    {
      targets.push_back("fast");
      targets.push_back("hybrid");
      targets.push_back("exact");
    }
  }

  const string csvPath = options.reportDir + "/pbn_combined_summary.csv";
  ofstream summary(csvPath.c_str());
  summary << "target,num_deals,num_cells,exact_match_vs_cpu,mae_vs_cpu,off1_vs_cpu,off2_vs_cpu,"
    "num_deals_with_pbn_table,exact_match_vs_pbn,mae_vs_pbn,off1_vs_pbn,off2_vs_pbn,elapsed_ms,"
    "dd_tables_per_sec,dd_solutions_per_sec,runtime_tag\n";

  struct ModeCapture
  {
    string name;
    vector<ddTableResults> tables;
    double elapsedMs;
    double tablesPerSec;
    double solutionsPerSec;
    string executionPath;
    int cpuFallbackDeals;
    string runtimeTag;
  };
  vector<ModeCapture> modeCaptures;

  for (size_t ti = 0; ti < targets.size(); ti++)
  {
    vector<ddTableResults> pred;
    vector<array<double, 20> > conf;
    double elapsedMs = 0.0;
    int cpuFallbackDeals = 0;
    string runtimeTag;
    if (! runTarget(targets[ti], pred, conf, elapsedMs, cpuFallbackDeals, runtimeTag))
    {
      cout << "Target run failed: " << targets[ti] << "\n";
      return false;
    }

    int exactMatches = 0;
    int off1 = 0;
    int off2 = 0;
    long long absSum = 0;

    int pExactMatches = 0;
    int pOff1 = 0;
    int pOff2 = 0;
    long long pAbsSum = 0;

    const int numCells = nDeals * 20;
    const int pCells = static_cast<int>(referenceIdx.size()) * 20;
    const string mmPath = options.reportDir + "/pbn_mismatches_" + targets[ti] + ".csv";
    ofstream mm(mmPath.c_str());
    mm << "deal_idx,source,combo_idx,expected,predicted,delta\n";

    for (int i = 0; i < nDeals; i++)
    {
      for (int strain = 0; strain < DDS_STRAINS; strain++)
      {
        for (int declarer = 0; declarer < DDS_HANDS; declarer++)
        {
          const int idx = 4 * strain + declarer;
          const int a = exact[static_cast<unsigned>(i)].resTable[strain][declarer];
          const int b = pred[static_cast<unsigned>(i)].resTable[strain][declarer];
          const int d = b - a;
          const int ad = abs(d);
          if (d == 0) exactMatches++;
          if (ad == 1) off1++;
          if (ad >= 2) off2++;
          absSum += ad;
        }
      }
    }

    for (size_t r = 0; r < referenceIdx.size(); r++)
    {
      const int i = referenceIdx[r];
      const ddTableResults& exp = referenceTables[static_cast<size_t>(i)];
      const ddTableResults& got = pred[static_cast<size_t>(i)];
      for (int strain = 0; strain < DDS_STRAINS; strain++)
      {
        for (int declarer = 0; declarer < DDS_HANDS; declarer++)
        {
          const int idx = 4 * strain + declarer;
          const int a = exp.resTable[strain][declarer];
          const int b = got.resTable[strain][declarer];
          const int d = b - a;
          const int ad = abs(d);
          if (d == 0) pExactMatches++;
          else
            mm << i << ",pbn_table," << idx << "," << a << "," << b << "," << d << "\n";
          if (ad == 1) pOff1++;
          if (ad >= 2) pOff2++;
          pAbsSum += ad;
        }
      }
    }
    mm.close();

    const double exactRate = static_cast<double>(exactMatches) / static_cast<double>(numCells);
    const double mae = static_cast<double>(absSum) / static_cast<double>(numCells);
    const double off1Rate = static_cast<double>(off1) / static_cast<double>(numCells);
    const double off2Rate = static_cast<double>(off2) / static_cast<double>(numCells);

    double pExactRate = -1.0;
    double pMae = -1.0;
    double pOff1Rate = -1.0;
    double pOff2Rate = -1.0;
    if (pCells > 0)
    {
      pExactRate = static_cast<double>(pExactMatches) / static_cast<double>(pCells);
      pMae = static_cast<double>(pAbsSum) / static_cast<double>(pCells);
      pOff1Rate = static_cast<double>(pOff1) / static_cast<double>(pCells);
      pOff2Rate = static_cast<double>(pOff2) / static_cast<double>(pCells);
    }

    const double secs = elapsedMs / 1000.0;
    const double tablesPerSec = (secs > 0.0 ? static_cast<double>(nDeals) / secs : 0.0);
    const double solutionsPerSec = (secs > 0.0 ? static_cast<double>(numCells) / secs : 0.0);

    cout << "PBN " << targets[ti]
      << ": cpu_exact_match=" << fixed << setprecision(6) << exactRate
      << " cpu_mae=" << mae;
    if (pCells > 0)
      cout << " pbn_exact_match=" << pExactRate << " pbn_mae=" << pMae;
    cout << " runtime_ms=" << elapsedMs
      << " tables_per_sec=" << tablesPerSec
      << " solutions_per_sec=" << solutionsPerSec << "\n";

    summary << targets[ti] << ","
      << nDeals << "," << numCells << ","
      << exactRate << "," << mae << "," << off1Rate << "," << off2Rate << ","
      << referenceIdx.size() << ",";
    if (pCells > 0)
      summary << pExactRate << "," << pMae << "," << pOff1Rate << "," << pOff2Rate;
    else
      summary << ",,,,";
    summary << "," << elapsedMs << "," << tablesPerSec << "," << solutionsPerSec
      << ",\"" << runtimeTag << "\"\n";

    ModeCapture mc;
    mc.name = targets[ti];
    mc.tables = pred;
    mc.elapsedMs = elapsedMs;
    mc.tablesPerSec = tablesPerSec;
    mc.solutionsPerSec = solutionsPerSec;
    mc.executionPath = "CPU";
    mc.cpuFallbackDeals = cpuFallbackDeals;
    mc.runtimeTag = runtimeTag;
    modeCaptures.push_back(mc);
  }
  summary.close();

  if (options.htmlReport != "")
  {
    ofstream html(options.htmlReport.c_str());
    if (html)
    {
      html << "<!doctype html><html><head><meta charset=\"utf-8\">";
      html << "<title>DDS PBN Report</title>";
      html << "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:16px;} "
           << "table{border-collapse:collapse;width:100%;font-size:12px;} "
           << "th,td{border:1px solid #ccc;padding:4px;vertical-align:top;} "
           << "th{background:#f5f5f5;position:sticky;top:0;} "
           << ".mono{font-family:Consolas,monospace;white-space:nowrap;} "
           << ".small{font-size:11px;color:#444;} "
           << "</style></head><body>";
      html << "<h2>DDS PBN Evaluation Report</h2>";
      html << "<p><b>Source:</b> " << HtmlEscape(options.pbnSource) << "<br>";
      html << "<b>Deals:</b> " << nDeals << " &nbsp; <b>Cells:</b> " << (nDeals * 20) << "</p>";

      html << "<h3>Mode Summary</h3><table><thead><tr>"
           << "<th>Mode</th><th>Execution Path</th><th>CPU Fallback Deals</th>"
           << "<th>Elapsed ms</th><th>DD tables/sec</th><th>DD solutions/sec</th><th>Runtime Tag</th>"
           << "</tr></thead><tbody>";
      for (size_t mi = 0; mi < modeCaptures.size(); mi++)
      {
        const ModeCapture& mc = modeCaptures[mi];
        html << "<tr><td>" << HtmlEscape(mc.name) << "</td>"
             << "<td>" << mc.executionPath << "</td>"
             << "<td>" << mc.cpuFallbackDeals << "</td>"
             << "<td>" << mc.elapsedMs << "</td>"
             << "<td>" << mc.tablesPerSec << "</td>"
             << "<td>" << mc.solutionsPerSec << "</td>"
             << "<td class=\"small\">" << HtmlEscape(mc.runtimeTag) << "</td></tr>";
      }
      html << "</tbody></table>";

      auto findMode = [&](const string& name) -> const ModeCapture *
      {
        for (size_t i = 0; i < modeCaptures.size(); i++)
          if (modeCaptures[i].name == name)
            return &modeCaptures[i];
        return nullptr;
      };
      const ModeCapture * fastCap = findMode("fast");
      const ModeCapture * hybridCap = findMode("hybrid");
      const ModeCapture * exactCap = findMode("exact");

      html << "<h3>Per-board DataFrame</h3>";
      html << "<table><thead><tr>"
           << "<th>row</th><th>board</th><th>deal</th>"
           << "<th>cpu_exact_dd20</th>"
           << "<th>fast_dd20</th><th>hybrid_dd20</th><th>exact_dd20</th>"
           << "<th>pbn_ref_dd20</th>"
           << "</tr></thead><tbody>";
      for (int i = 0; i < nDeals; i++)
      {
        string b = boardLabels[static_cast<size_t>(i)];
        if (b == "")
        {
          ostringstream bo;
          bo << (i + 1);
          b = bo.str();
        }

        html << "<tr>"
             << "<td>" << (i + 1) << "</td>"
             << "<td>" << HtmlEscape(b) << "</td>"
             << "<td class=\"small\">" << HtmlEscape(dealTexts[static_cast<size_t>(i)]) << "</td>"
             << "<td class=\"mono\">" << HtmlEscape(TableCompact(exact[static_cast<size_t>(i)])) << "</td>";
        if (fastCap != nullptr)
          html << "<td class=\"mono\">" << HtmlEscape(TableCompact(fastCap->tables[static_cast<size_t>(i)])) << "</td>";
        else
          html << "<td>-</td>";
        if (hybridCap != nullptr)
          html << "<td class=\"mono\">" << HtmlEscape(TableCompact(hybridCap->tables[static_cast<size_t>(i)])) << "</td>";
        else
          html << "<td>-</td>";
        if (exactCap != nullptr)
          html << "<td class=\"mono\">" << HtmlEscape(TableCompact(exactCap->tables[static_cast<size_t>(i)])) << "</td>";
        else
          html << "<td>-</td>";
        if (referenceTables[static_cast<size_t>(i)].resTable[0][0] != 0 ||
            referenceTables[static_cast<size_t>(i)].resTable[0][1] != 0 ||
            referenceTables[static_cast<size_t>(i)].resTable[0][2] != 0 ||
            referenceTables[static_cast<size_t>(i)].resTable[0][3] != 0 ||
            referenceIdx.size() > 0)
          html << "<td class=\"mono\">" << HtmlEscape(TableCompact(referenceTables[static_cast<size_t>(i)])) << "</td>";
        else
          html << "<td>-</td>";
        html << "</tr>";
      }
      html << "</tbody></table>";
      html << "</body></html>";
      html.close();
      cout << "Wrote HTML report: " << options.htmlReport << "\n";
      if (! LaunchHtmlReport(options.htmlReport))
        cout << "Warning: could not auto-launch HTML report in default browser.\n";
    }
    else
    {
      cout << "Could not write HTML report: " << options.htmlReport << "\n";
    }
  }

  cout << "Wrote combined PBN summary: " << csvPath << "\n";
  return true;
}
