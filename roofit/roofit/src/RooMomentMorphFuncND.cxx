/*****************************************************************************
 * Project: RooFit                                                           *
 *                                                                           *
 * This code was autogenerated by RooClassFactory                            *
 *****************************************************************************/

#include "Riostream.h"

#include "RooMomentMorphFuncND.h"
#include "RooAbsCategory.h"
#include "RooRealIntegral.h"
#include "RooRealConstant.h"
#include "RooRealVar.h"
#include "RooFormulaVar.h"
#include "RooCustomizer.h"
#include "RooRealSumFunc.h"
#include "RooAddition.h"
#include "RooAbsMoment.h"
#include "RooMoment.h"
#include "RooLinearVar.h"
#include "RooChangeTracker.h"
#include "RooNumIntConfig.h"
#include "RooHistPdf.h"

#include "TMath.h"
#include "TVector.h"
#include "TMap.h"

#include <map>
#include <algorithm>

using namespace std;

// ClassImp for RooMomentMorphFuncND
ClassImp(RooMomentMorphFuncND)

   // ClassImp for RooMomentMorphFuncND
   ClassImp(RooMomentMorphFuncND::Grid2)

   //_____________________________________________________________________________
   RooMomentMorphFuncND::RooMomentMorphFuncND()
   : _cacheMgr(this, 10, true, true), _curNormSet(0), _M(0), _MSqr(0), _setting(RooMomentMorphFuncND::Linear), _useHorizMorph(true)
{
   TRACE_CREATE
}

//_____________________________________________________________________________
RooMomentMorphFuncND::RooMomentMorphFuncND(const char *name, const char *title, const RooArgList &parList,
                                           const RooArgList &obsList, const Grid2 &referenceGrid,
                                           const Setting &setting)
   : RooAbsReal(name, title), _cacheMgr(this, 10, true, true), _parList("parList", "List of morph parameters", this),
     _obsList("obsList", "List of observables", this), _referenceGrid(referenceGrid),
     _pdfList("pdfList", "List of pdfs", this), _setting(setting), _useHorizMorph(true)
{
   // morph parameters
   initializeParameters(parList);

   // observables
   initializeObservables(obsList);

   _pdfList.add(_referenceGrid._pdfList);

   // general initialization
   initialize();

   TRACE_CREATE
}

//_____________________________________________________________________________
RooMomentMorphFuncND::RooMomentMorphFuncND(const char *name, const char *title, RooAbsReal &_m,
                                           const RooArgList &varList, const RooArgList &pdfList,
                                           const TVectorD &mrefpoints, Setting setting)
   : RooAbsReal(name, title), _cacheMgr(this, 10, true, true), _parList("parList", "List of morph parameters", this),
     _obsList("obsList", "List of observables", this), _pdfList("pdfList", "List of pdfs", this), _setting(setting),
     _useHorizMorph(true)
{
   // make reference grid
   RooBinning grid(mrefpoints.GetNrows() - 1, mrefpoints.GetMatrixArray());
   _referenceGrid.addBinning(grid);

   for (int i = 0; i < mrefpoints.GetNrows(); ++i) {
      for (int j = 0; j < grid.numBoundaries(); ++j) {
         if (mrefpoints[i] == grid.array()[j]) {
            _referenceGrid.addPdf(*(RooAbsReal *)pdfList.at(i), j);
            break;
         }
      }
   }

   _pdfList.add(_referenceGrid._pdfList);

   // morph parameters
   RooArgList parList;
   parList.add(_m);
   initializeParameters(parList);

   // observables
   initializeObservables(varList);

   // general initialization
   initialize();

   TRACE_CREATE
}

