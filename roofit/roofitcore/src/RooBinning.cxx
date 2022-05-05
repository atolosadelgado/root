/*****************************************************************************
 * Project: RooFit                                                           *
 * Package: RooFitCore                                                       *
 * @(#)root/roofitcore:$Id$
 * Authors:                                                                  *
 *   WV, Wouter Verkerke, UC Santa Barbara, verkerke@slac.stanford.edu       *
 *   DK, David Kirkby,    UC Irvine,         dkirkby@uci.edu                 *
 *                                                                           *
 * Copyright (c) 2000-2005, Regents of the University of California          *
 *                          and Stanford University. All rights reserved.    *
 *                                                                           *
 * Redistribution and use in source and binary forms,                        *
 * with or without modification, are permitted according to the terms        *
 * listed in LICENSE (http://roofit.sourceforge.net/license.txt)             *
 *****************************************************************************/

/**
\file RooBinning.cxx
\class RooBinning
\ingroup Roofitcore

Class RooBinning is an implements RooAbsBinning in terms
of an array of boundary values, posing no constraints on the choice
of binning, thus allowing variable bin sizes. Various methods allow
the user to add single bin boundaries, mirrored pairs, or sets of
uniformly spaced boundaries.
**/

#include "Riostream.h"
#include "RooBinning.h"
#include "RooDouble.h"
#include "RooAbsPdf.h"
#include "RooRealVar.h"
#include "RooNumber.h"
#include "RooMsgService.h"
#include "TBuffer.h"
#include "TList.h"

#include <algorithm>
#include <cmath>

using namespace std;

ClassImp(RooBinning);
;


////////////////////////////////////////////////////////////////////////////////
/// Constructor for an initially empty binning defining the range [xlo,xhi]

RooBinning::RooBinning(Double_t xlo, Double_t xhi, const char* name) :
  RooAbsBinning(name),
  _xlo(0), _xhi(0), _ownBoundLo(true), _ownBoundHi(true),
  _array(0), _blo(0)
{
  setRange(xlo,xhi);
}

////////////////////////////////////////////////////////////////////////////////
/// Constructor for a uniform binning in 'nbins' bins in the range [xlo,xhi]

RooBinning::RooBinning(Int_t nbins, Double_t xlo, Double_t xhi, const char* name) :
  RooAbsBinning(name),
  _xlo(0), _xhi(0), _ownBoundLo(true), _ownBoundHi(true),
  _array(0), _blo(0)
{
  _boundaries.reserve(1 + nbins);
  setRange(xlo, xhi);
  addUniform(nbins, xlo, xhi);
}

////////////////////////////////////////////////////////////////////////////////
/// Constructor for a binning in the range[xlo,xhi] with 'nbins' bin boundaries listed
/// array 'boundaries'

RooBinning::RooBinning(Int_t nbins, const Double_t* boundaries, const char* name) :
  RooAbsBinning(name),
  _xlo(0), _xhi(0), _ownBoundLo(true), _ownBoundHi(true),
  _array(0), _blo(0)
{
  // Variable bin size constructor
  _boundaries.reserve(1 + nbins);
  setRange(boundaries[0], boundaries[nbins]);
  while (nbins--) addBoundary(boundaries[nbins]);
}

////////////////////////////////////////////////////////////////////////////////
/// Copy constructor

RooBinning::RooBinning(const RooBinning& other, const char* name) :
  RooAbsBinning(name), _xlo(other._xlo), _xhi(other._xhi),
  _ownBoundLo(other._ownBoundLo), _ownBoundHi(other._ownBoundHi),
  _nbins(other._nbins), _boundaries(other._boundaries), _array(0),
  _blo(other._blo)
{
}

////////////////////////////////////////////////////////////////////////////////
/// Destructor

RooBinning::~RooBinning()
{
  delete[] _array;
}

////////////////////////////////////////////////////////////////////////////////
/// Add bin boundary at given value

