/*************************************************************************
 * Copyright (C) 1995-2021, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "RConfigure.h" // R__USE_IMT
#include "ROOT/RDataSource.hxx"
#include "ROOT/RDF/GraphNode.hxx"
#include "ROOT/InternalTreeUtils.hxx" // GetTreeFullPaths
#include "ROOT/RDF/RActionBase.hxx"
#include "ROOT/RDF/RDefineBase.hxx"
#include "ROOT/RDF/RFilterBase.hxx"
#include "ROOT/RDF/RLoopManager.hxx"
#include "ROOT/RDF/RRangeBase.hxx"
#include "ROOT/RDF/RVariationBase.hxx"
#include "ROOT/RLogger.hxx"
#include "RtypesCore.h" // Long64_t
#include "TStopwatch.h"
#include "TBranchElement.h"
#include "TBranchObject.h"
#include "TChain.h"
#include "TEntryList.h"
#include "TFile.h"
#include "TFriendElement.h"
#include "TROOT.h" // IsImplicitMTEnabled
#include "TTreeReader.h"
#include "TTree.h" // For MaxTreeSizeRAII. Revert when #6640 will be solved.

#ifdef R__USE_IMT
#include "ROOT/TThreadExecutor.hxx"
#include "ROOT/TTreeProcessorMT.hxx"
#include "ROOT/RSlotStack.hxx"
#endif

#ifdef R__HAS_ROOT7
#include "ROOT/RNTuple.hxx"
#include "ROOT/RPageStorage.hxx"
#include "ROOT/RNTupleDS.hxx"
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>
#include <set>
#include <limits> // For MaxTreeSizeRAII. Revert when #6640 will be solved.

using namespace ROOT::Detail::RDF;
using namespace ROOT::Internal::RDF;

namespace {
/// A helper function that returns all RDF code that is currently scheduled for just-in-time compilation.
/// This allows different RLoopManager instances to share these data.
/// We want RLoopManagers to be able to add their code to a global "code to execute via cling",
/// so that, lazily, we can jit everything that's needed by all RDFs in one go, which is potentially
/// much faster than jitting each RLoopManager's code separately.
std::string &GetCodeToJit()
{
   static std::string code;
   return code;
}

bool ContainsLeaf(const std::set<TLeaf *> &leaves, TLeaf *leaf)
{
   return (leaves.find(leaf) != leaves.end());
}

///////////////////////////////////////////////////////////////////////////////
/// This overload does not check whether the leaf/branch is already in bNamesReg. In case this is a friend leaf/branch,
/// `allowDuplicates` controls whether we add both `friendname.bname` and `bname` or just the shorter version.
void InsertBranchName(std::set<std::string> &bNamesReg, ColumnNames_t &bNames, const std::string &branchName,
                             const std::string &friendName, bool allowDuplicates)
{
   if (!friendName.empty()) {
      // In case of a friend tree, users might prepend its name/alias to the branch names
      const auto friendBName = friendName + "." + branchName;
      if (bNamesReg.insert(friendBName).second)
         bNames.push_back(friendBName);
   }

   if (allowDuplicates || friendName.empty()) {
      if (bNamesReg.insert(branchName).second)
         bNames.push_back(branchName);
   }
}

///////////////////////////////////////////////////////////////////////////////
/// This overload makes sure that the TLeaf has not been already inserted.
void InsertBranchName(std::set<std::string> &bNamesReg, ColumnNames_t &bNames, const std::string &branchName,
                             const std::string &friendName, std::set<TLeaf *> &foundLeaves, TLeaf *leaf,
                             bool allowDuplicates)
{
   const bool canAdd = allowDuplicates ? true : !ContainsLeaf(foundLeaves, leaf);
   if (!canAdd) {
      return;
   }

   InsertBranchName(bNamesReg, bNames, branchName, friendName, allowDuplicates);

   foundLeaves.insert(leaf);
}

void ExploreBranch(TTree &t, std::set<std::string> &bNamesReg, ColumnNames_t &bNames, TBranch *b,
                          std::string prefix, std::string &friendName, bool allowDuplicates)
{
   for (auto sb : *b->GetListOfBranches()) {
      TBranch *subBranch = static_cast<TBranch *>(sb);
      auto subBranchName = std::string(subBranch->GetName());
      auto fullName = prefix + subBranchName;

      std::string newPrefix;
      if (!prefix.empty())
         newPrefix = fullName + ".";

      ExploreBranch(t, bNamesReg, bNames, subBranch, newPrefix, friendName, allowDuplicates);

      auto branchDirectlyFromTree = t.GetBranch(fullName.c_str());
      if (!branchDirectlyFromTree)
         branchDirectlyFromTree = t.FindBranch(fullName.c_str()); // try harder
      if (branchDirectlyFromTree)
         InsertBranchName(bNamesReg, bNames, std::string(branchDirectlyFromTree->GetFullName()), friendName,
                          allowDuplicates);

      if (bNamesReg.find(subBranchName) == bNamesReg.end() && t.GetBranch(subBranchName.c_str()))
         InsertBranchName(bNamesReg, bNames, subBranchName, friendName, allowDuplicates);
   }
}

void GetBranchNamesImpl(TTree &t, std::set<std::string> &bNamesReg, ColumnNames_t &bNames,
                               std::set<TTree *> &analysedTrees, std::string &friendName, bool allowDuplicates)
{
   std::set<TLeaf *> foundLeaves;
   if (!analysedTrees.insert(&t).second) {
      return;
   }

   const auto branches = t.GetListOfBranches();
   // Getting the branches here triggered the read of the first file of the chain if t is a chain.
   // We check if a tree has been successfully read, otherwise we throw (see ROOT-9984) to avoid further
   // operations
   if (!t.GetTree()) {
      std::string err("GetBranchNames: error in opening the tree ");
      err += t.GetName();
      throw std::runtime_error(err);
   }
   if (branches) {
      for (auto b : *branches) {
         TBranch *branch = static_cast<TBranch *>(b);
         const auto branchName = std::string(branch->GetName());
         if (branch->IsA() == TBranch::Class()) {
            // Leaf list
            auto listOfLeaves = branch->GetListOfLeaves();
            if (listOfLeaves->GetEntriesUnsafe() == 1) {
               auto leaf = static_cast<TLeaf *>(listOfLeaves->UncheckedAt(0));
               InsertBranchName(bNamesReg, bNames, branchName, friendName, foundLeaves, leaf, allowDuplicates);
            }

            for (auto leaf : *listOfLeaves) {
               auto castLeaf = static_cast<TLeaf *>(leaf);
               const auto leafName = std::string(leaf->GetName());
               const auto fullName = branchName + "." + leafName;
               InsertBranchName(bNamesReg, bNames, fullName, friendName, foundLeaves, castLeaf, allowDuplicates);
            }
         } else if (branch->IsA() == TBranchObject::Class()) {
            // TBranchObject
            ExploreBranch(t, bNamesReg, bNames, branch, branchName + ".", friendName, allowDuplicates);
            InsertBranchName(bNamesReg, bNames, branchName, friendName, allowDuplicates);
         } else {
            // TBranchElement
            // Check if there is explicit or implicit dot in the name

            bool dotIsImplied = false;
            auto be = dynamic_cast<TBranchElement *>(b);
            if (!be)
               throw std::runtime_error("GetBranchNames: unsupported branch type");
            // TClonesArray (3) and STL collection (4)
            if (be->GetType() == 3 || be->GetType() == 4)
               dotIsImplied = true;

            if (dotIsImplied || branchName.back() == '.')
               ExploreBranch(t, bNamesReg, bNames, branch, "", friendName, allowDuplicates);
            else
               ExploreBranch(t, bNamesReg, bNames, branch, branchName + ".", friendName, allowDuplicates);

            InsertBranchName(bNamesReg, bNames, branchName, friendName, allowDuplicates);
         }
      }
   }

   // The list of friends needs to be accessed via GetTree()->GetListOfFriends()
   // (and not via GetListOfFriends() directly), otherwise when `t` is a TChain we
   // might not recover the list correctly (https://github.com/root-project/root/issues/6741).
   auto friendTrees = t.GetTree()->GetListOfFriends();

   if (!friendTrees)
      return;

   for (auto friendTreeObj : *friendTrees) {
      auto friendTree = ((TFriendElement *)friendTreeObj)->GetTree();

      std::string frName;
      auto alias = t.GetFriendAlias(friendTree);
      if (alias != nullptr)
         frName = std::string(alias);
      else
         frName = std::string(friendTree->GetName());

      GetBranchNamesImpl(*friendTree, bNamesReg, bNames, analysedTrees, frName, allowDuplicates);
   }
}

void ThrowIfNSlotsChanged(unsigned int nSlots)
{
   const auto currentSlots = RDFInternal::GetNSlots();
   if (currentSlots != nSlots) {
      std::string msg = "RLoopManager::Run: when the RDataFrame was constructed the number of slots required was " +
                        std::to_string(nSlots) + ", but when starting the event loop it was " +
                        std::to_string(currentSlots) + ".";
      if (currentSlots > nSlots)
         msg += " Maybe EnableImplicitMT() was called after the RDataFrame was constructed?";
      else
         msg += " Maybe DisableImplicitMT() was called after the RDataFrame was constructed?";
      throw std::runtime_error(msg);
   }
}

/**
\struct MaxTreeSizeRAII
\brief Scope-bound change of `TTree::fgMaxTreeSize`.

This RAII object stores the current value result of `TTree::GetMaxTreeSize`,
changes it to maximum at construction time and restores it back at destruction
time. Needed for issue #6523 and should be reverted when #6640 will be solved.
*/
struct MaxTreeSizeRAII {
   Long64_t fOldMaxTreeSize;