//_____________________________________________________________________________
RooMomentMorphFuncND::RooMomentMorphFuncND(const char *name, const char *title, RooAbsReal &_m,
                                           const RooArgList &varList, const RooArgList &pdfList,
                                           const RooArgList &mrefList, Setting setting)
   : RooAbsReal(name, title), _cacheMgr(this, 10, true, true), _parList("parList", "List of morph parameters", this),
     _obsList("obsList", "List of observables", this), _pdfList("pdfList", "List of pdfs", this), _setting(setting),
     _useHorizMorph(true)
{
   // make reference grid
   TVectorD mrefpoints(mrefList.getSize());
   Int_t i = 0;
   for (auto *mref : mrefList) {
      if (!dynamic_cast<RooAbsReal*>(mref)) {
         coutE(InputArguments) << "RooMomentMorphFuncND::ctor(" << GetName() << ") ERROR: mref " << mref->GetName()
                               << " is not of type RooAbsReal" << endl;
         throw string("RooMomentMorphFuncND::ctor() ERROR mref is not of type RooAbsReal");
      }
      if (!dynamic_cast<RooConstVar *>(mref)) {
         coutW(InputArguments) << "RooMomentMorphFuncND::ctor(" << GetName() << ") WARNING mref point " << i
                               << " is not a constant, taking a snapshot of its value" << endl;
      }
      mrefpoints[i] = static_cast<RooAbsReal*>(mref)->getVal();
      ++i;
   }

   RooBinning grid(mrefpoints.GetNrows() - 1, mrefpoints.GetMatrixArray());
   _referenceGrid.addBinning(grid);
   for (i = 0; i < mrefpoints.GetNrows(); ++i) {
      for (int j = 0; j < grid.numBoundaries(); ++j) {
         if (mrefpoints[i] == grid.array()[j]) {
            _referenceGrid.addPdf(static_cast<RooAbsReal &>(pdfList[i]), j);
            break;
         }
      }
   }

   _pdfList.add(_referenceGrid._pdfList);

   // morph parameters
   RooArgList parList;
   parList.add(_m);
   initializeParameters(parList);

   // observables
   initializeObservables(varList);

   // general initialization
   initialize();

   TRACE_CREATE
}

//_____________________________________________________________________________
RooMomentMorphFuncND::RooMomentMorphFuncND(const RooMomentMorphFuncND &other, const char *name)
   : RooAbsReal(other, name), _cacheMgr(other._cacheMgr, this), _curNormSet(0),
     _parList("parList", this, other._parList), _obsList("obsList", this, other._obsList),
     _referenceGrid(other._referenceGrid), _pdfList("pdfList", this, other._pdfList), _M(0), _MSqr(0),
     _setting(other._setting), _useHorizMorph(other._useHorizMorph)
{

   // general initialization
   initialize();

   TRACE_CREATE
}

//_____________________________________________________________________________
RooMomentMorphFuncND::~RooMomentMorphFuncND()
{
   if (_parItr)
      delete _parItr;
   if (_obsItr)
      delete _obsItr;
   if (_M)
      delete _M;
   if (_MSqr)
      delete _MSqr;

   TRACE_DESTROY
}

//_____________________________________________________________________________
void RooMomentMorphFuncND::initializeParameters(const RooArgList &parList)
{
   for (auto *par : parList) {
      if (!dynamic_cast<RooAbsReal *>(par)) {
         coutE(InputArguments) << "RooMomentMorphFuncND::ctor(" << GetName() << ") ERROR: parameter " << par->GetName()
                               << " is not of type RooAbsReal" << endl;
         throw string("RooMomentMorphFuncND::initializeParameters() ERROR parameter is not of type RooAbsReal");
      }
      _parList.add(*par);
   }
}

//_____________________________________________________________________________
void RooMomentMorphFuncND::initializeObservables(const RooArgList &obsList)
{
   for (auto *var : obsList) {
      if (!dynamic_cast<RooAbsReal *>(var)) {
         coutE(InputArguments) << "RooMomentMorphFuncND::ctor(" << GetName() << ") ERROR: variable " << var->GetName()
                               << " is not of type RooAbsReal" << endl;
         throw string("RooMomentMorphFuncND::initializeObservables() ERROR variable is not of type RooAbsReal");
      }
      _obsList.add(*var);
   }
}

//_____________________________________________________________________________
// from http://stackoverflow.com/a/5279601
template <typename T>
void RooMomentMorphFuncND::cartesian_product(vector<vector<T>> &out, vector<vector<T>> &in)
{
   vector<Digits<T>> vd;

   for (typename vector<vector<T>>::const_iterator it = in.begin(); it != in.end(); ++it) {
      Digits<T> d = {(*it).begin(), (*it).end(), (*it).begin()};
      vd.push_back(d);
   }

   while (1) {
      vector<T> result;
      for (typename vector<Digits<T>>::const_iterator it = vd.begin(); it != vd.end(); ++it) {
         result.push_back(*(it->me));
      }
      out.push_back(result);

      for (typename vector<Digits<T>>::iterator it = vd.begin();;) {
         ++(it->me);
         if (it->me == it->end) {
            if (it + 1 == vd.end()) {
               return;
            } else {
               it->me = it->begin;
               ++it;
            }
         } else {
            break;
         }
      }
   }
}

