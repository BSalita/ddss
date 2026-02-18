/*
   DDS, a bridge double dummy solver.

   OOB (out-of-box) DDS DLL loader implementation.
   Dynamically loads an unmodified upstream DDS DLL at runtime
   for correctness verification against the ddss fork.
*/

#include "oob_dds.h"

#include <iostream>
#include <cstring>

using namespace std;


// Platform-specific helpers for symbol resolution.
template<typename T>
T OobDds::ResolveSym(const char * name)
{
#ifdef _WIN32
  return reinterpret_cast<T>(GetProcAddress(handle_, name));
#else
  return reinterpret_cast<T>(dlsym(handle_, name));
#endif
}


void OobDds::ClearPointers()
{
  p_SetMaxThreads = nullptr;
  p_SetThreading = nullptr;
  p_SetResources = nullptr;
  p_FreeMemory = nullptr;
  p_SolveBoard = nullptr;
  p_SolveBoardPBN = nullptr;
  p_CalcDDtable = nullptr;
  p_CalcDDtablePBN = nullptr;
  p_CalcAllTables = nullptr;
  p_CalcAllTablesPBN = nullptr;
  p_CalcAllTablesPBNx = nullptr;
  p_SolveAllBoards = nullptr;
  p_SolveAllBoardsBin = nullptr;
  p_SolveAllChunks = nullptr;
  p_SolveAllChunksBin = nullptr;
  p_SolveAllChunksPBN = nullptr;
  p_Par = nullptr;
  p_CalcPar = nullptr;
  p_CalcParPBN = nullptr;
  p_SidesPar = nullptr;
  p_DealerPar = nullptr;
  p_DealerParBin = nullptr;
  p_SidesParBin = nullptr;
  p_ConvertToDealerTextFormat = nullptr;
  p_ConvertToSidesTextFormat = nullptr;
  p_AnalysePlayBin = nullptr;
  p_AnalysePlayPBN = nullptr;
  p_AnalyseAllPlaysBin = nullptr;
  p_AnalyseAllPlaysPBN = nullptr;
  p_GetDDSInfo = nullptr;
  p_ErrorMessage = nullptr;
}


OobDds::OobDds()
  : handle_(nullptr)
{
  ClearPointers();
}


OobDds::~OobDds()
{
  if (handle_)
  {
    if (p_FreeMemory)
      p_FreeMemory();

#ifdef _WIN32
    FreeLibrary(handle_);
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
  }
}


bool OobDds::Load(const string& path)
{
  path_ = path;

#ifdef _WIN32
  handle_ = LoadLibraryA(path.c_str());
#else
  handle_ = dlopen(path.c_str(), RTLD_LAZY);
#endif

  if (! handle_)
  {
#ifdef _WIN32
    cout << "OOB DLL: LoadLibrary failed for '" << path
         << "' (error " << GetLastError() << ")\n";
#else
    cout << "OOB DLL: dlopen failed for '" << path
         << "': " << dlerror() << "\n";
#endif
    return false;
  }

  // Resolve all 30 functions. Missing functions get nullptr.
  p_SetMaxThreads          = ResolveSym<pfn_SetMaxThreads>("SetMaxThreads");
  p_SetThreading           = ResolveSym<pfn_SetThreading>("SetThreading");
  p_SetResources           = ResolveSym<pfn_SetResources>("SetResources");
  p_FreeMemory             = ResolveSym<pfn_FreeMemory>("FreeMemory");

  p_SolveBoard             = ResolveSym<pfn_SolveBoard>("SolveBoard");
  p_SolveBoardPBN          = ResolveSym<pfn_SolveBoardPBN>("SolveBoardPBN");

  p_CalcDDtable            = ResolveSym<pfn_CalcDDtable>("CalcDDtable");
  p_CalcDDtablePBN         = ResolveSym<pfn_CalcDDtablePBN>("CalcDDtablePBN");

  p_CalcAllTables          = ResolveSym<pfn_CalcAllTables>("CalcAllTables");
  p_CalcAllTablesPBN       = ResolveSym<pfn_CalcAllTablesPBN>("CalcAllTablesPBN");
  p_CalcAllTablesPBNx      = ResolveSym<pfn_CalcAllTablesPBNx>("CalcAllTablesPBNx");

  p_SolveAllBoards         = ResolveSym<pfn_SolveAllBoards>("SolveAllBoards");
  p_SolveAllBoardsBin      = ResolveSym<pfn_SolveAllBoardsBin>("SolveAllBoardsBin");
  p_SolveAllChunks         = ResolveSym<pfn_SolveAllChunks>("SolveAllChunks");
  p_SolveAllChunksBin      = ResolveSym<pfn_SolveAllChunksBin>("SolveAllChunksBin");
  p_SolveAllChunksPBN      = ResolveSym<pfn_SolveAllChunksPBN>("SolveAllChunksPBN");

  p_Par                    = ResolveSym<pfn_Par>("Par");
  p_CalcPar                = ResolveSym<pfn_CalcPar>("CalcPar");
  p_CalcParPBN             = ResolveSym<pfn_CalcParPBN>("CalcParPBN");
  p_SidesPar               = ResolveSym<pfn_SidesPar>("SidesPar");
  p_DealerPar              = ResolveSym<pfn_DealerPar>("DealerPar");
  p_DealerParBin           = ResolveSym<pfn_DealerParBin>("DealerParBin");
  p_SidesParBin            = ResolveSym<pfn_SidesParBin>("SidesParBin");
  p_ConvertToDealerTextFormat = ResolveSym<pfn_ConvertToDealerTextFormat>("ConvertToDealerTextFormat");
  p_ConvertToSidesTextFormat  = ResolveSym<pfn_ConvertToSidesTextFormat>("ConvertToSidesTextFormat");

  p_AnalysePlayBin         = ResolveSym<pfn_AnalysePlayBin>("AnalysePlayBin");
  p_AnalysePlayPBN         = ResolveSym<pfn_AnalysePlayPBN>("AnalysePlayPBN");
  p_AnalyseAllPlaysBin     = ResolveSym<pfn_AnalyseAllPlaysBin>("AnalyseAllPlaysBin");
  p_AnalyseAllPlaysPBN     = ResolveSym<pfn_AnalyseAllPlaysPBN>("AnalyseAllPlaysPBN");

  p_GetDDSInfo             = ResolveSym<pfn_GetDDSInfo>("GetDDSInfo");
  p_ErrorMessage           = ResolveSym<pfn_ErrorMessage>("ErrorMessage");

  return true;
}