   MaxTreeSizeRAII() : fOldMaxTreeSize(TTree::GetMaxTreeSize())
   {
      TTree::SetMaxTreeSize(std::numeric_limits<Long64_t>::max());
   }

   ~MaxTreeSizeRAII() { TTree::SetMaxTreeSize(fOldMaxTreeSize); }
};

struct DatasetLogInfo {
   std::string fDataSet;
   ULong64_t fRangeStart;
   ULong64_t fRangeEnd;
   unsigned int fSlot;
};

std::string LogRangeProcessing(const DatasetLogInfo &info)
{
   std::stringstream msg;
   msg << "Processing " << info.fDataSet << ": entry range [" << info.fRangeStart << "," << info.fRangeEnd - 1
       << "], using slot " << info.fSlot << " in thread " << std::this_thread::get_id() << '.';
   return msg.str();
}

DatasetLogInfo TreeDatasetLogInfo(const TTreeReader &r, unsigned int slot)
{
   const auto tree = r.GetTree();
   const auto chain = dynamic_cast<TChain *>(tree);
   std::string what;
   if (chain) {
      auto files = chain->GetListOfFiles();
      std::vector<std::string> treeNames;
      std::vector<std::string> fileNames;
      for (TObject *f : *files) {
         treeNames.emplace_back(f->GetName());
         fileNames.emplace_back(f->GetTitle());
      }
      what = "trees {";
      for (const auto &t : treeNames) {
         what += t + ",";
      }
      what.back() = '}';
      what += " in files {";
      for (const auto &f : fileNames) {
         what += f + ",";
      }
      what.back() = '}';
   } else {
      assert(tree != nullptr); // to make clang-tidy happy
      const auto treeName = tree->GetName();
      what = std::string("tree \"") + treeName + "\"";
      const auto file = tree->GetCurrentFile();
      if (file)
         what += std::string(" in file \"") + file->GetName() + "\"";
   }
   const auto entryRange = r.GetEntriesRange();
   const ULong64_t end = entryRange.second == -1ll ? tree->GetEntries() : entryRange.second;
   return {std::move(what), static_cast<ULong64_t>(entryRange.first), end, slot};
}

auto MakeDatasetColReadersKey(const std::string &colName, const std::type_info &ti)
{
   // We use a combination of column name and column type name as the key because in some cases we might end up
   // with concrete readers that use different types for the same column, e.g. std::vector and RVec here:
   //    df.Sum<vector<int>>("stdVectorBranch");
   //    df.Sum<RVecI>("stdVectorBranch");
   return colName + ':' + ti.name();
}
} // anonymous namespace

namespace ROOT {
namespace Detail {
namespace RDF {

/// A RAII object that calls RLoopManager::CleanUpTask at destruction
struct RCallCleanUpTask {
   RLoopManager &fLoopManager;
   unsigned int fArg;
   TTreeReader *fReader;

   RCallCleanUpTask(RLoopManager &lm, unsigned int arg = 0u, TTreeReader *reader = nullptr)
      : fLoopManager(lm), fArg(arg), fReader(reader)
   {
   }
   ~RCallCleanUpTask() { fLoopManager.CleanUpTask(fReader, fArg); }
};

} // namespace RDF
} // namespace Detail
} // namespace ROOT