//_____________________________________________________________________________
void RooMomentMorphFuncND::initialize()
{
   // TIterator* pdfItr = _referenceGrid._pdfList.createIterator() ;
   // RooAbsReal* pdf ;
   // for (int i=0; (pdf = dynamic_cast<RooAbsReal*>(pdfItr->Next())); ++i) {
   //   if (!pdf) {
   //     coutE(InputArguments) << "RooMomentMorphFunc::ctor(" << GetName() << ") ERROR: pdf " << pdf->GetName() << " is
   //     not of type RooAbsReal" << endl ;
   //     throw string("RooPolyMorh::ctor() ERROR pdf is not of type RooAbsReal") ;
   //   }
   //   _pdfList.addClone(*pdf) ;
   // }
   // delete pdfItr ;

   for (vector<RooAbsBinning *>::iterator itr = _referenceGrid._grid.begin(); itr != _referenceGrid._grid.end();
        ++itr) {
      _referenceGrid._nnuis.push_back((*itr)->numBins() + 1);
   }

   int nPar = _parList.getSize();
   int nDim = _referenceGrid._grid.size();
   int nPdf = _referenceGrid._pdfList.getSize();
   int nRef = _referenceGrid._nref.size();
   int depth = TMath::Power(2, nPar);

   if (nPar != nDim) {
      coutE(InputArguments) << "RooMomentMorphFuncND::initialize(" << GetName() << ") ERROR: nPar != nDim"
                            << ": " << nPar << " !=" << nDim << endl;
      assert(0);
   }

   if (nPdf != nRef) {
      coutE(InputArguments) << "RooMomentMorphFuncND::initialize(" << GetName() << ") ERROR: nPdf != nRef"
                            << ": " << nPdf << " !=" << nRef << endl;
      assert(0);
   }

   // Transformation matrix for NonLinear settings
   _M = new TMatrixD(nPdf, nPdf);
   _MSqr = new TMatrixD(depth, depth);
   if (_setting == NonLinear || _setting == NonLinearPosFractions || _setting == NonLinearLinFractions) {
      TMatrixD M(nPdf, nPdf);

      vector<vector<double>> dm(nPdf);
      for (int k = 0; k < nPdf; ++k) {
         vector<double> dm2;
         for (int idim = 0; idim < nPar; idim++) {
            Double_t delta = _referenceGrid._nref[k][idim] - _referenceGrid._nref[0][idim];
            dm2.push_back(delta);
         }
         dm[k] = dm2;
      }

      vector<vector<int>> powers;
      for (int idim = 0; idim < nPar; idim++) {
         vector<int> xtmp;
         for (int ix = 0; ix < _referenceGrid._nnuis[idim]; ix++) {
            xtmp.push_back(ix);
         }
         powers.push_back(xtmp);
      }

      vector<vector<int>> output;
      cartesian_product(output, powers);
      int nCombs = output.size();

      for (int k = 0; k < nPdf; ++k) {
         int nperm = 0;
         for (int i = 0; i < nCombs; i++) {
            double tmpDm = 1.0;
            for (int ix = 0; ix < nPar; ix++) {
               Double_t delta = dm[k][ix];
               tmpDm *= TMath::Power(delta, static_cast<double>(output[i][ix]));
            }
            M(k, nperm) = tmpDm;
            nperm++;
         }
      }

      // M.Print();
      (*_M) = M.Invert();
   }

   // Resize transformation vectors
   _squareVec.resize(TMath::Power(2, nPar));
   _squareIdx.resize(TMath::Power(2, nPar));
}

//_____________________________________________________________________________
RooMomentMorphFuncND::Grid2::Grid2(const RooMomentMorphFuncND::Grid2 &other)
   : _grid(other._grid), _pdfList(other._pdfList), _pdfMap(other._pdfMap), _nref(other._nref)
{
}

//_____________________________________________________________________________
RooMomentMorphFuncND::Grid2::~Grid2()
{
}