bool OobDds::IsLoaded() const
{
  return handle_ != nullptr && p_CalcDDtablePBN != nullptr;
}


const string& OobDds::Path() const
{
  return path_;
}


void OobDds::PrintStatus() const
{
  struct { const char * name; const void * ptr; } funcs[] = {
    {"SetMaxThreads",          reinterpret_cast<const void *>(p_SetMaxThreads)},
    {"SetThreading",           reinterpret_cast<const void *>(p_SetThreading)},
    {"SetResources",           reinterpret_cast<const void *>(p_SetResources)},
    {"FreeMemory",             reinterpret_cast<const void *>(p_FreeMemory)},
    {"SolveBoard",             reinterpret_cast<const void *>(p_SolveBoard)},
    {"SolveBoardPBN",          reinterpret_cast<const void *>(p_SolveBoardPBN)},
    {"CalcDDtable",            reinterpret_cast<const void *>(p_CalcDDtable)},
    {"CalcDDtablePBN",         reinterpret_cast<const void *>(p_CalcDDtablePBN)},
    {"CalcAllTables",          reinterpret_cast<const void *>(p_CalcAllTables)},
    {"CalcAllTablesPBN",       reinterpret_cast<const void *>(p_CalcAllTablesPBN)},
    {"CalcAllTablesPBNx",      reinterpret_cast<const void *>(p_CalcAllTablesPBNx)},
    {"SolveAllBoards",         reinterpret_cast<const void *>(p_SolveAllBoards)},
    {"SolveAllBoardsBin",      reinterpret_cast<const void *>(p_SolveAllBoardsBin)},
    {"SolveAllChunks",         reinterpret_cast<const void *>(p_SolveAllChunks)},
    {"SolveAllChunksBin",      reinterpret_cast<const void *>(p_SolveAllChunksBin)},
    {"SolveAllChunksPBN",      reinterpret_cast<const void *>(p_SolveAllChunksPBN)},
    {"Par",                    reinterpret_cast<const void *>(p_Par)},
    {"CalcPar",                reinterpret_cast<const void *>(p_CalcPar)},
    {"CalcParPBN",             reinterpret_cast<const void *>(p_CalcParPBN)},
    {"SidesPar",               reinterpret_cast<const void *>(p_SidesPar)},
    {"DealerPar",              reinterpret_cast<const void *>(p_DealerPar)},
    {"DealerParBin",           reinterpret_cast<const void *>(p_DealerParBin)},
    {"SidesParBin",            reinterpret_cast<const void *>(p_SidesParBin)},
    {"ConvertToDealerTextFormat", reinterpret_cast<const void *>(p_ConvertToDealerTextFormat)},
    {"ConvertToSidesTextFormat",  reinterpret_cast<const void *>(p_ConvertToSidesTextFormat)},
    {"AnalysePlayBin",         reinterpret_cast<const void *>(p_AnalysePlayBin)},
    {"AnalysePlayPBN",         reinterpret_cast<const void *>(p_AnalysePlayPBN)},
    {"AnalyseAllPlaysBin",     reinterpret_cast<const void *>(p_AnalyseAllPlaysBin)},
    {"AnalyseAllPlaysPBN",     reinterpret_cast<const void *>(p_AnalyseAllPlaysPBN)},
    {"GetDDSInfo",             reinterpret_cast<const void *>(p_GetDDSInfo)},
    {"ErrorMessage",           reinterpret_cast<const void *>(p_ErrorMessage)},
  };

  int resolved = 0;
  int missing = 0;
  const int total = static_cast<int>(sizeof(funcs) / sizeof(funcs[0]));

  for (int i = 0; i < total; i++)
  {
    if (funcs[i].ptr)
      resolved++;
    else
      missing++;
  }

  cout << "OOB DLL: " << path_ << "\n";
  cout << "  Resolved " << resolved << "/" << total << " functions";
  if (missing > 0)
  {
    cout << " (missing:";
    for (int i = 0; i < total; i++)
    {
      if (! funcs[i].ptr)
        cout << " " << funcs[i].name;
    }
    cout << ")";
  }
  cout << "\n";
}