///////////////////////////////////////////////////////////////////////////////
/// Get all the branches names, including the ones of the friend trees
ColumnNames_t ROOT::Internal::RDF::GetBranchNames(TTree &t, bool allowDuplicates)
{
   std::set<std::string> bNamesSet;
   ColumnNames_t bNames;
   std::set<TTree *> analysedTrees;
   std::string emptyFrName = "";
   GetBranchNamesImpl(t, bNamesSet, bNames, analysedTrees, emptyFrName, allowDuplicates);
   return bNames;
}

RLoopManager::RLoopManager(TTree *tree, const ColumnNames_t &defaultBranches)
   : fTree(std::shared_ptr<TTree>(tree, [](TTree *) {})), fDefaultColumns(defaultBranches),
     fNSlots(RDFInternal::GetNSlots()),
     fLoopType(ROOT::IsImplicitMTEnabled() ? ELoopType::kROOTFilesMT : ELoopType::kROOTFiles),
     fNewSampleNotifier(fNSlots), fSampleInfos(fNSlots), fDatasetColumnReaders(fNSlots)
{
}

RLoopManager::RLoopManager(std::unique_ptr<TTree> tree, const ColumnNames_t &defaultBranches)
   : fTree(std::move(tree)),
     fDefaultColumns(defaultBranches),
     fNSlots(RDFInternal::GetNSlots()),
     fLoopType(ROOT::IsImplicitMTEnabled() ? ELoopType::kROOTFilesMT : ELoopType::kROOTFiles),
     fNewSampleNotifier(fNSlots),
     fSampleInfos(fNSlots),
     fDatasetColumnReaders(fNSlots)
{
}

RLoopManager::RLoopManager(ULong64_t nEmptyEntries)
   : fEmptyEntryRange(0, nEmptyEntries),
     fNSlots(RDFInternal::GetNSlots()),
     fLoopType(ROOT::IsImplicitMTEnabled() ? ELoopType::kNoFilesMT : ELoopType::kNoFiles),
     fNewSampleNotifier(fNSlots),
     fSampleInfos(fNSlots),
     fDatasetColumnReaders(fNSlots)
{
}

RLoopManager::RLoopManager(std::unique_ptr<RDataSource> ds, const ColumnNames_t &defaultBranches)
   : fDefaultColumns(defaultBranches), fNSlots(RDFInternal::GetNSlots()),
     fLoopType(ROOT::IsImplicitMTEnabled() ? ELoopType::kDataSourceMT : ELoopType::kDataSource),
     fDataSource(std::move(ds)), fNewSampleNotifier(fNSlots), fSampleInfos(fNSlots), fDatasetColumnReaders(fNSlots)
{
   fDataSource->SetNSlots(fNSlots);
}

RLoopManager::RLoopManager(ROOT::RDF::Experimental::RDatasetSpec &&spec)
   : fNSlots(RDFInternal::GetNSlots()),
     fLoopType(ROOT::IsImplicitMTEnabled() ? ELoopType::kROOTFilesMT : ELoopType::kROOTFiles),
     fNewSampleNotifier(fNSlots),
     fSampleInfos(fNSlots),
     fDatasetColumnReaders(fNSlots)
{
   ChangeSpec(std::move(spec));
}

/**
 * @brief Changes the internal TTree held by the RLoopManager.
 *
 * @warning This method may lead to potentially dangerous interactions if used
 *     after the construction of the RDataFrame. Changing the specification makes
 *     sense *if and only if* the schema of the dataset is *unchanged*, i.e. the
 *     new specification refers to exactly the same number of columns, with the
 *     same names and types. The actual use case of this method is moving the
 *     processing of the same RDataFrame to a different range of entries of the
 *     same dataset (which may be stored in a different set of files).
 *
 * @param spec The specification of the dataset to be adopted.
 */
void RLoopManager::ChangeSpec(ROOT::RDF::Experimental::RDatasetSpec &&spec)
{
   // Change the range of entries to be processed
   fBeginEntry = spec.GetEntryRangeBegin();
   fEndEntry = spec.GetEntryRangeEnd();

   // Store the samples
   fSamples = spec.MoveOutSamples();
   fSampleMap.clear();

   // Create the internal main chain
   auto chain = ROOT::Internal::TreeUtils::MakeChainForMT();
   for (auto &sample : fSamples) {
      const auto &trees = sample.GetTreeNames();
      const auto &files = sample.GetFileNameGlobs();
      for (std::size_t i = 0ul; i < files.size(); ++i) {
         // We need to use `<filename>?#<treename>` as an argument to TChain::Add
         // (see https://github.com/root-project/root/pull/8820 for why)
         const auto fullpath = files[i] + "?#" + trees[i];
         chain->Add(fullpath.c_str());
         // ...but instead we use `<filename>/<treename>` as a sample ID (cannot
         // change this easily because of backward compatibility: the sample ID
         // is exposed to users via RSampleInfo and DefinePerSample).
         const auto sampleId = files[i] + '/' + trees[i];
         fSampleMap.insert({sampleId, &sample});
      }
   }
   SetTree(std::move(chain));

   // Create friends from the specification and connect them to the main chain
   const auto &friendInfo = spec.GetFriendInfo();
   fFriends = ROOT::Internal::TreeUtils::MakeFriends(friendInfo);
   for (std::size_t i = 0ul; i < fFriends.size(); i++) {
      const auto &thisFriendAlias = friendInfo.fFriendNames[i].second;
      fTree->AddFriend(fFriends[i].get(), thisFriendAlias.c_str());
   }
}

/// Run event loop with no source files, in parallel.
void RLoopManager::RunEmptySourceMT()
{
#ifdef R__USE_IMT
   ROOT::Internal::RSlotStack slotStack(fNSlots);
   // Working with an empty tree.
   // Evenly partition the entries according to fNSlots. Produce around 2 tasks per slot.
   const auto nEmptyEntries = GetNEmptyEntries();
   const auto nEntriesPerSlot = nEmptyEntries / (fNSlots * 2);
   auto remainder = nEmptyEntries % (fNSlots * 2);
   std::vector<std::pair<ULong64_t, ULong64_t>> entryRanges;
   ULong64_t begin = fEmptyEntryRange.first;
   while (begin < fEmptyEntryRange.second) {
      ULong64_t end = begin + nEntriesPerSlot;
      if (remainder > 0) {
         ++end;
         --remainder;
      }
      entryRanges.emplace_back(begin, end);
      begin = end;
   }

   // Each task will generate a subrange of entries
   auto genFunction = [this, &slotStack](const std::pair<ULong64_t, ULong64_t> &range) {
      ROOT::Internal::RSlotStackRAII slotRAII(slotStack);
      auto slot = slotRAII.fSlot;
      RCallCleanUpTask cleanup(*this, slot);
      InitNodeSlots(nullptr, slot);
      R__LOG_DEBUG(0, RDFLogChannel()) << LogRangeProcessing({"an empty source", range.first, range.second, slot});
      try {
         UpdateSampleInfo(slot, range);
         for (auto currEntry = range.first; currEntry < range.second; ++currEntry) {
            RunAndCheckFilters(slot, currEntry);
         }
      } catch (...) {
         // Error might throw in experiment frameworks like CMSSW
         std::cerr << "RDataFrame::Run: event loop was interrupted\n";
         throw;
      }
   };

   ROOT::TThreadExecutor pool;
   pool.Foreach(genFunction, entryRanges);

#endif // not implemented otherwise
}