//_____________________________________________________________________________
void RooMomentMorphFuncND::Grid2::addPdf(const RooAbsReal &pdf, int bin_x)
{
   vector<int> thisBoundaries;
   vector<double> thisBoundaryCoordinates;
   thisBoundaries.push_back(bin_x);
   thisBoundaryCoordinates.push_back(_grid[0]->array()[bin_x]);
   _pdfList.add(pdf);
   _pdfMap[thisBoundaries] = _pdfList.getSize() - 1;
   _nref.push_back(thisBoundaryCoordinates);
}

//_____________________________________________________________________________
void RooMomentMorphFuncND::Grid2::addPdf(const RooAbsReal &pdf, int bin_x, int bin_y)
{
   vector<int> thisBoundaries;
   vector<double> thisBoundaryCoordinates;
   thisBoundaries.push_back(bin_x);
   thisBoundaryCoordinates.push_back(_grid[0]->array()[bin_x]);
   thisBoundaries.push_back(bin_y);
   thisBoundaryCoordinates.push_back(_grid[1]->array()[bin_y]);
   _pdfList.add(pdf);
   _pdfMap[thisBoundaries] = _pdfList.getSize() - 1;
   _nref.push_back(thisBoundaryCoordinates);
}

//_____________________________________________________________________________
void RooMomentMorphFuncND::Grid2::addPdf(const RooAbsReal &pdf, int bin_x, int bin_y, int bin_z)
{
   vector<int> thisBoundaries;
   vector<double> thisBoundaryCoordinates;
   thisBoundaries.push_back(bin_x);
   thisBoundaryCoordinates.push_back(_grid[0]->array()[bin_x]);
   thisBoundaries.push_back(bin_y);
   thisBoundaryCoordinates.push_back(_grid[1]->array()[bin_y]);
   thisBoundaries.push_back(bin_z);
   thisBoundaryCoordinates.push_back(_grid[2]->array()[bin_z]);
   _pdfList.add(pdf);
   _pdfMap[thisBoundaries] = _pdfList.getSize() - 1;
   _nref.push_back(thisBoundaryCoordinates);
}

//_____________________________________________________________________________
void RooMomentMorphFuncND::Grid2::addPdf(const RooAbsReal &pdf, vector<int> bins)
{
   vector<double> thisBoundaryCoordinates;
   int nBins = bins.size();
   for (int i = 0; i < nBins; i++) {
      thisBoundaryCoordinates.push_back(_grid[i]->array()[bins[i]]);
   }
   _pdfList.add(pdf);
   _pdfMap[bins] = _pdfList.getSize() - 1;
   _nref.push_back(thisBoundaryCoordinates);
}

