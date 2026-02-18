/*
   DDS, a bridge double dummy solver.

   Backend evaluation helpers:
   random deal generation, CPU exact solving, and reports.
*/

#include "backend_eval.h"
#include "oob_dds.h"

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

  static const char SEAT_CHARS[DDS_HANDS] = {'N', 'E', 'S', 'W'};
  static const char RANK_CHARS[13] = {'A', 'K', 'Q', 'J', 'T', '9', '8', '7',
    '6', '5', '4', '3', '2'};
  static const int RANK_VALUES[13] = {14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2};

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
    const int step = max(1, total / 20);
    return (done % step) == 0;
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
    const string& progressLabel,
    double * elapsedMsOut = nullptr)
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

    if (elapsedMsOut)
      *elapsedMsOut = ms;

    return true;
  }

  void WriteSummaryJson(
    const string& path,
    const int deals,
    const int cells,
    const int mismatches,
    const double exactRate,
    const double mae,
    const double elapsedMs,
    const double tablesPerSec,
    const double solutionsPerSec)
  {
    ofstream f(path.c_str());
    f << "{\n";
    f << "  \"num_deals\": " << deals << ",\n";
    f << "  \"num_cells\": " << cells << ",\n";
    f << "  \"mismatches\": " << mismatches << ",\n";
    f << "  \"exact_match_rate\": " << fixed << setprecision(8) << exactRate << ",\n";
    f << "  \"mae\": " << mae << ",\n";
    f << "  \"elapsed_ms\": " << elapsedMs << ",\n";
    f << "  \"dd_tables_per_sec\": " << tablesPerSec << ",\n";
    f << "  \"dd_solutions_per_sec\": " << solutionsPerSec << "\n";
    f << "}\n";
  }

  // OOB-compatible batch structs matching upstream DDS layout
  // (MAXNOOFTABLES=40, MAXNOOFBOARDS=200).  These are ABI-compatible
  // with an unmodified upstream DDS DLL, unlike the ddss-compiled
  // structs which use MAXNOOFTABLES=1000.
  static constexpr int OOB_MAXNOOFTABLES = 40;
  static constexpr int OOB_MAXNOOFBOARDS = OOB_MAXNOOFTABLES * DDS_STRAINS;

  struct OobDdTableDealsPBN
  {
    int noOfTables;
    ddTableDealPBN deals[OOB_MAXNOOFBOARDS];
  };

  struct OobDdTablesRes
  {
    int noOfBoards;
    ddTableResults results[OOB_MAXNOOFBOARDS];
  };

  struct OobAllParResults
  {
    parResults presults[OOB_MAXNOOFTABLES];
  };

  // Solve deals via OOB DLL's batch CalcAllTablesPBN API and compare
  // against ddss results.  Chunks into groups of OOB_MAXNOOFTABLES (40)
  // to match the upstream struct layout.
  // ddssMs is the elapsed time for the ddss solve pass (for comparison).
  // If oobResultsOut is non-null, the OOB results are stored there.
  // Returns the number of mismatched cells, or -1 on fatal error.
  int CompareWithOob(
    OobDds& oob,
    const vector<dealPBN>& deals,
    const vector<ddTableResults>& ddssResults,
    const int nDeals,
    const double ddssMs,
    const string& reportDir,
    const string& csvName,
    vector<ddTableResults> * oobResultsOut = nullptr)
  {
    if (! oob.IsLoaded())
    {
      cout << "OOB DLL not loaded\n";
      return -1;
    }

    cout << "\n=== OOB Cross-Verification ===\n";
    cout << "OOB DLL:   " << oob.Path() << "\n";
    cout << "OOB mode:  batched (CalcAllTablesPBN, chunks of "
         << OOB_MAXNOOFTABLES << ")\n";
    cout << "Deals:     " << nDeals << " (" << (nDeals * 20) << " cells)\n";

    if (oobResultsOut)
      oobResultsOut->resize(static_cast<size_t>(nDeals));

    // Solve all deals via OOB batch API.
    vector<ddTableResults> oobTables(static_cast<size_t>(nDeals));

    auto batchDeals = std::make_unique<OobDdTableDealsPBN>();
    auto batchRes   = std::make_unique<OobDdTablesRes>();
    auto batchPar   = std::make_unique<OobAllParResults>();

    const chrono::high_resolution_clock::time_point t0 =
      chrono::high_resolution_clock::now();

    int oobErrors = 0;

    for (int i = 0; i < nDeals; i += OOB_MAXNOOFTABLES)
    {
      const int count = min(OOB_MAXNOOFTABLES, nDeals - i);
      memset(batchDeals.get(), 0, sizeof(OobDdTableDealsPBN));
      memset(batchRes.get(), 0, sizeof(OobDdTablesRes));
      memset(batchPar.get(), 0, sizeof(OobAllParResults));

      batchDeals->noOfTables = count;
      for (int j = 0; j < count; j++)
      {
        strncpy(batchDeals->deals[j].cards,
                deals[static_cast<size_t>(i + j)].remainCards,
                sizeof(batchDeals->deals[j].cards) - 1);
      }

      int filter[DDS_STRAINS] = {0, 0, 0, 0, 0};
      const int ret = oob.CalcAllTablesPBNRaw(
        batchDeals.get(), -1, filter,
        batchRes.get(), batchPar.get());

      if (ret == RETURN_UNKNOWN_FAULT)
      {
        cout << "FATAL: OOB DLL does not export CalcAllTablesPBN\n";
        return -1;
      }

      if (ret != RETURN_NO_FAULT)
      {
        char errBuf[80] = {0};
        oob.ErrorMessage(ret, errBuf);
        cout << "OOB CalcAllTablesPBN error at batch offset " << i
             << " (count " << count << "): " << ret << " " << errBuf << "\n";
        oobErrors += count;
        for (int j = 0; j < count; j++)
          memset(&oobTables[static_cast<size_t>(i + j)], 0, sizeof(ddTableResults));
        continue;
      }

      for (int j = 0; j < count; j++)
        oobTables[static_cast<size_t>(i + j)] = batchRes->results[j];
    }

    const chrono::high_resolution_clock::time_point t1 =
      chrono::high_resolution_clock::now();
    const double oobMs =
      chrono::duration_cast<chrono::duration<double, milli> >(t1 - t0).count();

    // Compare results cell-by-cell.
    const string mmPath = reportDir + "/" + csvName;
    ofstream mm(mmPath.c_str());
    mm << "deal_idx,combo_idx,strain,declarer,ddss,oob,delta\n";

    int totalMismatches = 0;
    int totalMatches = 0;

    for (int i = 0; i < nDeals; i++)
    {
      const ddTableResults& got = ddssResults[static_cast<size_t>(i)];
      const ddTableResults& oobTable = oobTables[static_cast<size_t>(i)];

      if (oobResultsOut)
        (*oobResultsOut)[static_cast<size_t>(i)] = oobTable;

      for (int strain = 0; strain < DDS_STRAINS; strain++)
      {
        for (int declarer = 0; declarer < DDS_HANDS; declarer++)
        {
          const int a = got.resTable[strain][declarer];
          const int b = oobTable.resTable[strain][declarer];
          if (a != b)
          {
            const int idx = 4 * strain + declarer;
            mm << i << "," << idx << "," << strain << "," << declarer
               << "," << a << "," << b << "," << (a - b) << "\n";
            totalMismatches++;
          }
          else
          {
            totalMatches++;
          }
        }
      }
    }
    mm.close();

    const double ddssRate = (ddssMs > 0.0) ? (nDeals * 1000.0 / ddssMs) : 0.0;
    const double oobRate = (oobMs > 0.0) ? (nDeals * 1000.0 / oobMs) : 0.0;

    cout << "\n--- Timing ---\n";
    cout << "  ddss (batched):  " << fixed << setprecision(1) << ddssMs << " ms  ("
         << setprecision(1) << ddssRate << " tables/s)\n";
    cout << "  OOB  (batched):  " << fixed << setprecision(1) << oobMs << " ms  ("
         << setprecision(1) << oobRate << " tables/s)\n";
    if (oobMs > 0.0 && ddssMs > 0.0)
    {
      const double ratio = oobMs / ddssMs;
      if (ratio > 1.0)
        cout << "  ddss is " << setprecision(2) << ratio << "x faster than OOB\n";
      else if (ratio < 1.0)
        cout << "  OOB is " << setprecision(2) << (1.0 / ratio) << "x faster than ddss\n";
      else
        cout << "  Same speed\n";
    }

    const int totalCells = nDeals * 20;
    cout << "\n--- Totals ---\n";
    cout << "  Cells compared: " << totalCells << "\n";
    cout << "  Matches:        " << totalMatches << "\n";
    cout << "  Mismatches:     " << totalMismatches << "\n";
    if (oobErrors > 0)
      cout << "  OOB errors:     " << oobErrors << "\n";

    if (totalMismatches == 0)
      cout << "  Result: PASS\n";
    else
      cout << "  Result: FAIL\n"
           << "  Mismatches written to: " << mmPath << "\n";

    cout << "==============================\n\n";

    return totalMismatches;
  }
}