/// Run event loop with no source files, in sequence.
void RLoopManager::RunEmptySource()
{
   InitNodeSlots(nullptr, 0);
   R__LOG_DEBUG(0, RDFLogChannel()) << LogRangeProcessing(
      {"an empty source", fEmptyEntryRange.first, fEmptyEntryRange.second, 0u});
   RCallCleanUpTask cleanup(*this);
   try {
      UpdateSampleInfo(/*slot*/ 0, fEmptyEntryRange);
      for (ULong64_t currEntry = fEmptyEntryRange.first;
           currEntry < fEmptyEntryRange.second && fNStopsReceived < fNChildren; ++currEntry) {
         RunAndCheckFilters(0, currEntry);
      }
   } catch (...) {
      std::cerr << "RDataFrame::Run: event loop was interrupted\n";
      throw;
   }
}

/// Run event loop over one or multiple ROOT files, in parallel.
void RLoopManager::RunTreeProcessorMT()
{
#ifdef R__USE_IMT
   if (fEndEntry == fBeginEntry) // empty range => no work needed
      return;
   ROOT::Internal::RSlotStack slotStack(fNSlots);
   const auto &entryList = fTree->GetEntryList() ? *fTree->GetEntryList() : TEntryList();
   auto tp = (fBeginEntry != 0 || fEndEntry != std::numeric_limits<Long64_t>::max())
                ? std::make_unique<ROOT::TTreeProcessorMT>(*fTree, fNSlots, std::make_pair(fBeginEntry, fEndEntry))
                : std::make_unique<ROOT::TTreeProcessorMT>(*fTree, entryList, fNSlots);

   std::atomic<ULong64_t> entryCount(0ull);

   tp->Process([this, &slotStack, &entryCount](TTreeReader &r) -> void {
      ROOT::Internal::RSlotStackRAII slotRAII(slotStack);
      auto slot = slotRAII.fSlot;
      RCallCleanUpTask cleanup(*this, slot, &r);
      InitNodeSlots(&r, slot);
      R__LOG_DEBUG(0, RDFLogChannel()) << LogRangeProcessing(TreeDatasetLogInfo(r, slot));
      const auto entryRange = r.GetEntriesRange(); // we trust TTreeProcessorMT to call SetEntriesRange
      const auto nEntries = entryRange.second - entryRange.first;
      auto count = entryCount.fetch_add(nEntries);
      try {
         // recursive call to check filters and conditionally execute actions
         while (r.Next()) {
            if (fNewSampleNotifier.CheckFlag(slot)) {
               UpdateSampleInfo(slot, r);
            }
            RunAndCheckFilters(slot, count++);
         }
      } catch (...) {
         std::cerr << "RDataFrame::Run: event loop was interrupted\n";
         throw;
      }
      // fNStopsReceived < fNChildren is always true at the moment as we don't support event loop early quitting in
      // multi-thread runs, but it costs nothing to be safe and future-proof in case we add support for that later.
      if (r.GetEntryStatus() != TTreeReader::kEntryBeyondEnd && fNStopsReceived < fNChildren) {
         // something went wrong in the TTreeReader event loop
         throw std::runtime_error("An error was encountered while processing the data. TTreeReader status code is: " +
                                  std::to_string(r.GetEntryStatus()));
      }
   });
#endif // no-op otherwise (will not be called)
}

/// Run event loop over one or multiple ROOT files, in sequence.
void RLoopManager::RunTreeReader()
{
   TTreeReader r(fTree.get(), fTree->GetEntryList());
   if (0 == fTree->GetEntriesFast() || fBeginEntry == fEndEntry)
      return;
   // Apply the range if there is any
   // In case of a chain with a total of N entries, calling SetEntriesRange(N + 1, ...) does not error out
   // This is a bug, reported here: https://github.com/root-project/root/issues/10774
   if (fBeginEntry != 0 || fEndEntry != std::numeric_limits<Long64_t>::max())
      if (r.SetEntriesRange(fBeginEntry, fEndEntry) != TTreeReader::kEntryValid)
         throw std::logic_error("Something went wrong in initializing the TTreeReader.");

   RCallCleanUpTask cleanup(*this, 0u, &r);
   InitNodeSlots(&r, 0);
   R__LOG_DEBUG(0, RDFLogChannel()) << LogRangeProcessing(TreeDatasetLogInfo(r, 0u));

   // recursive call to check filters and conditionally execute actions
   // in the non-MT case processing can be stopped early by ranges, hence the check on fNStopsReceived
   try {
      while (r.Next() && fNStopsReceived < fNChildren) {
         if (fNewSampleNotifier.CheckFlag(0)) {
            UpdateSampleInfo(/*slot*/0, r);
         }
         RunAndCheckFilters(0, r.GetCurrentEntry());
      }
   } catch (...) {
      std::cerr << "RDataFrame::Run: event loop was interrupted\n";
      throw;
   }
   if (r.GetEntryStatus() != TTreeReader::kEntryBeyondEnd && fNStopsReceived < fNChildren) {
      // something went wrong in the TTreeReader event loop
      throw std::runtime_error("An error was encountered while processing the data. TTreeReader status code is: " +
                               std::to_string(r.GetEntryStatus()));
   }
}