//_____________________________________________________________________________
RooMomentMorphFuncND::CacheElem *RooMomentMorphFuncND::getCache(const RooArgSet * /*nset*/) const
{
   CacheElem *cache = static_cast<CacheElem *>(_cacheMgr.getObj(0, (RooArgSet *)0));
   if (cache) {
      return cache;
   }

   int nObs = _obsList.getSize();
   int nPdf = _referenceGrid._pdfList.getSize();

   RooAbsReal *null = 0;
   vector<RooAbsReal *> meanrv(nPdf * nObs, null);
   vector<RooAbsReal *> sigmarv(nPdf * nObs, null);
   vector<RooAbsReal *> myrms(nObs, null);
   vector<RooAbsReal *> mypos(nObs, null);
   vector<RooAbsReal *> slope(nPdf * nObs, null);
   vector<RooAbsReal *> offsets(nPdf * nObs, null);
   vector<RooAbsReal *> transVar(nPdf * nObs, null);
   vector<RooAbsReal *> transPdf(nPdf, null);

   RooArgSet ownedComps;
   RooArgList fracl;

   // fraction parameters
   RooArgList coefList("coefList");   // fractions multiplied with input pdfs
   RooArgList coefList2("coefList2"); // fractions multiplied with mean position of observable contribution
   RooArgList coefList3("coefList3"); // fractions multiplied with rms position of observable contribution

   for (int i = 0; i < 3 * nPdf; ++i) {
      string fracName = Form("frac_%d", i);
      double initval = 0.0;
      RooRealVar *frac = new RooRealVar(fracName.c_str(), fracName.c_str(), initval); // to be set later

      fracl.add(*frac);
      if (i < nPdf)
         coefList.add(*(RooRealVar *)(fracl.at(i)));
      else if (i < 2 * nPdf)
         coefList2.add(*(RooRealVar *)(fracl.at(i)));
      else
         coefList3.add(*(RooRealVar *)(fracl.at(i)));
      ownedComps.add(*(RooRealVar *)(fracl.at(i)));
   }

   RooRealSumFunc *theSumFunc = 0;
   string sumfuncName = Form("%s_sumfunc", GetName());

   if (_useHorizMorph) {
      // mean and sigma
      RooArgList obsList(_obsList);
      for (int i = 0; i < nPdf; ++i) {
         for (int j = 0; j < nObs; ++j) {
            RooAbsMoment *mom = nObs == 1
                                   ? ((RooAbsReal *)_pdfList.at(i))->sigma((RooRealVar &)*obsList.at(j))
                                   : ((RooAbsReal *)_pdfList.at(i))->sigma((RooRealVar &)*obsList.at(j), obsList);

            mom->setLocalNoDirtyInhibit(true);
            mom->mean()->setLocalNoDirtyInhibit(true);

            sigmarv[sij(i, j)] = mom;
            meanrv[sij(i, j)] = mom->mean();

            ownedComps.add(*sigmarv[sij(i, j)]);
         }
      }

      // slope and offsets (to be set later, depend on nuisance parameters)
      for (int j = 0; j < nObs; ++j) {
         RooArgList meanList("meanList");
         RooArgList rmsList("rmsList");
         for (int i = 0; i < nPdf; ++i) {
            meanList.add(*meanrv[sij(i, j)]);
            rmsList.add(*sigmarv[sij(i, j)]);
         }
         string myrmsName = Form("%s_rms_%d", GetName(), j);
         string myposName = Form("%s_pos_%d", GetName(), j);
         mypos[j] = new RooAddition(myposName.c_str(), myposName.c_str(), meanList, coefList2);
         myrms[j] = new RooAddition(myrmsName.c_str(), myrmsName.c_str(), rmsList, coefList3);
         ownedComps.add(RooArgSet(*myrms[j], *mypos[j]));
      }

      // construction of unit pdfs
      RooArgList transPdfList;

      Int_t i = 0;
      for (auto const *pdf : static_range_cast<RooAbsReal *>(_pdfList)) {

         string pdfName = Form("pdf_%d", i);
         RooCustomizer cust(*pdf, pdfName.c_str());

         Int_t j = 0;
         for (auto *var : static_range_cast<RooRealVar *>(obsList)) {
            // slope and offsets formulas
            string slopeName = Form("%s_slope_%d_%d", GetName(), i, j);
            string offsetName = Form("%s_offset_%d_%d", GetName(), i, j);

            slope[sij(i, j)] =
               new RooFormulaVar(slopeName.c_str(), "@0/@1", RooArgList(*sigmarv[sij(i, j)], *myrms[j]));
            offsets[sij(i, j)] = new RooFormulaVar(offsetName.c_str(), "@0-(@1*@2)",
                                                   RooArgList(*meanrv[sij(i, j)], *mypos[j], *slope[sij(i, j)]));
            ownedComps.add(RooArgSet(*slope[sij(i, j)], *offsets[sij(i, j)]));

            // linear transformations, so pdf can be renormalized easily
            string transVarName = Form("%s_transVar_%d_%d", GetName(), i, j);
            transVar[sij(i, j)] = new RooLinearVar(transVarName.c_str(), transVarName.c_str(), *var, *slope[sij(i, j)],
                                                   *offsets[sij(i, j)]);

            // *** WVE this is important *** this declares that frac effectively depends on the morphing parameters
            // This will prevent the likelihood optimizers from erroneously declaring terms constant
            transVar[sij(i, j)]->addServerList((RooAbsCollection &)_parList);

            ownedComps.add(*transVar[sij(i, j)]);
            cust.replaceArg(*var, *transVar[sij(i, j)]);
            ++j;
         }
         transPdf[i] = (RooAbsReal *)cust.build();
         transPdfList.add(*transPdf[i]);
         ownedComps.add(*transPdf[i]);
         ++i;
      }

      // sum pdf
      theSumFunc = new RooRealSumFunc(sumfuncName.c_str(), sumfuncName.c_str(), transPdfList, coefList);
   } else {
      theSumFunc = new RooRealSumFunc(sumfuncName.c_str(), sumfuncName.c_str(), _pdfList, coefList);
   }

   // *** WVE this is important *** this declares that frac effectively depends on the morphing parameters
   // This will prevent the likelihood optimizers from erroneously declaring terms constant
   theSumFunc->addServerList((RooAbsCollection &)_parList);
   theSumFunc->addOwnedComponents(ownedComps);

   // change tracker for fraction parameters
   string trackerName = Form("%s_frac_tracker", GetName());
   RooChangeTracker *tracker = new RooChangeTracker(trackerName.c_str(), trackerName.c_str(), _parList, true);

   // Store it in the cache
   cache = new CacheElem(*theSumFunc, *tracker, fracl);
   _cacheMgr.setObj(0, 0, cache, 0);

   return cache;
}

