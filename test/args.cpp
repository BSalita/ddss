/* 
   DDS, a bridge double dummy solver.

   Copyright (C) 2006-2014 by Bo Haglund / 
   2014-2018 by Bo Haglund & Soren Hein.

   See LICENSE and README.
*/


// These functions parse the command line for options.


#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


#include "args.h"
#include "cst.h"

using namespace std;


extern OptionsType options;

struct optEntry
{
  string shortName;
  string longName;
  unsigned numArgs;
};

#define DTEST_NUM_OPTIONS 11

const optEntry optList[DTEST_NUM_OPTIONS] =
{
  {"f", "file", 1},
  {"s", "solver", 1},
  {"t", "threading", 1},
  {"n", "numthr", 1},
  {"m", "memory", 1},
  {"r", "random-deals", 1},
  {"k", "reduced-cards", 1},
  {"e", "seed", 1},
  {"g", "report-dir", 1},
  {"p", "pbn-source", 1},
  {"w", "html-report", 1}
};

const vector<string> solverList =
{
  "solve",
  "calc",
  "play",
  "par",
  "dealerpar"
};

const vector<string> threadingList =
{
  "none",
  "WinAPI",
  "OpenMP",
  "GCD",
  "Boost",
  "STL",
  "TBB",
  "STLIMPL",
  "PPLIMPL",
  "default"
};

string shortOptsAll, shortOptsWithArg;

int GetNextArgToken(
  int argc,
  char * argv[]);

void SetDefaults();

bool ParseRound();


void Usage(
  const char base[])
{
  string basename(base);
  const size_t l = basename.find_last_of("\\/");
  if (l != string::npos)
    basename.erase(0, l+1);

  cout <<
    "Usage: " << basename << " [options]\n\n" <<
    "-f, --file s       Input file, or the number n;\n" <<
    "                   '100' means ../hands/list100.txt).\n" <<
    "                   (Default: input.txt)\n" <<
    "\n" <<
    "-s, --solver       One of: solve, calc, play, par, dealerpar.\n" <<
    "                   (Default: solve)\n" <<
    "\n" <<
    "-t, --threading t  Currently one of (case-insensitive):\n" <<
    "                   default, none, winapi, openmp, gcd, boost,\n" <<
    "                   stl, tbb, stlimpl, pplimpl.\n" <<
    "                   (Default: default meaning that DDS decides)\n" <<
    "\n" <<
    "-n, --numthr n     Maximum number of threads.\n" <<
    "                   (Default: 0 meaning that DDS decides)\n" <<
    "\n" <<
    "-m, --memory n     Total DDS memory size in MB.\n" <<
    "                   (Default: 0 meaning that DDS decides)\n" <<
    "\n" <<
    "-r, --random-deals n  Generate n random deals and solve.\n" <<
    "                   (Default: 0 meaning disabled)\n" <<
    "\n" <<
    "-k, --reduced-cards n Cards per hand in random-deals mode.\n" <<
    "                   Range: 1..13.\n" <<
    "                   (Default: 13)\n" <<
    "\n" <<
    "-e, --seed n       Random seed for generated deals.\n" <<
    "                   (Default: 42)\n" <<
    "\n" <<
    "-g, --report-dir p Directory for reports.\n" <<
    "                   (Default: dds_compare_reports)\n" <<
    "\n" <<
    "-p, --pbn-source s Local PBN file path or URL.\n" <<
    "                   If set, runs PBN evaluation mode.\n" <<
    "\n" <<
    "-w, --html-report f Write a readable HTML report file.\n" <<
    "                   Intended for PBN evaluation mode.\n" <<
    "\n" <<
    endl;
}


int nextToken = 1;
char * optarg;

int GetNextArgToken(
  int argc,
  char * argv[])
{
  // 0 means done, -1 means error.

  if (nextToken >= argc)
    return 0;

  string str(argv[nextToken]);
  if (str[0] != '-' || str.size() == 1)
    return -1;

  if (str[1] == '-')
  {
    if (str.size() == 2)
      return -1;
    str.erase(0, 2);
  }
  else if (str.size() == 2)
    str.erase(0, 1);
  else
    return -1;

  for (unsigned i = 0; i < DTEST_NUM_OPTIONS; i++)
  {
    if (str == optList[i].shortName || str == optList[i].longName)
    {
      if (optList[i].numArgs == 1)
      {
        if (nextToken+1 >= argc)
          return -1;

        optarg = argv[nextToken+1];
        nextToken += 2;
      }
      else
        nextToken++;

      return optList[i].shortName[0];
    }
  }

  return -1;
}


void SetDefaults()
{
  options.fname = "input.txt";
  options.solver = DTEST_SOLVER_SOLVE;
  options.threading = DTEST_THREADING_DEFAULT;
  options.numThreads = 0;
  options.memoryMB = 0;
  options.randomDeals = 0;
  options.reducedCards = 13;
  options.randomSeed = 42;
  options.reportDir = "dds_compare_reports";
  options.pbnSource = "";
  options.htmlReport = "";
}