/// Run event loop over data accessed through a DataSource, in sequence.
void RLoopManager::RunDataSource()
{
   assert(fDataSource != nullptr);
   fDataSource->Initialize();
   auto ranges = fDataSource->GetEntryRanges();
   while (!ranges.empty() && fNStopsReceived < fNChildren) {
      InitNodeSlots(nullptr, 0u);
      fDataSource->InitSlot(0u, 0ull);
      RCallCleanUpTask cleanup(*this);
      try {
         for (const auto &range : ranges) {
            const auto start = range.first;
            const auto end = range.second;
            R__LOG_DEBUG(0, RDFLogChannel()) << LogRangeProcessing({fDataSource->GetLabel(), start, end, 0u});
            for (auto entry = start; entry < end && fNStopsReceived < fNChildren; ++entry) {
               if (fDataSource->SetEntry(0u, entry)) {
                  RunAndCheckFilters(0u, entry);
               }
            }
         }
      } catch (...) {
         std::cerr << "RDataFrame::Run: event loop was interrupted\n";
         throw;
      }
      fDataSource->FinalizeSlot(0u);
      ranges = fDataSource->GetEntryRanges();
   }
   fDataSource->Finalize();
}

/// Run event loop over data accessed through a DataSource, in parallel.
void RLoopManager::RunDataSourceMT()
{
#ifdef R__USE_IMT
   assert(fDataSource != nullptr);
   ROOT::Internal::RSlotStack slotStack(fNSlots);
   ROOT::TThreadExecutor pool;

   // Each task works on a subrange of entries
   auto runOnRange = [this, &slotStack](const std::pair<ULong64_t, ULong64_t> &range) {
      ROOT::Internal::RSlotStackRAII slotRAII(slotStack);
      const auto slot = slotRAII.fSlot;
      InitNodeSlots(nullptr, slot);
      RCallCleanUpTask cleanup(*this, slot);
      fDataSource->InitSlot(slot, range.first);
      const auto start = range.first;
      const auto end = range.second;
      R__LOG_DEBUG(0, RDFLogChannel()) << LogRangeProcessing({fDataSource->GetLabel(), start, end, slot});
      try {
         for (auto entry = start; entry < end; ++entry) {
            if (fDataSource->SetEntry(slot, entry)) {
               RunAndCheckFilters(slot, entry);
            }
         }
      } catch (...) {
         std::cerr << "RDataFrame::Run: event loop was interrupted\n";
         throw;
      }
      fDataSource->FinalizeSlot(slot);
   };

   fDataSource->Initialize();
   auto ranges = fDataSource->GetEntryRanges();
   while (!ranges.empty()) {
      pool.Foreach(runOnRange, ranges);
      ranges = fDataSource->GetEntryRanges();
   }
   fDataSource->Finalize();
#endif // not implemented otherwise (never called)
}

/// Execute actions and make sure named filters are called for each event.
/// Named filters must be called even if the analysis logic would not require it, lest they report confusing results.
void RLoopManager::RunAndCheckFilters(unsigned int slot, Long64_t entry)
{
   // data-block callbacks run before the rest of the graph
   if (fNewSampleNotifier.CheckFlag(slot)) {
      for (auto &callback : fSampleCallbacks)
         callback.second(slot, fSampleInfos[slot]);
      fNewSampleNotifier.UnsetFlag(slot);
   }

   for (auto *actionPtr : fBookedActions)
      actionPtr->Run(slot, entry);
   for (auto *namedFilterPtr : fBookedNamedFilters)
      namedFilterPtr->CheckFilters(slot, entry);
   for (auto &callback : fCallbacksEveryNEvents)
      callback(slot);
}

/// Build TTreeReaderValues for all nodes
/// This method loops over all filters, actions and other booked objects and
/// calls their `InitSlot` method, to get them ready for running a task.
void RLoopManager::InitNodeSlots(TTreeReader *r, unsigned int slot)
{
   SetupSampleCallbacks(r, slot);
   for (auto *ptr : fBookedActions)
      ptr->InitSlot(r, slot);
   for (auto *ptr : fBookedFilters)
      ptr->InitSlot(r, slot);
   for (auto *ptr : fBookedDefines)
      ptr->InitSlot(r, slot);
   for (auto *ptr : fBookedVariations)
      ptr->InitSlot(r, slot);

   for (auto &callback : fCallbacksOnce)
      callback(slot);
}

void RLoopManager::SetupSampleCallbacks(TTreeReader *r, unsigned int slot) {
   if (r != nullptr) {
      // we need to set a notifier so that we run the callbacks every time we switch to a new TTree
      // `PrependLink` inserts this notifier into the TTree/TChain's linked list of notifiers
      fNewSampleNotifier.GetChainNotifyLink(slot).PrependLink(*r->GetTree());
   }
   // Whatever the data source, initially set the "new data block" flag:
   // - for TChains, this ensures that we don't skip the first data block because
   //   the correct tree is already loaded
   // - for RDataSources and empty sources, which currently don't have data blocks, this
   //   ensures that we run once per task
   fNewSampleNotifier.SetFlag(slot);
}

void RLoopManager::UpdateSampleInfo(unsigned int slot, const std::pair<ULong64_t, ULong64_t> &range) {
   fSampleInfos[slot] = RSampleInfo(
      "Empty source, range: {" + std::to_string(range.first) + ", " + std::to_string(range.second) + "}", range);
}

void RLoopManager::UpdateSampleInfo(unsigned int slot, TTreeReader &r) {
   // one GetTree to retrieve the TChain, another to retrieve the underlying TTree
   auto *tree = r.GetTree()->GetTree();
   R__ASSERT(tree != nullptr);
   const std::string treename = ROOT::Internal::TreeUtils::GetTreeFullPaths(*tree)[0];
   auto *file = tree->GetCurrentFile();
   const std::string fname = file != nullptr ? file->GetName() : "#inmemorytree#";

   std::pair<Long64_t, Long64_t> range = r.GetEntriesRange();
   R__ASSERT(range.first >= 0);
   if (range.second == -1) {
      range.second = tree->GetEntries(); // convert '-1', i.e. 'until the end', to the actual entry number
   }
   const std::string &id = fname + '/' + treename;
   fSampleInfos[slot] = fSampleMap.empty() ? RSampleInfo(id, range) : RSampleInfo(id, range, fSampleMap[id]);
}

/// Initialize all nodes of the functional graph before running the event loop.
/// This method is called once per event-loop and performs generic initialization
/// operations that do not depend on the specific processing slot (i.e. operations
/// that are common for all threads).
void RLoopManager::InitNodes()
{
   EvalChildrenCounts();
   for (auto *filter : fBookedFilters)
      filter->InitNode();
   for (auto *range : fBookedRanges)
      range->InitNode();
   for (auto *ptr : fBookedActions)
      ptr->Initialize();
}