//_____________________________________________________________________________
RooArgList RooMomentMorphFuncND::CacheElem::containedArgs(Action)
{
   return RooArgList(*_sumFunc, *_tracker);
}

//_____________________________________________________________________________
RooMomentMorphFuncND::CacheElem::~CacheElem()
{
   delete _sumFunc;
   delete _tracker;
}

//_____________________________________________________________________________
Double_t RooMomentMorphFuncND::getVal(const RooArgSet *set) const
{
   // Special version of getVal() overrides RooAbsReal::getVal() to save value of current normalization set
   _curNormSet = set ? (RooArgSet *)set : (RooArgSet *)&_obsList;
   return RooAbsReal::getVal(set);
}

//_____________________________________________________________________________
RooAbsReal *RooMomentMorphFuncND::sumFunc(const RooArgSet *nset)
{
   CacheElem *cache = getCache(nset ? nset : _curNormSet);

   if (cache->_tracker->hasChanged(true)) {
      cache->calculateFractions(*this, false); // verbose turned off
   }
   return cache->_sumFunc;
}

//_____________________________________________________________________________
Double_t RooMomentMorphFuncND::evaluate() const
{
   CacheElem *cache = getCache(_curNormSet);

   if (cache->_tracker->hasChanged(true)) {
      cache->calculateFractions(*this, false); // verbose turned off
   }

   Double_t ret = cache->_sumFunc->getVal(_obsList.nset());

   return ret;
}

//_____________________________________________________________________________
RooRealVar *RooMomentMorphFuncND::CacheElem::frac(int i)
{
   return (RooRealVar *)(_frac.at(i));
}

//_____________________________________________________________________________
const RooRealVar *RooMomentMorphFuncND::CacheElem::frac(int i) const
{
   return (RooRealVar *)(_frac.at(i));
}

//_____________________________________________________________________________
// from http://stackoverflow.com/a/5097100/8747
template <typename Iterator>
bool RooMomentMorphFuncND::next_combination(const Iterator first, Iterator k, const Iterator last)
{
   if ((first == last) || (first == k) || (last == k)) {
      return false;
   }
   Iterator itr1 = first;
   Iterator itr2 = last;
   ++itr1;
   if (last == itr1) {
      return false;
   }
   itr1 = last;
   --itr1;
   itr1 = k;
   --itr2;
   while (first != itr1) {
      if (*--itr1 < *itr2) {
         Iterator j = k;
         while (!(*itr1 < *j)) ++j;
         iter_swap(itr1, j);
         ++itr1;
         ++j;
         itr2 = k;
         rotate(itr1, j, last);
         while (last != j) {
            ++j;
            ++itr2;
         }
         rotate(k, itr2, last);
         return true;
      }
   }
   rotate(first, k, last);
   return false;
}

