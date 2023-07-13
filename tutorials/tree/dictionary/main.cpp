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

void writeTree();
void readTree();

int main()
{
   std::cerr << "Starting writeTree()..." << std::endl;
   writeTree();
   std::cerr << "Starting writeTree()... Done! " << std::endl;
   std::cerr << "Starting readTree()..." << std::endl;
   readTree();
   std::cerr << "Starting readTree()... Done! " << std::endl;

   return 0;
}