/// Perform clean-up operations. To be called at the end of each event loop.
void RLoopManager::CleanUpNodes()
{
   fMustRunNamedFilters = false;

   // forget RActions and detach TResultProxies
   for (auto *ptr : fBookedActions)
      ptr->Finalize();

   fRunActions.insert(fRunActions.begin(), fBookedActions.begin(), fBookedActions.end());
   fBookedActions.clear();

   // reset children counts
   fNChildren = 0;
   fNStopsReceived = 0;
   for (auto *ptr : fBookedFilters)
      ptr->ResetChildrenCount();
   for (auto *ptr : fBookedRanges)
      ptr->ResetChildrenCount();

   fCallbacksEveryNEvents.clear();
   fCallbacksOnce.clear();
}

/// Perform clean-up operations. To be called at the end of each task execution.
void RLoopManager::CleanUpTask(TTreeReader *r, unsigned int slot)
{
   if (r != nullptr)
      fNewSampleNotifier.GetChainNotifyLink(slot).RemoveLink(*r->GetTree());
   for (auto *ptr : fBookedActions)
      ptr->FinalizeSlot(slot);
   for (auto *ptr : fBookedFilters)
      ptr->FinalizeSlot(slot);
   for (auto *ptr : fBookedDefines)
      ptr->FinalizeSlot(slot);

   if (fLoopType == ELoopType::kROOTFiles || fLoopType == ELoopType::kROOTFilesMT) {
      // we are reading from a tree/chain and we need to re-create the RTreeColumnReaders at every task
      // because the TTreeReader object changes at every task
      for (auto &v : fDatasetColumnReaders[slot])
         v.second.reset();
   }
}

/// Add RDF nodes that require just-in-time compilation to the computation graph.
/// This method also clears the contents of GetCodeToJit().
void RLoopManager::Jit()
{
   // TODO this should be a read lock unless we find GetCodeToJit non-empty
   R__LOCKGUARD(gROOTMutex);

   const std::string code = std::move(GetCodeToJit());
   if (code.empty()) {
      R__LOG_INFO(RDFLogChannel()) << "Nothing to jit and execute.";
      return;
   }

   TStopwatch s;
   s.Start();
   RDFInternal::InterpreterCalc(code, "RLoopManager::Run");
   s.Stop();
   R__LOG_INFO(RDFLogChannel()) << "Just-in-time compilation phase completed"
                                << (s.RealTime() > 1e-3 ? " in " + std::to_string(s.RealTime()) + " seconds."
                                                        : " in less than 1ms.");
}

/// Trigger counting of number of children nodes for each node of the functional graph.
/// This is done once before starting the event loop. Each action sends an `increase children count` signal
/// upstream, which is propagated until RLoopManager. Each time a node receives the signal, in increments its
/// children counter. Each node only propagates the signal once, even if it receives it multiple times.
/// Named filters also send an `increase children count` signal, just like actions, as they always execute during
/// the event loop so the graph branch they belong to must count as active even if it does not end in an action.
void RLoopManager::EvalChildrenCounts()
{
   for (auto *actionPtr : fBookedActions)
      actionPtr->TriggerChildrenCount();
   for (auto *namedFilterPtr : fBookedNamedFilters)
      namedFilterPtr->TriggerChildrenCount();
}

/// Start the event loop with a different mechanism depending on IMT/no IMT, data source/no data source.
/// Also perform a few setup and clean-up operations (jit actions if necessary, clear booked actions after the loop...).
/// The jitting phase is skipped if the `jit` parameter is `false` (unsafe, use with care).
void RLoopManager::Run(bool jit)
{
   // Change value of TTree::GetMaxTreeSize only for this scope. Revert when #6640 will be solved.
   MaxTreeSizeRAII ctxtmts;

   R__LOG_INFO(RDFLogChannel()) << "Starting event loop number " << fNRuns << '.';

   ThrowIfNSlotsChanged(GetNSlots());

   if (jit)
      Jit();

   InitNodes();

   // Exceptions can occur during the event loop. In order to ensure proper cleanup of nodes
   // we use RAII: even in case of an exception, the destructor of the object is invoked and
   // all the cleanup takes place.
   class NodesCleanerRAII {
      RLoopManager &fRLM;

   public:
      NodesCleanerRAII(RLoopManager &thisRLM) : fRLM(thisRLM) {}
      ~NodesCleanerRAII() { fRLM.CleanUpNodes(); }
   };

   NodesCleanerRAII runKeeper(*this);

   TStopwatch s;
   s.Start();

   switch (fLoopType) {
   case ELoopType::kNoFilesMT: RunEmptySourceMT(); break;
   case ELoopType::kROOTFilesMT: RunTreeProcessorMT(); break;
   case ELoopType::kDataSourceMT: RunDataSourceMT(); break;
   case ELoopType::kNoFiles: RunEmptySource(); break;
   case ELoopType::kROOTFiles: RunTreeReader(); break;
   case ELoopType::kDataSource: RunDataSource(); break;
   }
   s.Stop();

   fNRuns++;

   R__LOG_INFO(RDFLogChannel()) << "Finished event loop number " << fNRuns - 1 << " (" << s.CpuTime() << "s CPU, "
                                << s.RealTime() << "s elapsed).";
}

/// Return the list of default columns -- empty if none was provided when constructing the RDataFrame
const ColumnNames_t &RLoopManager::GetDefaultColumnNames() const
{
   return fDefaultColumns;
}

TTree *RLoopManager::GetTree() const
{
   return fTree.get();
}

void RLoopManager::Register(RDFInternal::RActionBase *actionPtr)
{
   fBookedActions.emplace_back(actionPtr);
   AddSampleCallback(actionPtr, actionPtr->GetSampleCallback());
}

void RLoopManager::Deregister(RDFInternal::RActionBase *actionPtr)
{
   RDFInternal::Erase(actionPtr, fRunActions);
   RDFInternal::Erase(actionPtr, fBookedActions);
   fSampleCallbacks.erase(actionPtr);
}

void RLoopManager::Register(RFilterBase *filterPtr)
{
   fBookedFilters.emplace_back(filterPtr);
   if (filterPtr->HasName()) {
      fBookedNamedFilters.emplace_back(filterPtr);
      fMustRunNamedFilters = true;
   }
}

void RLoopManager::Deregister(RFilterBase *filterPtr)
{
   RDFInternal::Erase(filterPtr, fBookedFilters);
   RDFInternal::Erase(filterPtr, fBookedNamedFilters);
}

void RLoopManager::Register(RRangeBase *rangePtr)
{
   fBookedRanges.emplace_back(rangePtr);
}

void RLoopManager::Deregister(RRangeBase *rangePtr)
{
   RDFInternal::Erase(rangePtr, fBookedRanges);
}

