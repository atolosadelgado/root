// Author: Alvaro Tolosa-Delgado CERN 07/2023
// Author: Jorge Agramunt Ros IFIC(Valencia,Spain) 07/2023

/*************************************************************************
 * Copyright (C) 1995-2023, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include <iostream>

#include <TFile.h>
#include <TTree.h>
#include <TTreeReader.h>
#include <TTreeReaderValue.h>

#include "data2Tree.hpp"

void writeTree()
{
   std::unique_ptr<TFile> ofile { TFile::Open("testFile.root", "recreate") };
   if ( nullptr == ofile ) {
      std::cerr << " File not open." << std::endl;
      return;
   }

   std::unique_ptr<TTree> myTree = std::make_unique<TTree>("myTree", "");
   myDetectorData obj_for_branch1;
   myTree->Branch("branch1.", &obj_for_branch1);

   myDetectorData obj_for_branch2;
   myTree->Branch("branch2.", &obj_for_branch2);

   for (int i = 0; i < 10; ++i) {
      //-- if i is even, fill branch2 and set branch1's entry to zero
      if (i % 2 == 0) {
         obj_for_branch1.clear();
         obj_for_branch2.time = i + 5;
         obj_for_branch2.energy = 2 * i + 5;
         obj_for_branch2.detectorID = 3 * i + 5;
         myTree->Fill();

      }
      //-- if i is odd, we do the opposite
      else {
         obj_for_branch2.clear();
         obj_for_branch1.time = i + 1;
         obj_for_branch1.energy = 2 * i + 1;
         obj_for_branch1.detectorID = 3 * i + 1;
         myTree->Fill();
      }
   }

   myTree->Print();

   ofile->Write(); // This write the files and the TTree

}

