// @(#)root/treeplayer:$Id$
// Author: Markus Frank 20/05/2005

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TRefArrayProxy
#define ROOT_TRefArrayProxy
#include "TRefProxy.h"

//______________________________________________________________________________
//
// TRefArrayProxy is a container proxy, which allows to access references stored
// in a TRefArray from TTree::Draw
//
//______________________________________________________________________________
class TRefArrayProxy : public TRefProxy  {
public:
   // The implicit's constructor and destructor have the correct implementation.

   // TVirtualRefProxy overload: Clone the reference proxy (virtual constructor)
   TVirtualRefProxy* Clone() const override        { return new TRefArrayProxy(*this);}
   // TVirtualRefProxy overload: Flag to indicate if this is a container reference
   bool HasCounter()  const override             { return true;                    }
   // TVirtualRefProxy overload: Access referenced object(-data)
   void* GetObject(TFormLeafInfoReference* info, void* data, Int_t instance) override;
   // TVirtualRefProxy overload: Access to container size (if container reference (ie TRefArray) etc)
   Int_t  GetCounterValue(TFormLeafInfoReference* info, void *data) override;
};
#endif // ROOT_TRefArrayProxy