bool RooBinning::addBoundary(Double_t boundary)
{
  std::vector<Double_t>::iterator it =
      std::lower_bound(_boundaries.begin(), _boundaries.end(), boundary);
  if (_boundaries.end() != it && *it == boundary) {
    // If boundary previously existed as range delimiter,
    //                    convert to regular boundary now
    if (boundary == _xlo) _ownBoundLo = false;
    if (boundary == _xhi) _ownBoundHi = false;
    return false;
  }
  // Add a new boundary
  _boundaries.insert(it, boundary);
  updateBinCount();
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// Add pair of boundaries: one at 'boundary' and one at 2*mirrorPoint-boundary

void RooBinning::addBoundaryPair(Double_t boundary, Double_t mirrorPoint)
{
  addBoundary(boundary);
  addBoundary(2. * mirrorPoint - boundary);
}

////////////////////////////////////////////////////////////////////////////////
/// Remove boundary at given value

bool RooBinning::removeBoundary(Double_t boundary)
{
  std::vector<Double_t>::iterator it = std::lower_bound(_boundaries.begin(),
      _boundaries.end(), boundary);
  if  (_boundaries.end() != it && *it == boundary) {
    _boundaries.erase(it);
    // if some moron deletes the boundaries corresponding to the current
    // range, we need to make sure that we do not get into an undefined state,
    // so _xlo and _xhi need to be set to some valid values
    if (_boundaries.empty()) {
      _xlo = _xhi = 0.;
    } else {
      if (boundary == _xlo) _xlo = _boundaries.front();
      if (boundary == _xhi) _xhi = _boundaries.back();
    }
    updateBinCount();
    return false;
  }
  // Return error status - no boundary found
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// Check if boundary exists at given value

bool RooBinning::hasBoundary(Double_t boundary)
{
  return std::binary_search(_boundaries.begin(), _boundaries.end(), boundary);
}

////////////////////////////////////////////////////////////////////////////////
/// Add array of nbins uniformly sized bins in range [xlo,xhi]

void RooBinning::addUniform(Int_t nbins, Double_t xlo, Double_t xhi)
{
  _boundaries.reserve(_boundaries.size() + nbins + 1);
  for (Int_t i = 0; i <= nbins; ++i)
    addBoundary((double(nbins - i) / double(nbins)) * xlo +
   (double(i) / double(nbins)) * xhi);
}

////////////////////////////////////////////////////////////////////////////////
/// Return sequential bin number that contains value x where bin
/// zero is the first bin with an upper boundary above the lower bound
/// of the range

Int_t RooBinning::binNumber(Double_t x) const
{
  return std::max(0, std::min(_nbins, rawBinNumber(x) - _blo));
}

////////////////////////////////////////////////////////////////////////////////
/// Return sequential bin number that contains value x where bin
/// zero is the first bin that is defined, regardless if that bin
/// is outside the current defined range

Int_t RooBinning::rawBinNumber(Double_t x) const
{
  std::vector<Double_t>::const_iterator it = std::lower_bound(
      _boundaries.begin(), _boundaries.end(), x);
  // always return valid bin number
  while (_boundaries.begin() != it &&
     (_boundaries.end() == it || _boundaries.end() == it + 1 || x < *it)) --it;
  return it - _boundaries.begin();
}

////////////////////////////////////////////////////////////////////////////////
/// Return the value of the nearest boundary to x

Double_t RooBinning::nearestBoundary(Double_t x) const
{
  Double_t xl, xh;
  if (binEdges(binNumber(x), xl, xh)) return 0;
  return (std::abs(xl - x) < std::abs(xh - x)) ? xl : xh;
}

////////////////////////////////////////////////////////////////////////////////
/// Return array of boundary values

Double_t* RooBinning::array() const
{
  delete[] _array;
  _array = new Double_t[numBoundaries()];
  std::copy(_boundaries.begin()+_blo, _boundaries.begin()+_blo+_nbins+1, _array);
  return _array;
}

////////////////////////////////////////////////////////////////////////////////
/// Change the defined range associated with this binning.
/// Bins that lie outside the new range [xlo,xhi] will not be
/// removed, but will be 'inactive', i.e. the new 0 bin will
/// be the first bin with an upper boundarie > xlo

void RooBinning::setRange(Double_t xlo, Double_t xhi)
{
  if (xlo > xhi) {
    coutE(InputArguments) << "RooBinning::setRange: ERROR low bound > high bound" << endl;
    return;
  }
  // Remove previous boundaries
  if (_ownBoundLo) removeBoundary(_xlo);
  if (_ownBoundHi) removeBoundary(_xhi);
  // Insert boundaries at range delimiter, if necessary
  _ownBoundLo = addBoundary(xlo);
  _ownBoundHi = addBoundary(xhi);
  _xlo = xlo, _xhi = xhi;
  // Count number of bins with new range
  updateBinCount();
}

////////////////////////////////////////////////////////////////////////////////
/// Update the internal bin counter

void RooBinning::updateBinCount()
{
  if (_boundaries.size() <= 1) {
      _nbins = -1;
      return;
  }
  _blo = rawBinNumber(_xlo);
  std::vector<Double_t>::const_iterator it = std::lower_bound(
      _boundaries.begin(), _boundaries.end(), _xhi);
  if (_boundaries.begin() != it && (_boundaries.end() == it || _xhi < *it)) --it;
  const Int_t bhi = it - _boundaries.begin();
  _nbins = bhi - _blo;
}

////////////////////////////////////////////////////////////////////////////////
/// Return upper and lower bound of bin 'bin'. If the return value
/// is true an error occurred

bool RooBinning::binEdges(Int_t bin, Double_t& xlo, Double_t& xhi) const
{
  if (0 > bin || bin >= _nbins) {
    coutE(InputArguments) << "RooBinning::binEdges ERROR: bin number must be in range (0," << _nbins << ")" << endl;
    return true;
  }
  xlo = _boundaries[bin + _blo], xhi = _boundaries[bin + _blo + 1];
  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// Return the position of the center of bin 'bin'

Double_t RooBinning::binCenter(Int_t bin) const
{
  Double_t xlo, xhi;
  if (binEdges(bin, xlo, xhi)) return 0;
  return 0.5 * (xlo + xhi);
}

////////////////////////////////////////////////////////////////////////////////
/// Return the width of the requested bin

Double_t RooBinning::binWidth(Int_t bin) const
{
  Double_t xlo, xhi;
  if (binEdges(bin, xlo, xhi)) return 0;
  return (xhi - xlo);
}

////////////////////////////////////////////////////////////////////////////////
/// Return the lower bound of the requested bin

Double_t RooBinning::binLow(Int_t bin) const
{
  Double_t xlo, xhi;
  if (binEdges(bin, xlo, xhi)) return 0;
  return xlo;
}

////////////////////////////////////////////////////////////////////////////////
/// Return the upper bound of the requested bin

Double_t RooBinning::binHigh(Int_t bin) const
{
  Double_t xlo, xhi;
  if (binEdges(bin, xlo, xhi)) return  0;
  return xhi;
}

////////////////////////////////////////////////////////////////////////////////
/// Custom streamer that provides backward compatibility to read v1 data

void RooBinning::Streamer(TBuffer &R__b)
{
   if (R__b.IsReading()) {

     UInt_t R__s, R__c;
     Version_t R__v = R__b.ReadVersion(&R__s, &R__c); if (R__v) { }
     switch (R__v) {
       case 3:
    // current version - fallthrough intended
       case 2:
    // older version with std::set<Double_t> instead of
    // std::vector<Double_t>, apparently ROOT is clever enough to not care
    // about set vs vector
    R__b.ReadClassBuffer(RooBinning::Class(), this, R__v, R__s, R__c);
    break;
       case 1:
    {
      RooAbsBinning::Streamer(R__b);
      R__b >> _xlo;
      R__b >> _xhi;
      R__b >> _ownBoundLo;
      R__b >> _ownBoundHi;
      R__b >> _nbins;

      _boundaries.clear();
      // Convert TList to std::vector<Double_t>
      TList tmp;
      tmp.Streamer(R__b);
      _boundaries.reserve(tmp.GetSize());
      TIterator* it = tmp.MakeIterator();
      for (RooDouble* el = (RooDouble*) it->Next(); el;
          el = (RooDouble*) it->Next()) _boundaries.push_back(*el);
      delete it;
    }
    R__b.CheckByteCount(R__s, R__c, RooBinning::IsA());
    break;
       default:
    throw std::string("Unknown class version!");
     }
     if (_boundaries.size() > 2) {
       std::sort(_boundaries.begin(), _boundaries.end());
       _boundaries.erase(std::unique(_boundaries.begin(), _boundaries.end()),
      _boundaries.end());
     }
   } else {
     R__b.WriteClassBuffer(RooBinning::Class(),this);
   }
}