void RLoopManager::Register(RDefineBase *ptr)
{
   fBookedDefines.emplace_back(ptr);
}

void RLoopManager::Deregister(RDefineBase *ptr)
{
   RDFInternal::Erase(ptr, fBookedDefines);
   fSampleCallbacks.erase(ptr);
}

void RLoopManager::Register(RDFInternal::RVariationBase *v)
{
   fBookedVariations.emplace_back(v);
}

void RLoopManager::Deregister(RDFInternal::RVariationBase *v)
{
   RDFInternal::Erase(v, fBookedVariations);
}

// dummy call, end of recursive chain of calls
bool RLoopManager::CheckFilters(unsigned int, Long64_t)
{
   return true;
}

/// Call `FillReport` on all booked filters
void RLoopManager::Report(ROOT::RDF::RCutFlowReport &rep) const
{
   for (const auto *fPtr : fBookedNamedFilters)
      fPtr->FillReport(rep);
}

void RLoopManager::SetTree(std::shared_ptr<TTree> tree)
{
   fTree = std::move(tree);

   TChain *ch = nullptr;
   if ((ch = dynamic_cast<TChain *>(fTree.get())))
      fNoCleanupNotifier.RegisterChain(*ch);
}

void RLoopManager::ToJitExec(const std::string &code) const
{
   R__LOCKGUARD(gROOTMutex);
   GetCodeToJit().append(code);
}

void RLoopManager::RegisterCallback(ULong64_t everyNEvents, std::function<void(unsigned int)> &&f)
{
   if (everyNEvents == 0ull)
      fCallbacksOnce.emplace_back(std::move(f), fNSlots);
   else
      fCallbacksEveryNEvents.emplace_back(everyNEvents, std::move(f), fNSlots);
}

std::vector<std::string> RLoopManager::GetFiltersNames()
{
   std::vector<std::string> filters;
   for (auto *filter : fBookedFilters) {
      auto name = (filter->HasName() ? filter->GetName() : "Unnamed Filter");
      filters.push_back(name);
   }
   return filters;
}

std::vector<RNodeBase *> RLoopManager::GetGraphEdges() const
{
   std::vector<RNodeBase *> nodes(fBookedFilters.size() + fBookedRanges.size());
   auto it = std::copy(fBookedFilters.begin(), fBookedFilters.end(), nodes.begin());
   std::copy(fBookedRanges.begin(), fBookedRanges.end(), it);
   return nodes;
}

std::vector<RDFInternal::RActionBase *> RLoopManager::GetAllActions() const
{
   std::vector<RDFInternal::RActionBase *> actions(fBookedActions.size() + fRunActions.size());
   auto it = std::copy(fBookedActions.begin(), fBookedActions.end(), actions.begin());
   std::copy(fRunActions.begin(), fRunActions.end(), it);
   return actions;
}

std::shared_ptr<ROOT::Internal::RDF::GraphDrawing::GraphNode> RLoopManager::GetGraph(
   std::unordered_map<void *, std::shared_ptr<ROOT::Internal::RDF::GraphDrawing::GraphNode>> &visitedMap)
{
   // If there is already a node for the RLoopManager return it. If there is not, return a new one.
   auto duplicateRLoopManagerIt = visitedMap.find((void *)this);
   if (duplicateRLoopManagerIt != visitedMap.end())
      return duplicateRLoopManagerIt->second;

   std::string name;
   if (fDataSource) {
      name = fDataSource->GetLabel();
   } else if (fTree) {
      name = fTree->GetName();
   } else {
      name = "Empty source\\nEntries: " + std::to_string(GetNEmptyEntries());
   }
   auto thisNode = std::make_shared<ROOT::Internal::RDF::GraphDrawing::GraphNode>(
      name, visitedMap.size(), ROOT::Internal::RDF::GraphDrawing::ENodeType::kRoot);
   visitedMap[(void *)this] = thisNode;
   return thisNode;
}

////////////////////////////////////////////////////////////////////////////
/// Return all valid TTree::Branch names (caching results for subsequent calls).
/// Never use fBranchNames directy, always request it through this method.
const ColumnNames_t &RLoopManager::GetBranchNames()
{
   if (fValidBranchNames.empty() && fTree) {
      fValidBranchNames = RDFInternal::GetBranchNames(*fTree, /*allowRepetitions=*/true);
   }
   return fValidBranchNames;
}

/// Return true if AddDataSourceColumnReaders was called for column name col.
bool RLoopManager::HasDataSourceColumnReaders(const std::string &col, const std::type_info &ti) const
{
   const auto key = MakeDatasetColReadersKey(col, ti);
   assert(fDataSource != nullptr);
   // since data source column readers are always added for all slots at the same time,
   // if the reader is present for slot 0 we have it for all other slots as well.
   return fDatasetColumnReaders[0].find(key) != fDatasetColumnReaders[0].end();
}

void RLoopManager::AddDataSourceColumnReaders(const std::string &col,
                                              std::vector<std::unique_ptr<RColumnReaderBase>> &&readers,
                                              const std::type_info &ti)
{
   const auto key = MakeDatasetColReadersKey(col, ti);
   assert(fDataSource != nullptr && !HasDataSourceColumnReaders(col, ti));
   assert(readers.size() == fNSlots);

   for (auto slot = 0u; slot < fNSlots; ++slot) {
      fDatasetColumnReaders[slot][key] = std::move(readers[slot]);
   }
}

// Differently from AddDataSourceColumnReaders, this can be called from multiple threads concurrently
/// \brief Register a new RTreeColumnReader with this RLoopManager.
/// \return A shared pointer to the inserted column reader.
RColumnReaderBase *RLoopManager::AddTreeColumnReader(unsigned int slot, const std::string &col,
                                                     std::unique_ptr<RColumnReaderBase> &&reader,
                                                     const std::type_info &ti)
{
   auto &readers = fDatasetColumnReaders[slot];
   const auto key = MakeDatasetColReadersKey(col, ti);
   // if a reader for this column and this slot was already there, we are doing something wrong
   assert(readers.find(key) == readers.end() || readers[key] == nullptr);
   auto *rptr = reader.get();
   readers[key] = std::move(reader);
   return rptr;
}

RColumnReaderBase *
RLoopManager::GetDatasetColumnReader(unsigned int slot, const std::string &col, const std::type_info &ti) const
{
   const auto key = MakeDatasetColReadersKey(col, ti);
   auto it = fDatasetColumnReaders[slot].find(key);
   if (it != fDatasetColumnReaders[slot].end())
      return it->second.get();
   else
      return nullptr;
}