void PrintOptions()
{
  cout << left;
  cout << setw(12) << "file" << 
    setw(12) <<  options.fname << "\n";
  cout << setw(12) << "solver" << setw(12) <<  
    solverList[options.solver] << "\n";
  cout << setw(12) << "threading" << setw(12) <<  
    threadingList[options.threading] << "\n";
  cout << setw(12) << "threads" << setw(12) <<  
    options.numThreads << "\n";
  cout << setw(12) << "memory" << setw(12) <<  
    options.memoryMB << " MB\n";
  cout << setw(12) << "deals" << setw(12) <<
    options.randomDeals << "\n";
  cout << setw(12) << "red-cards" << setw(12) <<
    options.reducedCards << "\n";
  cout << setw(12) << "seed" << setw(12) <<
    options.randomSeed << "\n";
  cout << setw(12) << "reports" << setw(12) <<
    options.reportDir << "\n";
  cout << setw(12) << "pbn-source" << setw(12) <<
    (options.pbnSource == "" ? "-" : options.pbnSource) << "\n";
  cout << setw(12) << "html-report" << setw(12) <<
    (options.htmlReport == "" ? "-" : options.htmlReport) << "\n";
  cout << "\n" << right;
}


void ReadArgs(
  int argc,
  char * argv[])
{
  for (unsigned i = 0; i < DTEST_NUM_OPTIONS; i++)
  {
    shortOptsAll += optList[i].shortName;
    if (optList[i].numArgs)
      shortOptsWithArg += optList[i].shortName;
  }

  if (argc == 1)
  {
    Usage(argv[0]);
    exit(0);
  }

  SetDefaults();

  int c, m = 0;
  bool errFlag = false, matchFlag;
  string stmp;
  char * ctmp;
  struct stat buffer;

  while ((c = GetNextArgToken(argc, argv)) > 0)
  {
    switch(c)
    {
      case 'f':
        if (stat(optarg, &buffer) == 0)
        {
          options.fname = string(optarg);
          break;
        }

        stmp = "../hands/list" + string(optarg) + ".txt";
        if (stat(stmp.c_str(), &buffer) == 0)
        {
          options.fname = stmp;
          break;
        }

        cout << "Input file '" << optarg << "' not found\n";
        cout << "Input file '" << stmp << "' not found\n";
        nextToken -= 2;
        errFlag = true;
        break;

      case 's':
        matchFlag = false;
        stmp = optarg;
        transform(stmp.begin(), stmp.end(), stmp.begin(), ::tolower);

        for (unsigned i = 0; i < DTEST_SOLVER_SIZE && ! matchFlag; i++)
        {
          string s = solverList[i];
          transform(s.begin(), s.end(), s.begin(), ::tolower); 
          if (stmp == s)
          {
            m = static_cast<int>(i);
            matchFlag = true;
          }
        }

        if (matchFlag)
          options.solver = static_cast<Solver>(m);
        else
        {
          cout << "Solver '" << optarg << "' not found\n";
          nextToken -= 2;
          errFlag = true;
        }
        break;

      case 't':
        matchFlag = false;
        stmp = optarg;
        transform(stmp.begin(), stmp.end(), stmp.begin(), ::tolower);

        for (unsigned i = 0; i < DTEST_THREADING_SIZE && ! matchFlag; i++)
        {
          string s = threadingList[i];
          transform(s.begin(), s.end(), s.begin(), ::tolower); 
          if (stmp == s)
          {
            m = static_cast<int>(i);
            matchFlag = true;
          }
        }

        if (matchFlag)
          options.threading = static_cast<Threading>(m);
        else
        {
          cout << "Threading '" << optarg << "' not found\n";
          nextToken -= 2;
          errFlag = true;
        }
        break;

      case 'n':
        m = static_cast<int>(strtol(optarg, &ctmp, 0));
        if (m < 0)
        {
          cout << "Number of threads must be >= 0\n\n";
          nextToken -= 2;
          errFlag = true;
        }
        options.numThreads = m;
        break;

      case 'm':
        m = static_cast<int>(strtol(optarg, &ctmp, 0));
        if (m < 0)
        {
          cout << "Memory in MB must be >= 0\n\n";
          nextToken -= 2;
          errFlag = true;
        }
        options.memoryMB = m;
        break;

      case 'r':
        m = static_cast<int>(strtol(optarg, &ctmp, 0));
        if (m < 0)
        {
          cout << "Number of random deals must be >= 0\n\n";
          nextToken -= 2;
          errFlag = true;
        }
        options.randomDeals = m;
        break;

      case 'k':
        m = static_cast<int>(strtol(optarg, &ctmp, 0));
        if (m < 1 || m > 13)
        {
          cout << "Reduced cards must be in [1, 13]\n\n";
          nextToken -= 2;
          errFlag = true;
        }
        options.reducedCards = m;
        break;

      case 'e':
        m = static_cast<int>(strtol(optarg, &ctmp, 0));
        options.randomSeed = m;
        break;

      case 'g':
        options.reportDir = string(optarg);
        break;

      case 'p':
        options.pbnSource = string(optarg);
        break;

      case 'w':
        options.htmlReport = string(optarg);
        break;

      default:
        cout << "Unknown option\n";
        errFlag = true;
        break;
    }
    if (errFlag)
      break;
  }

  if (errFlag || c == -1)
  {
    cout << "Error while parsing option '" << argv[nextToken] << "'\n";
    cout << "Invoke the program without arguments for help" << endl;
    exit(0);
  }
}