//_____________________________________________________________________________
void RooMomentMorphFuncND::CacheElem::calculateFractions(const RooMomentMorphFuncND &self, bool verbose) const
{
   int nPdf = self._pdfList.getSize();
   int nPar = self._parList.getSize();

   Double_t fracLinear(1.);
   Double_t fracNonLinear(1.);

   if (self._setting == NonLinear || self._setting == NonLinearLinFractions || self._setting == NonLinearPosFractions) {
      // Calculate the delta vector
      vector<double> dm2;
      for (int idim = 0; idim < nPar; idim++) {
         Double_t delta = ((RooRealVar *)self._parList.at(idim))->getVal() - self._referenceGrid._nref[0][idim];
         dm2.push_back(delta);
      }

      vector<vector<int>> powers;
      for (int idim = 0; idim < nPar; idim++) {
         vector<int> xtmp;
         for (int ix = 0; ix < self._referenceGrid._nnuis[idim]; ix++) {
            xtmp.push_back(ix);
         }
         powers.push_back(xtmp);
      }

      vector<vector<int>> output;
      cartesian_product(output, powers);
      int nCombs = output.size();

      vector<double> deltavec(nPdf, 1.0);

      int nperm = 0;
      for (int i = 0; i < nCombs; i++) {
         double tmpDm = 1.0;
         for (int ix = 0; ix < nPar; ix++) {
            Double_t delta = dm2[ix];
            tmpDm *= TMath::Power(delta, static_cast<double>(output[i][ix]));
         }
         deltavec[nperm] = tmpDm;
         nperm++;
      }

      double sumposfrac = 0.0;
      for (int i = 0; i < nPdf; ++i) {
         double ffrac = 0.0;

         for (int j = 0; j < nPdf; ++j) {
            ffrac += (*self._M)(j, i) * deltavec[j] * fracNonLinear;
         }

         if (ffrac >= 0) {
            sumposfrac += ffrac;
         }

         // fractions for pdf
         if (self._setting != NonLinearLinFractions) {
            ((RooRealVar *)frac(i))->setVal(ffrac);
         }

         // fractions for rms and mean
         ((RooRealVar *)frac(nPdf + i))->setVal(ffrac);     // need to add up
         ((RooRealVar *)frac(2 * nPdf + i))->setVal(ffrac); // need to add up

         if (verbose) {
            cout << "NonLinear fraction " << ffrac << endl;
            frac(i)->Print();
            frac(nPdf + i)->Print();
            frac(2 * nPdf + i)->Print();
         }
      }

      if (self._setting == NonLinearPosFractions) {
         for (int i = 0; i < nPdf; ++i) {
            if (((RooRealVar *)frac(i))->getVal() < 0)
               ((RooRealVar *)frac(i))->setVal(0.);
            ((RooRealVar *)frac(i))->setVal(((RooRealVar *)frac(i))->getVal() / sumposfrac);
         }
      }
   }

   if (self._setting == Linear || self._setting == NonLinearLinFractions) {
      // loop over parList
      self._parItr->Reset();

      // zero all fractions
      // for (int i = 0; i < 3*nPdf; ++i) {
      for (int i = 0; i < nPdf; ++i) {
         double initval = 0;
         ((RooRealVar *)frac(i))->setVal(initval);
         ((RooRealVar *)frac(nPdf + i))->setVal(initval);
         ((RooRealVar *)frac(2 * nPdf + i))->setVal(initval);
      }

      vector<double> mtmp;

      for (int j = 0; j < nPar; j++) {
         RooRealVar *m = (RooRealVar *)(self._parItr->Next());
         mtmp.push_back(m->getVal());
      }

      self.findShape(mtmp); // this sets _squareVec and _squareIdx quantities

      int depth = TMath::Power(2, nPar);
      vector<double> deltavec(depth, 1.0);

      int nperm = 0;

      vector<int> xtmp;
      for (int ix = 0; ix < nPar; ix++) {
         xtmp.push_back(ix);
      }

      for (int iperm = 1; iperm <= nPar; ++iperm) {
         do {
            double dtmp = mtmp[xtmp[0]] - self._squareVec[0][xtmp[0]];
            for (int itmp = 1; itmp < iperm; ++itmp) {
               dtmp *= mtmp[xtmp[itmp]] - self._squareVec[0][xtmp[itmp]];
            }
            deltavec[nperm + 1] = dtmp;
            nperm++;
         } while (next_combination(xtmp.begin(), xtmp.begin() + iperm, xtmp.end()));
      }

      Double_t origFrac1(0.), origFrac2(0.);
      for (int i = 0; i < depth; ++i) {
         double ffrac = 0.;
         for (int j = 0; j < depth; ++j) {
            ffrac += (*self._MSqr)(j, i) * deltavec[j] * fracLinear;
         }

         // set fractions for pdf
         origFrac1 = ((RooRealVar *)frac(self._squareIdx[i]))->getVal();      // already set in case of smoothlinear
         ((RooRealVar *)frac(self._squareIdx[i]))->setVal(origFrac1 + ffrac); // need to add up

         // set fractions for rms and mean
         if (self._setting != NonLinearLinFractions) {
            origFrac2 =
               ((RooRealVar *)frac(nPdf + self._squareIdx[i]))->getVal(); // already set in case of smoothlinear
            ((RooRealVar *)frac(nPdf + self._squareIdx[i]))->setVal(origFrac2 + ffrac);     // need to add up
            ((RooRealVar *)frac(2 * nPdf + self._squareIdx[i]))->setVal(origFrac2 + ffrac); // need to add up
         }

         if (verbose) {
            cout << "Linear fraction " << ffrac << endl;
            frac(self._squareIdx[i])->Print();
            frac(nPdf + self._squareIdx[i])->Print();
            frac(2 * nPdf + self._squareIdx[i])->Print();
         }
      }
   }
}