void OobDds::PrintInfo() const
{
  if (! p_GetDDSInfo)
  {
    cout << "OOB DLL: GetDDSInfo not available\n";
    return;
  }

  DDSInfo info;
  memset(&info, 0, sizeof(info));
  p_GetDDSInfo(&info);

  cout << "OOB DLL info\n";
  cout << "  Version      " << info.versionString << "\n";
  cout << "  Threads      " << info.noOfThreads << "\n";
  cout << "  Cores        " << info.numCores << "\n";
  cout << "  System       " << info.systemString << "\n";
}


// --- Wrapper method implementations ---
// Each checks its pointer and returns a sensible default if missing.

void OobDds::SetMaxThreads(int userThreads)
{
  if (p_SetMaxThreads) p_SetMaxThreads(userThreads);
}

int OobDds::SetThreading(int code)
{
  if (p_SetThreading) return p_SetThreading(code);
  return RETURN_UNKNOWN_FAULT;
}

void OobDds::SetResources(int maxMemoryMB, int maxThreads)
{
  if (p_SetResources) p_SetResources(maxMemoryMB, maxThreads);
}

void OobDds::FreeMemory()
{
  if (p_FreeMemory) p_FreeMemory();
}

int OobDds::SolveBoard(struct deal dl, int target, int solutions,
  int mode, struct futureTricks * futp, int threadIndex)
{
  if (p_SolveBoard) return p_SolveBoard(dl, target, solutions, mode, futp, threadIndex);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::SolveBoardPBN(struct dealPBN dlpbn, int target, int solutions,
  int mode, struct futureTricks * futp, int thrId)
{
  if (p_SolveBoardPBN) return p_SolveBoardPBN(dlpbn, target, solutions, mode, futp, thrId);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::CalcDDtable(struct ddTableDeal tableDeal,
  struct ddTableResults * tablep)
{
  if (p_CalcDDtable) return p_CalcDDtable(tableDeal, tablep);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::CalcDDtablePBN(struct ddTableDealPBN tableDealPBN,
  struct ddTableResults * tablep)
{
  if (p_CalcDDtablePBN) return p_CalcDDtablePBN(tableDealPBN, tablep);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::CalcAllTables(struct ddTableDeals * dealsp, int mode,
  int trumpFilter[5], struct ddTablesRes * resp,
  struct allParResults * presp)
{
  if (p_CalcAllTables) return p_CalcAllTables(dealsp, mode, trumpFilter, resp, presp);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::CalcAllTablesPBN(struct ddTableDealsPBN * dealsp, int mode,
  int trumpFilter[5], struct ddTablesRes * resp,
  struct allParResults * presp)
{
  if (p_CalcAllTablesPBN) return p_CalcAllTablesPBN(dealsp, mode, trumpFilter, resp, presp);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::CalcAllTablesPBNRaw(void * dealsp, int mode,
  int trumpFilter[5], void * resp, void * presp)
{
  if (! p_CalcAllTablesPBN)
    return RETURN_UNKNOWN_FAULT;

  typedef int (STDCALL *pfn_raw)(void *, int, int[5], void *, void *);
  auto fn = reinterpret_cast<pfn_raw>(
    reinterpret_cast<void(*)()>(p_CalcAllTablesPBN));
  return fn(dealsp, mode, trumpFilter, resp, presp);
}

int OobDds::CalcAllTablesPBNx(int numDeals, struct ddTableDealPBN dealCards[],
  int mode, int trumpFilter[5], struct ddTableResults results[],
  struct parResults par[])
{
  if (p_CalcAllTablesPBNx) return p_CalcAllTablesPBNx(numDeals, dealCards, mode, trumpFilter, results, par);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::SolveAllBoards(struct boardsPBN * bop, struct solvedBoards * solvedp)
{
  if (p_SolveAllBoards) return p_SolveAllBoards(bop, solvedp);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::SolveAllBoardsBin(struct boards * bop, struct solvedBoards * solvedp)
{
  if (p_SolveAllBoardsBin) return p_SolveAllBoardsBin(bop, solvedp);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::SolveAllChunks(struct boardsPBN * bop, struct solvedBoards * solvedp,
  int chunkSize)
{
  if (p_SolveAllChunks) return p_SolveAllChunks(bop, solvedp, chunkSize);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::SolveAllChunksBin(struct boards * bop, struct solvedBoards * solvedp,
  int chunkSize)
{
  if (p_SolveAllChunksBin) return p_SolveAllChunksBin(bop, solvedp, chunkSize);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::SolveAllChunksPBN(struct boardsPBN * bop, struct solvedBoards * solvedp,
  int chunkSize)
{
  if (p_SolveAllChunksPBN) return p_SolveAllChunksPBN(bop, solvedp, chunkSize);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::Par(struct ddTableResults * tablep, struct parResults * presp,
  int vulnerable)
{
  if (p_Par) return p_Par(tablep, presp, vulnerable);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::CalcPar(struct ddTableDeal tableDeal, int vulnerable,
  struct ddTableResults * tablep, struct parResults * presp)
{
  if (p_CalcPar) return p_CalcPar(tableDeal, vulnerable, tablep, presp);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::CalcParPBN(struct ddTableDealPBN tableDealPBN,
  struct ddTableResults * tablep, int vulnerable,
  struct parResults * presp)
{
  if (p_CalcParPBN) return p_CalcParPBN(tableDealPBN, tablep, vulnerable, presp);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::SidesPar(struct ddTableResults * tablep,
  struct parResultsDealer sidesRes[2], int vulnerable)
{
  if (p_SidesPar) return p_SidesPar(tablep, sidesRes, vulnerable);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::DealerPar(struct ddTableResults * tablep,
  struct parResultsDealer * presp, int dealer, int vulnerable)
{
  if (p_DealerPar) return p_DealerPar(tablep, presp, dealer, vulnerable);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::DealerParBin(struct ddTableResults * tablep,
  struct parResultsMaster * presp, int dealer, int vulnerable)
{
  if (p_DealerParBin) return p_DealerParBin(tablep, presp, dealer, vulnerable);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::SidesParBin(struct ddTableResults * tablep,
  struct parResultsMaster sidesRes[2], int vulnerable)
{
  if (p_SidesParBin) return p_SidesParBin(tablep, sidesRes, vulnerable);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::ConvertToDealerTextFormat(struct parResultsMaster * pres, char * resp)
{
  if (p_ConvertToDealerTextFormat) return p_ConvertToDealerTextFormat(pres, resp);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::ConvertToSidesTextFormat(struct parResultsMaster * pres,
  struct parTextResults * resp)
{
  if (p_ConvertToSidesTextFormat) return p_ConvertToSidesTextFormat(pres, resp);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::AnalysePlayBin(struct deal dl, struct playTraceBin play,
  struct solvedPlay * solved, int thrId)
{
  if (p_AnalysePlayBin) return p_AnalysePlayBin(dl, play, solved, thrId);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::AnalysePlayPBN(struct dealPBN dlPBN, struct playTracePBN playPBN,
  struct solvedPlay * solvedp, int thrId)
{
  if (p_AnalysePlayPBN) return p_AnalysePlayPBN(dlPBN, playPBN, solvedp, thrId);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::AnalyseAllPlaysBin(struct boards * bop, struct playTracesBin * plp,
  struct solvedPlays * solvedp, int chunkSize)
{
  if (p_AnalyseAllPlaysBin) return p_AnalyseAllPlaysBin(bop, plp, solvedp, chunkSize);
  return RETURN_UNKNOWN_FAULT;
}

int OobDds::AnalyseAllPlaysPBN(struct boardsPBN * bopPBN,
  struct playTracesPBN * plpPBN, struct solvedPlays * solvedp, int chunkSize)
{
  if (p_AnalyseAllPlaysPBN) return p_AnalyseAllPlaysPBN(bopPBN, plpPBN, solvedp, chunkSize);
  return RETURN_UNKNOWN_FAULT;
}

void OobDds::GetDDSInfo(struct DDSInfo * info)
{
  if (p_GetDDSInfo) p_GetDDSInfo(info);
}

void OobDds::ErrorMessage(int code, char line[80])
{
  if (p_ErrorMessage) p_ErrorMessage(code, line);
}
