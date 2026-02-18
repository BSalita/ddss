/*
   DDS, a bridge double dummy solver.

   OOB (out-of-box) DDS DLL loader.
   Dynamically loads an unmodified upstream DDS DLL at runtime
   for correctness verification against the ddss fork.
*/

#ifndef DTEST_OOB_DDS_H
#define DTEST_OOB_DDS_H

#include <string>
#include "../include/dll.h"

#ifdef _WIN32
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif


// Function pointer typedefs for the full DDS API (30 functions).
// Calling convention matches dll.h: STDCALL on Windows, default elsewhere.

// Resource management
typedef void (STDCALL *pfn_SetMaxThreads)(int);
typedef int  (STDCALL *pfn_SetThreading)(int);
typedef void (STDCALL *pfn_SetResources)(int, int);
typedef void (STDCALL *pfn_FreeMemory)();

// Single-board solving
typedef int (STDCALL *pfn_SolveBoard)(
  struct deal, int, int, int, struct futureTricks *, int);
typedef int (STDCALL *pfn_SolveBoardPBN)(
  struct dealPBN, int, int, int, struct futureTricks *, int);

// DD tables (single deal)
typedef int (STDCALL *pfn_CalcDDtable)(
  struct ddTableDeal, struct ddTableResults *);
typedef int (STDCALL *pfn_CalcDDtablePBN)(
  struct ddTableDealPBN, struct ddTableResults *);

// DD tables (batch)
typedef int (STDCALL *pfn_CalcAllTables)(
  struct ddTableDeals *, int, int[5],
  struct ddTablesRes *, struct allParResults *);
typedef int (STDCALL *pfn_CalcAllTablesPBN)(
  struct ddTableDealsPBN *, int, int[5],
  struct ddTablesRes *, struct allParResults *);
typedef int (STDCALL *pfn_CalcAllTablesPBNx)(
  int, struct ddTableDealPBN[], int, int[5],
  struct ddTableResults[], struct parResults[]);

// Batch board solving
typedef int (STDCALL *pfn_SolveAllBoards)(
  struct boardsPBN *, struct solvedBoards *);
typedef int (STDCALL *pfn_SolveAllBoardsBin)(
  struct boards *, struct solvedBoards *);
typedef int (STDCALL *pfn_SolveAllChunks)(
  struct boardsPBN *, struct solvedBoards *, int);
typedef int (STDCALL *pfn_SolveAllChunksBin)(
  struct boards *, struct solvedBoards *, int);
typedef int (STDCALL *pfn_SolveAllChunksPBN)(
  struct boardsPBN *, struct solvedBoards *, int);

// Par score
typedef int (STDCALL *pfn_Par)(
  struct ddTableResults *, struct parResults *, int);
typedef int (STDCALL *pfn_CalcPar)(
  struct ddTableDeal, int, struct ddTableResults *, struct parResults *);
typedef int (STDCALL *pfn_CalcParPBN)(
  struct ddTableDealPBN, struct ddTableResults *, int, struct parResults *);
typedef int (STDCALL *pfn_SidesPar)(
  struct ddTableResults *, struct parResultsDealer[2], int);
typedef int (STDCALL *pfn_DealerPar)(
  struct ddTableResults *, struct parResultsDealer *, int, int);
typedef int (STDCALL *pfn_DealerParBin)(
  struct ddTableResults *, struct parResultsMaster *, int, int);
typedef int (STDCALL *pfn_SidesParBin)(
  struct ddTableResults *, struct parResultsMaster[2], int);
typedef int (STDCALL *pfn_ConvertToDealerTextFormat)(
  struct parResultsMaster *, char *);
typedef int (STDCALL *pfn_ConvertToSidesTextFormat)(
  struct parResultsMaster *, struct parTextResults *);

// Play analysis
typedef int (STDCALL *pfn_AnalysePlayBin)(
  struct deal, struct playTraceBin, struct solvedPlay *, int);
typedef int (STDCALL *pfn_AnalysePlayPBN)(
  struct dealPBN, struct playTracePBN, struct solvedPlay *, int);
typedef int (STDCALL *pfn_AnalyseAllPlaysBin)(
  struct boards *, struct playTracesBin *, struct solvedPlays *, int);
typedef int (STDCALL *pfn_AnalyseAllPlaysPBN)(
  struct boardsPBN *, struct playTracesPBN *, struct solvedPlays *, int);

// Info and error
typedef void (STDCALL *pfn_GetDDSInfo)(struct DDSInfo *);
typedef void (STDCALL *pfn_ErrorMessage)(int, char[80]);


class OobDds
{
public:
  OobDds();
  ~OobDds();

  // Load the DLL from the given path. Returns true on success.
  bool Load(const std::string& path);

  // True if at least CalcDDtablePBN was resolved.
  bool IsLoaded() const;

  // Path that was loaded.
  const std::string& Path() const;

  // Print which functions resolved and which didn't.
  void PrintStatus() const;

  // Convenience: print OOB DDS version info.
  void PrintInfo() const;

  // --- Wrapper methods matching the DDS API ---

  // Resource management
  void SetMaxThreads(int userThreads);
  int  SetThreading(int code);
  void SetResources(int maxMemoryMB, int maxThreads);
  void FreeMemory();

  // Single-board solving
  int SolveBoard(struct deal dl, int target, int solutions,
    int mode, struct futureTricks * futp, int threadIndex);
  int SolveBoardPBN(struct dealPBN dlpbn, int target, int solutions,
    int mode, struct futureTricks * futp, int thrId);

  // DD tables (single deal)
  int CalcDDtable(struct ddTableDeal tableDeal,
    struct ddTableResults * tablep);
  int CalcDDtablePBN(struct ddTableDealPBN tableDealPBN,
    struct ddTableResults * tablep);