vector<RandomDeal> GenerateRandomDeals(
  const int numDeals,
  const int seed)
{
  vector<RandomDeal> out;
  out.resize(static_cast<unsigned>(numDeals));

  mt19937 rng(static_cast<unsigned>(seed));

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
      pbn += SeatOrderPBN(handCards[seat]);
      if (off + 1 != DDS_HANDS)
        pbn += " ";
    }

    strncpy(rd.deal.remainCards, pbn.c_str(), sizeof(rd.deal.remainCards)-1);
    rd.deal.remainCards[sizeof(rd.deal.remainCards)-1] = '\0';

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
    options.randomDeals, options.randomSeed);
  const int nDeals = static_cast<int>(deals.size());
  const int cells = nDeals * 20;
  cout << "Solving " << nDeals << " random deals (CPU exact)\n";

  vector<dealPBN> dealPbns(static_cast<unsigned>(nDeals));
  for (int i = 0; i < nDeals; i++)
    dealPbns[static_cast<unsigned>(i)] = deals[static_cast<unsigned>(i)].deal;

  vector<ddTableResults> results;
  double elapsedMs = 0.0;

  if (!SolveCpuExactBatched(dealPbns, nDeals, results, "cpu-exact", &elapsedMs))
    return false;

  const double secs = elapsedMs / 1000.0;
  const double tablesPerSec = (secs > 0.0 ? static_cast<double>(nDeals) / secs : 0.0);
  const double solutionsPerSec = (secs > 0.0 ? static_cast<double>(cells) / secs : 0.0);

  cout << "CPU exact: " << nDeals << " deals, " << cells << " cells, "
       << fixed << setprecision(1) << elapsedMs << " ms, "
       << tablesPerSec << " tables/s, "
       << solutionsPerSec << " solutions/s\n";

  const string summaryPath = options.reportDir + "/dds_solve_summary.json";
  WriteSummaryJson(summaryPath, nDeals, cells, 0, 1.0, 0.0,
    elapsedMs, tablesPerSec, solutionsPerSec);

  // OOB cross-verification if --verify is enabled.
  if (options.verify)
  {
    OobDds oob;
    if (oob.Load(options.oobDll))
    {
      oob.PrintStatus();
      oob.PrintInfo();
      oob.SetMaxThreads(options.numThreads);
      CompareWithOob(oob, dealPbns, results, nDeals, elapsedMs,
        options.reportDir, "oob_mismatches.csv");
    }
  }

  return true;
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

  // Convert GitHub blob/tree URLs to raw content URLs.
  // e.g.  https://github.com/user/repo/blob/branch/path
  //    -> https://raw.githubusercontent.com/user/repo/refs/heads/branch/path
  string NormalizeGitHubUrl(
    const string& url)
  {
    const string blobPrefix = "https://github.com/";
    if (url.rfind(blobPrefix, 0) != 0)
      return url;

    string tail = url.substr(blobPrefix.size());

    // Find /blob/ or /tree/ segment after user/repo.
    const size_t slashCount = 2; // skip user/repo (2 slashes)
    size_t pos = 0;
    for (size_t n = 0; n < slashCount && pos != string::npos; n++)
      pos = tail.find('/', pos + (n > 0 ? 1 : 0));

    if (pos == string::npos)
      return url;

    string userRepo = tail.substr(0, pos);
    string rest = tail.substr(pos); // starts with /blob/branch/...

    const string blobSeg = "/blob/";
    const string treeSeg = "/tree/";
    string branchAndPath;
    if (rest.rfind(blobSeg, 0) == 0)
      branchAndPath = rest.substr(blobSeg.size());
    else if (rest.rfind(treeSeg, 0) == 0)
      branchAndPath = rest.substr(treeSeg.size());
    else
      return url;

    return "https://raw.githubusercontent.com/" + userRepo +
      "/refs/heads/" + branchAndPath;
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

    // Auto-convert github.com/blob URLs to raw content URLs.
    const string url = NormalizeGitHubUrl(source);
    if (url != source)
      cout << "  URL rewritten to: " << url << "\n";

    auto StripBom = [](string& s)
    {
      if (s.size() >= 3 &&
          static_cast<unsigned char>(s[0]) == 0xEF &&
          static_cast<unsigned char>(s[1]) == 0xBB &&
          static_cast<unsigned char>(s[2]) == 0xBF)
        s.erase(0, 3);
    };

#ifdef _WIN32
    auto RunReadCmd = [&](const string& cmd) -> bool
    {
      FILE * fp = _popen(cmd.c_str(), "rb");
      if (! fp)
        return false;
      char buf[4096];
      textOut = "";
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        textOut.append(buf, n);
      const int rc = _pclose(fp);
      if (rc != 0 || textOut == "")
        return false;
      StripBom(textOut);
      return true;
    };

    // Try curl first -- it writes raw bytes faithfully.
    if (RunReadCmd("curl.exe -fsSL \"" + url + "\" 2>nul"))
      return true;

    // Fallback to PowerShell Invoke-WebRequest.
    string safeUrl = url;
    for (size_t i = 0; i < safeUrl.size(); i++)
      if (safeUrl[i] == '\'')
        safeUrl.insert(i++, "'");
    const string psExpr =
      "\"[Console]::OutputEncoding=[System.Text.Encoding]::UTF8;"
      "$ProgressPreference='SilentlyContinue'; "
      "(Invoke-WebRequest -UseBasicParsing -Uri '" + safeUrl + "').Content\"";

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

    return false;
#else
    auto RunPipeCmd = [&](const string& cmd) -> bool
    {
      FILE * fp = popen(cmd.c_str(), "r");
      if (! fp)
        return false;
      char buf[4096];
      textOut = "";
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        textOut.append(buf, n);
      int rc = pclose(fp);
      if (rc != 0 || textOut == "")
        return false;
      StripBom(textOut);
      return true;
    };

    string safeUrl = url;
    for (size_t i = 0; i < safeUrl.size(); i++)
    {
      if (safeUrl[i] == '\'')
      {
        safeUrl.replace(i, 1, "'\"'\"'");
        i += 4;
      }
    }

    if (RunPipeCmd("curl -fsSL '" + safeUrl + "' 2>/dev/null"))
      return true;

    if (RunPipeCmd("wget -qO- '" + safeUrl + "' 2>/dev/null"))
      return true;

    return false;
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
        memset(&e.deal, 0, sizeof(e.deal));
        e.hasReferenceTable = false;
        memset(&e.referenceTable, 0, sizeof(e.referenceTable));
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
        memset(&e.deal, 0, sizeof(e.deal));
        e.hasReferenceTable = false;
        memset(&e.referenceTable, 0, sizeof(e.referenceTable));
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
  const string resolvedSource = IsUrl(options.pbnSource)
    ? NormalizeGitHubUrl(options.pbnSource) : options.pbnSource;
  if (! LoadTextFromSource(options.pbnSource, text))
  {
    cout << "Could not read PBN source '" << resolvedSource << "'\n";
    return false;
  }

  vector<PbnDealEntry> entries;
  if (! ParsePbnSourceText(text, entries))
  {
    cout << "No deals parsed from PBN source\n";
    return false;
  }

  vector<dealPBN> pbnDeals;
  pbnDeals.reserve(entries.size());
  vector<int> referenceIdx;
  vector<ddTableResults> referenceTables;
  referenceTables.reserve(entries.size());
  vector<string> boardLabels;
  vector<string> dealTexts;
  boardLabels.reserve(entries.size());
  dealTexts.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); i++)
  {
    pbnDeals.push_back(entries[i].deal);
    boardLabels.push_back(entries[i].boardLabel);
    dealTexts.push_back(entries[i].dealText);
    ddTableResults dummy;
    memset(&dummy, 0, sizeof(dummy));
    referenceTables.push_back(dummy);
    if (entries[i].hasReferenceTable)
    {
      referenceIdx.push_back(static_cast<int>(pbnDeals.size() - 1));
      referenceTables.back() = entries[i].referenceTable;
    }
  }

  const int nDeals = static_cast<int>(pbnDeals.size());
  if (nDeals == 0)
  {
    cout << "Parsed deals had invalid structure\n";
    return false;
  }

  cout << "Loaded " << nDeals << " deals from PBN source";
  cout << " (" << referenceIdx.size() << " with embedded DD tables)\n";

  vector<ddTableResults> exact;
  double ddssMs = 0.0;
  if (!SolveCpuExactBatched(pbnDeals, nDeals, exact, "pbn-exact", &ddssMs))
    return false;

  const int numCells = nDeals * 20;

  int pExactMatches = 0;
  int pOff1 = 0;
  int pOff2 = 0;
  long long pAbsSum = 0;
  const int pCells = static_cast<int>(referenceIdx.size()) * 20;

  const string mmPath = options.reportDir + "/pbn_mismatches.csv";
  ofstream mm(mmPath.c_str());
  mm << "deal_idx,source,combo_idx,expected,predicted,delta\n";

  for (size_t r = 0; r < referenceIdx.size(); r++)
  {
    const int i = referenceIdx[r];
    const ddTableResults& exp = referenceTables[static_cast<size_t>(i)];
    const ddTableResults& got = exact[static_cast<size_t>(i)];
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

  cout << "PBN exact: " << nDeals << " deals, " << numCells << " cells\n";
  if (pCells > 0)
    cout << "  vs PBN reference: exact_match=" << fixed << setprecision(6) << pExactRate
         << " mae=" << pMae << " off1=" << pOff1Rate << " off2=" << pOff2Rate << "\n";

  // OOB cross-verification if --verify is enabled.
  bool hasOob = false;
  vector<ddTableResults> oobResults;
  if (options.verify)
  {
    OobDds oob;
    if (oob.Load(options.oobDll))
    {
      oob.PrintStatus();
      oob.PrintInfo();
      oob.SetMaxThreads(options.numThreads);
      CompareWithOob(oob, pbnDeals, exact, nDeals, ddssMs,
        options.reportDir, "oob_mismatches.csv", &oobResults);
      hasOob = (oobResults.size() == static_cast<size_t>(nDeals));
    }
  }

  const string csvPath = options.reportDir + "/pbn_combined_summary.csv";
  ofstream summary(csvPath.c_str());
  summary << "num_deals,num_cells,num_deals_with_pbn_table,exact_match_vs_pbn,mae_vs_pbn,off1_vs_pbn,off2_vs_pbn\n";
  summary << nDeals << "," << numCells << ","
    << referenceIdx.size() << ",";
  if (pCells > 0)
    summary << pExactRate << "," << pMae << "," << pOff1Rate << "," << pOff2Rate;
  else
    summary << ",,,,";
  summary << "\n";
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
           << ".mismatch{background:#fdd;} "
           << "</style></head><body>";
      html << "<h2>DDS PBN Evaluation Report</h2>";
      html << "<p><b>Source:</b> " << HtmlEscape(options.pbnSource) << "<br>";
      if (hasOob)
        html << "<b>OOB DLL:</b> " << HtmlEscape(options.oobDll) << "<br>";
      html << "<b>Deals:</b> " << nDeals << " &nbsp; <b>Cells:</b> " << numCells << "</p>";

      html << "<h3>Per-board Results</h3>";
      html << "<table><thead><tr>"
           << "<th>row</th><th>board</th><th>deal</th>"
           << "<th>cpu_exact_dd20</th>"
           << "<th>pbn_ref_dd20</th>";
      if (hasOob)
        html << "<th>oob_dd20</th>";
      html << "</tr></thead><tbody>";
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
        if (referenceTables[static_cast<size_t>(i)].resTable[0][0] != 0 ||
            referenceTables[static_cast<size_t>(i)].resTable[0][1] != 0 ||
            referenceTables[static_cast<size_t>(i)].resTable[0][2] != 0 ||
            referenceTables[static_cast<size_t>(i)].resTable[0][3] != 0 ||
            referenceIdx.size() > 0)
          html << "<td class=\"mono\">" << HtmlEscape(TableCompact(referenceTables[static_cast<size_t>(i)])) << "</td>";
        else
          html << "<td>-</td>";

        if (hasOob)
        {
          const string oobStr = TableCompact(oobResults[static_cast<size_t>(i)]);
          const string ddssStr = TableCompact(exact[static_cast<size_t>(i)]);
          const string cls = (oobStr != ddssStr) ? "mono mismatch" : "mono";
          html << "<td class=\"" << cls << "\">"
               << HtmlEscape(oobStr) << "</td>";
        }
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