void RLoopManager::AddSampleCallback(void *nodePtr, SampleCallback_t &&callback)
{
   if (callback)
      fSampleCallbacks.insert({nodePtr, std::move(callback)});
}

void RLoopManager::SetEmptyEntryRange(std::pair<ULong64_t, ULong64_t> &&newRange)
{
   fEmptyEntryRange = std::move(newRange);
}

/**
 * \brief Helper function to open a file (or the first file from a glob).
 * This function is used at construction time of an RDataFrame, to check the
 * concrete type of the dataset stored inside the file.
 */
std::unique_ptr<TFile> OpenFileWithSanityChecks(std::string_view fileNameGlob)
{
   bool fileIsGlob = [&fileNameGlob]() {
      const std::vector<std::string_view> wildcards = {"[", "]", "*", "?"}; // Wildcards accepted by TChain::Add
      return std::any_of(wildcards.begin(), wildcards.end(),
                         [&fileNameGlob](const auto &wc) { return fileNameGlob.find(wc) != std::string_view::npos; });
   }();

   // Open first file in case of glob, suppose all files in the glob use the same data format
   std::string fileToOpen{fileIsGlob ? ROOT::Internal::TreeUtils::ExpandGlob(std::string{fileNameGlob})[0]
                                     : fileNameGlob};

   ::TDirectory::TContext ctxt; // Avoid changing gDirectory;
   std::unique_ptr<TFile> inFile{TFile::Open(fileToOpen.c_str(), "READ_WITHOUT_GLOBAL_REGISTRATION")};
   if (!inFile || inFile->IsZombie())
      throw std::invalid_argument("RDataFrame: could not open file \"" + fileToOpen + "\".");

   return inFile;
}

std::shared_ptr<ROOT::Detail::RDF::RLoopManager>
ROOT::Detail::RDF::CreateLMFromTTree(std::string_view datasetName, std::string_view fileNameGlob,
                                     const ROOT::RDF::ColumnNames_t &defaultColumns, bool checkFile)
{
   // Introduce the same behaviour as in CreateLMFromFile for consistency.
   // Creating an RDataFrame with a non-existing file will throw early rather
   // than wait for the start of the graph execution.
   if (checkFile) {
      OpenFileWithSanityChecks(fileNameGlob);
   }
   std::string datasetNameInt{datasetName};
   std::string fileNameGlobInt{fileNameGlob};
   auto chain = ROOT::Internal::TreeUtils::MakeChainForMT(datasetNameInt.c_str());
   chain->Add(fileNameGlobInt.c_str());
   auto lm = std::make_shared<ROOT::Detail::RDF::RLoopManager>(std::move(chain), defaultColumns);
   return lm;
}

std::shared_ptr<ROOT::Detail::RDF::RLoopManager>
ROOT::Detail::RDF::CreateLMFromTTree(std::string_view datasetName, const std::vector<std::string> &fileNameGlobs,
                                     const std::vector<std::string> &defaultColumns, bool checkFile)
{
   // Introduce the same behaviour as in CreateLMFromFile for consistency.
   // Creating an RDataFrame with a non-existing file will throw early rather
   // than wait for the start of the graph execution.
   if (checkFile) {
      OpenFileWithSanityChecks(fileNameGlobs[0]);
   }
   std::string treeNameInt(datasetName);
   auto chain = ROOT::Internal::TreeUtils::MakeChainForMT(treeNameInt);
   for (auto &f : fileNameGlobs)
      chain->Add(f.c_str());
   auto lm = std::make_shared<ROOT::Detail::RDF::RLoopManager>(std::move(chain), defaultColumns);
   return lm;
}

#ifdef R__HAS_ROOT7
std::shared_ptr<ROOT::Detail::RDF::RLoopManager>
ROOT::Detail::RDF::CreateLMFromRNTuple(std::string_view datasetName, std::string_view fileNameGlob,
                                       const ROOT::RDF::ColumnNames_t &defaultColumns)
{
   auto dataSource = std::make_unique<ROOT::Experimental::RNTupleDS>(datasetName, fileNameGlob);
   auto lm = std::make_shared<ROOT::Detail::RDF::RLoopManager>(std::move(dataSource), defaultColumns);
   return lm;
}

std::shared_ptr<ROOT::Detail::RDF::RLoopManager>
ROOT::Detail::RDF::CreateLMFromRNTuple(std::string_view datasetName, const std::vector<std::string> &fileNameGlobs,
                                       const ROOT::RDF::ColumnNames_t &defaultColumns)
{
   auto dataSource = std::make_unique<ROOT::Experimental::RNTupleDS>(datasetName, fileNameGlobs);
   auto lm = std::make_shared<ROOT::Detail::RDF::RLoopManager>(std::move(dataSource), defaultColumns);
   return lm;
}

std::shared_ptr<ROOT::Detail::RDF::RLoopManager>
ROOT::Detail::RDF::CreateLMFromFile(std::string_view datasetName, std::string_view fileNameGlob,
                                    const ROOT::RDF::ColumnNames_t &defaultColumns)
{

   auto inFile = OpenFileWithSanityChecks(fileNameGlob);

   if (inFile->Get<TTree>(datasetName.data())) {
      return CreateLMFromTTree(datasetName, fileNameGlob, defaultColumns, /*checkFile=*/false);
   } else if (inFile->Get<ROOT::Experimental::RNTuple>(datasetName.data())) {
      return CreateLMFromRNTuple(datasetName, fileNameGlob, defaultColumns);
   }

   throw std::invalid_argument("RDataFrame: unsupported data format for dataset \"" + std::string(datasetName) +
                               "\" in file \"" + inFile->GetName() + "\".");
}

std::shared_ptr<ROOT::Detail::RDF::RLoopManager>
ROOT::Detail::RDF::CreateLMFromFile(std::string_view datasetName, const std::vector<std::string> &fileNameGlobs,
                                    const ROOT::RDF::ColumnNames_t &defaultColumns)
{

   auto inFile = OpenFileWithSanityChecks(fileNameGlobs[0]);

   if (inFile->Get<TTree>(datasetName.data())) {
      return CreateLMFromTTree(datasetName, fileNameGlobs, defaultColumns, /*checkFile=*/false);
   } else if (inFile->Get<ROOT::Experimental::RNTuple>(datasetName.data())) {
      return CreateLMFromRNTuple(datasetName, fileNameGlobs, defaultColumns);
   }

   throw std::invalid_argument("RDataFrame: unsupported data format for dataset \"" + std::string(datasetName) +
                               "\" in file \"" + inFile->GetName() + "\".");
}
#endif