  // DD tables (batch)
  int CalcAllTables(struct ddTableDeals * dealsp, int mode,
    int trumpFilter[5], struct ddTablesRes * resp,
    struct allParResults * presp);
  int CalcAllTablesPBN(struct ddTableDealsPBN * dealsp, int mode,
    int trumpFilter[5], struct ddTablesRes * resp,
    struct allParResults * presp);
  int CalcAllTablesPBNx(int numDeals, struct ddTableDealPBN dealCards[],
    int mode, int trumpFilter[5], struct ddTableResults results[],
    struct parResults par[]);

  // Raw batch call that bypasses ddss-sized struct typedefs.
  // Allows passing OOB-compatible (upstream-sized) batch structs
  // directly to the OOB DLL's CalcAllTablesPBN without ABI mismatch.
  int CalcAllTablesPBNRaw(void * dealsp, int mode,
    int trumpFilter[5], void * resp, void * presp);

  // Batch board solving
  int SolveAllBoards(struct boardsPBN * bop, struct solvedBoards * solvedp);
  int SolveAllBoardsBin(struct boards * bop, struct solvedBoards * solvedp);
  int SolveAllChunks(struct boardsPBN * bop, struct solvedBoards * solvedp,
    int chunkSize);
  int SolveAllChunksBin(struct boards * bop, struct solvedBoards * solvedp,
    int chunkSize);
  int SolveAllChunksPBN(struct boardsPBN * bop, struct solvedBoards * solvedp,
    int chunkSize);

  // Par score
  int Par(struct ddTableResults * tablep, struct parResults * presp,
    int vulnerable);
  int CalcPar(struct ddTableDeal tableDeal, int vulnerable,
    struct ddTableResults * tablep, struct parResults * presp);
  int CalcParPBN(struct ddTableDealPBN tableDealPBN,
    struct ddTableResults * tablep, int vulnerable,
    struct parResults * presp);
  int SidesPar(struct ddTableResults * tablep,
    struct parResultsDealer sidesRes[2], int vulnerable);
  int DealerPar(struct ddTableResults * tablep,
    struct parResultsDealer * presp, int dealer, int vulnerable);
  int DealerParBin(struct ddTableResults * tablep,
    struct parResultsMaster * presp, int dealer, int vulnerable);
  int SidesParBin(struct ddTableResults * tablep,
    struct parResultsMaster sidesRes[2], int vulnerable);
  int ConvertToDealerTextFormat(struct parResultsMaster * pres, char * resp);
  int ConvertToSidesTextFormat(struct parResultsMaster * pres,
    struct parTextResults * resp);

  // Play analysis
  int AnalysePlayBin(struct deal dl, struct playTraceBin play,
    struct solvedPlay * solved, int thrId);
  int AnalysePlayPBN(struct dealPBN dlPBN, struct playTracePBN playPBN,
    struct solvedPlay * solvedp, int thrId);
  int AnalyseAllPlaysBin(struct boards * bop, struct playTracesBin * plp,
    struct solvedPlays * solvedp, int chunkSize);
  int AnalyseAllPlaysPBN(struct boardsPBN * bopPBN,
    struct playTracesPBN * plpPBN, struct solvedPlays * solvedp,
    int chunkSize);

  // Info and error
  void GetDDSInfo(struct DDSInfo * info);
  void ErrorMessage(int code, char line[80]);

private:
#ifdef _WIN32
  HMODULE handle_;
#else
  void * handle_;
#endif
  std::string path_;

  // Function pointers (nullptr if not resolved)
  pfn_SetMaxThreads          p_SetMaxThreads;
  pfn_SetThreading           p_SetThreading;
  pfn_SetResources           p_SetResources;
  pfn_FreeMemory             p_FreeMemory;
  pfn_SolveBoard             p_SolveBoard;
  pfn_SolveBoardPBN          p_SolveBoardPBN;
  pfn_CalcDDtable            p_CalcDDtable;
  pfn_CalcDDtablePBN         p_CalcDDtablePBN;
  pfn_CalcAllTables          p_CalcAllTables;
  pfn_CalcAllTablesPBN       p_CalcAllTablesPBN;
  pfn_CalcAllTablesPBNx      p_CalcAllTablesPBNx;
  pfn_SolveAllBoards         p_SolveAllBoards;
  pfn_SolveAllBoardsBin      p_SolveAllBoardsBin;
  pfn_SolveAllChunks         p_SolveAllChunks;
  pfn_SolveAllChunksBin      p_SolveAllChunksBin;
  pfn_SolveAllChunksPBN      p_SolveAllChunksPBN;
  pfn_Par                    p_Par;
  pfn_CalcPar                p_CalcPar;
  pfn_CalcParPBN             p_CalcParPBN;
  pfn_SidesPar               p_SidesPar;
  pfn_DealerPar              p_DealerPar;
  pfn_DealerParBin           p_DealerParBin;
  pfn_SidesParBin            p_SidesParBin;
  pfn_ConvertToDealerTextFormat p_ConvertToDealerTextFormat;
  pfn_ConvertToSidesTextFormat  p_ConvertToSidesTextFormat;
  pfn_AnalysePlayBin         p_AnalysePlayBin;
  pfn_AnalysePlayPBN         p_AnalysePlayPBN;
  pfn_AnalyseAllPlaysBin     p_AnalyseAllPlaysBin;
  pfn_AnalyseAllPlaysPBN     p_AnalyseAllPlaysPBN;
  pfn_GetDDSInfo             p_GetDDSInfo;
  pfn_ErrorMessage           p_ErrorMessage;

  void ClearPointers();

  template<typename T>
  T ResolveSym(const char * name);
};

#endif