//_____________________________________________________________________________
void RooMomentMorphFuncND::findShape(const vector<double> &x) const
{
   int nPar = _parList.getSize();
   int nRef = _referenceGrid._nref.size();

   // Find hypercube enclosing the location to morph to
   bool isEnclosed = true;
   for (int i = 0; i < nPar; i++) {
      if (x[i] < _referenceGrid._grid[i]->lowBound())
         isEnclosed = false;
      if (x[i] > _referenceGrid._grid[i]->highBound())
         isEnclosed = false;
   }
   (void)isEnclosed; // Silence warning for isEnclosed being unused.
   // cout << "isEnclosed = " << isEnclosed << endl;

   int depth = TMath::Power(2, nPar);

   vector<vector<double>> boundaries(nPar);
   for (int idim = 0; idim < nPar; idim++) {
      int bin = _referenceGrid._grid[idim]->binNumber(x[idim]);
      double lo = _referenceGrid._grid[idim]->binLow(bin);
      double hi = _referenceGrid._grid[idim]->binHigh(bin);
      boundaries[idim].push_back(lo);
      boundaries[idim].push_back(hi);
   }

   vector<vector<double>> output;
   cartesian_product(output, boundaries);
   _squareVec = output;

   for (int isq = 0; isq < depth; isq++) {
      for (int iref = 0; iref < nRef; iref++) {
         if (_squareVec[isq] == _referenceGrid._nref[iref]) {
            _squareIdx[isq] = iref;
            break;
         }
      }
   }

   // cout << endl;

   // for (int isq = 0; isq < _squareVec.size(); isq++) {
   //   cout << _squareIdx[isq];
   //   cout << " (";
   //   for (int isqq = 0; isqq < _squareVec[isq].size(); isqq++) {
   //     cout << _squareVec[isq][isqq] << ((isqq<_squareVec[isq].size()-1)?",":"");
   //   }
   //   cout << ") ";
   // }

   // construct transformation matrix for linear extrapolation
   TMatrixD M(depth, depth);

   vector<int> xtmp;
   for (int ix = 0; ix < nPar; ix++) {
      xtmp.push_back(ix);
   }

   for (int k = 0; k < depth; ++k) {
      M(k, 0) = 1.0;

      int nperm = 0;
      vector<double> squareBase = _squareVec[0];

      for (int iperm = 1; iperm <= nPar; ++iperm) {
         do {
            double dtmp = _squareVec[k][xtmp[0]] - squareBase[xtmp[0]];
            for (int itmp = 1; itmp < iperm; ++itmp) {
               dtmp *= _squareVec[k][xtmp[itmp]] - squareBase[xtmp[itmp]];
            }
            M(k, nperm + 1) = dtmp;
            nperm++;
         } while (next_combination(xtmp.begin(), xtmp.begin() + iperm, xtmp.end()));
      }
   }

   // M.Print();
   (*_MSqr) = M.Invert();
}

//_____________________________________________________________________________
bool RooMomentMorphFuncND::setBinIntegrator(RooArgSet &allVars)
{
   if (allVars.getSize() == 1) {
      RooAbsReal *temp = const_cast<RooMomentMorphFuncND *>(this);
      temp->specialIntegratorConfig(true)->method1D().setLabel("RooBinIntegrator");
      int nbins = ((RooRealVar *)allVars.first())->numBins();
      temp->specialIntegratorConfig(true)->getConfigSection("RooBinIntegrator").setRealValue("numBins", nbins);
      return true;
   } else {
      cout << "Currently BinIntegrator only knows how to deal with 1-d " << endl;
      return false;
   }
   return false;
}
