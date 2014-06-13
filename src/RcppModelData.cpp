/*
 * RcppModelData.cpp
 *
 *  Created on: Apr 24, 2014
 *      Author: msuchard
 */

#include "Rcpp.h"
#include "RcppModelData.h"
#include "Timer.h"
#include "RcppCcdInterface.h"
#include "io/NewGenericInputReader.h"
#include "RcppProgressLogger.h"

using namespace Rcpp;
 
XPtr<bsccs::ModelData> parseEnvironmentForPtr(const Environment& x) {
	if (!x.inherits("ccdData")) {
		::Rf_error("Input must be a ccdData object");	// TODO Change to "stop"
	}
			
	SEXP tSexp = x["ccdDataPtr"];
	if (TYPEOF(tSexp) != EXTPTRSXP) {
		::Rf_error("Input must contain a ccdDataPtr object"); // TODO Change to "stop" (bug in Rcpp 0.11)
	}	

	XPtr<bsccs::ModelData> ptr(tSexp);
	if (!ptr) {
		::Rf_error("ccdData object is uninitialized"); // TODO Change to "stop"
	}
	return ptr;	
}

// [[Rcpp::export(".isRcppPtrNull")]]
bool isRcppPtrNull(SEXP x) {
	if (TYPEOF(x) != EXTPTRSXP) {
		::Rf_error("Input must be an Rcpp externalptr"); // TODO Change to "stop"
	}
	XPtr<int> ptr(x); 
	return !ptr;
}

// [[Rcpp::export("getNumberOfStrata")]]
size_t ccdGetNumberOfStrata(Environment x) {			
	XPtr<bsccs::ModelData> data = parseEnvironmentForPtr(x);	
	return data->getNumberOfPatients();
}

// [[Rcpp::export("getNumberOfCovariates")]]
size_t ccdGetNumberOfColumns(Environment x) {	
	XPtr<bsccs::ModelData> data = parseEnvironmentForPtr(x);	
	return data->getNumberOfColumns();
}

// [[Rcpp::export("getNumberOfRows")]]
size_t ccdGetNumberOfRows(Environment x) {	
	XPtr<bsccs::ModelData> data = parseEnvironmentForPtr(x);	
	return data->getNumberOfRows();
}


// TODO MJS wants a default constructor and a append function that takes chunks

// [[Rcpp::export(".ccdReadSqlData")]]
List ccdReadSqlData(
        const std::vector<long>& oStratumId,
        const std::vector<long>& oRowId,
        const std::vector<double>& oY,
        const std::vector<double>& oTime,
        const std::vector<long>& cRowId,
        const std::vector<long>& cCovariateId,
        const std::vector<double>& cCovariateValue,
        const std::string& modelTypeName) {
        // o -> outcome, c -> covariates
//     SqlGenericInputReader reader(
    List list = List::create(
            Rcpp::Named("result") = 42.0
        );
    return list;
}

// [[Rcpp::export(".ccdReadData")]]
List ccdReadFileData(const std::string& fileName, const std::string& modelTypeName) {

		using namespace bsccs;
		Timer timer; 
    Models::ModelType modelType = RcppCcdInterface::parseModelType(modelTypeName);        		
    InputReader* reader = new NewGenericInputReader(modelType,
    	bsccs::make_shared<loggers::RcppProgressLogger>(true), // make silent
    	bsccs::make_shared<loggers::RcppErrorHandler>());	
		reader->readFile(fileName.c_str()); // TODO Check for error

    XPtr<ModelData> ptr(reader->getModelData());
    
    
    const std::vector<double>& y = ptr->getYVectorRef();
    double total = std::accumulate(y.begin(), y.end(), 0.0);
    // delete reader; // TODO Test
       
    double time = timer();
    List list = List::create(
            Rcpp::Named("ccdDataPtr") = ptr,            
            Rcpp::Named("timeLoad") = time,
            Rcpp::Named("debug") = List::create(
            		Rcpp::Named("totalY") = total
            )
    );
    return list;
}

// [[Rcpp::export(".ccdModelData")]]
List ccdModelData(SEXP pid, SEXP y, SEXP z, SEXP offs, SEXP dx, SEXP sx, SEXP ix) {

	bsccs::Timer timer;

	IntegerVector ipid;
	if (!Rf_isNull(pid)) {
		ipid = pid; // This is not a copy
	} // else pid.size() == 0

	NumericVector iy(y);

	NumericVector iz;
	if (!Rf_isNull(z)) {
		iz = z;
	} // else z.size() == 0

	NumericVector ioffs;
	if (!Rf_isNull(offs)) {
		ioffs = offs;
	}

	// dense
	NumericVector dxv;
	if (!Rf_isNull(dx)) {
		S4 dxx(dx);
		dxv = dxx.slot("x");
	}

	// sparse
	IntegerVector siv, spv; NumericVector sxv;
	if (!Rf_isNull(sx)) {
		S4 sxx(sx);
		siv = sxx.slot("i");
		spv = sxx.slot("p");
		sxv = sxx.slot("x");
	}

	// indicator
	IntegerVector iiv, ipv;
	if (!Rf_isNull(ix)) {
		S4 ixx(ix);
		iiv = ixx.slot("i"); // TODO Check that no copy is made
		ipv = ixx.slot("p");
	}

	using namespace bsccs;
    XPtr<RcppModelData> ptr(new RcppModelData(ipid, iy, iz, ioffs, dxv, siv, spv, sxv, iiv, ipv));

	double duration = timer();

 	List list = List::create(
 			Rcpp::Named("data") = ptr,
 			Rcpp::Named("timeLoad") = duration
 		);
  return list;
}

namespace bsccs {

RcppModelData::RcppModelData(
		const IntegerVector& pid,
		const NumericVector& y,
		const NumericVector& z,
		const NumericVector& offs,
		const NumericVector& dxv, // dense
		const IntegerVector& siv, // sparse
		const IntegerVector& spv,
		const NumericVector& sxv,
		const IntegerVector& iiv, // indicator
		const IntegerVector& ipv
		) : ModelData(
				pid,
				y,
				z,
				offs
				) {
	// Convert dense
	int nCovariates = static_cast<int>(dxv.size() / y.size());
	for (int i = 0; i < nCovariates; ++i) {
		push_back(
				NULL, NULL,
				dxv.begin() + i * y.size(), dxv.begin() + (i + 1) * y.size(),
				DENSE);
		getColumn(getNumberOfColumns() - 1).add_label(getNumberOfColumns());
//		std::cout << "Added dense covariate" << std::endl;
	}

	// Convert sparse
	nCovariates = spv.size() - 1;
	for (int i = 0; i < nCovariates; ++i) {

		int begin = spv[i];
		int end = spv[i + 1];

		push_back(
				siv.begin() + begin, siv.begin() + end,
				sxv.begin() + begin, sxv.begin() + end,
				SPARSE);
        getColumn(getNumberOfColumns() - 1).add_label(getNumberOfColumns());				
//		std::cout << "Added sparse covariate " << (end - begin) << std::endl;
	}

	// Convert indicator
	nCovariates = ipv.size() - 1;
	for (int i = 0; i < nCovariates; ++i) {

		int begin = ipv[i];
		int end = ipv[i + 1];

		push_back(
				iiv.begin() + begin, iiv.begin() + end,
				NULL, NULL,
				INDICATOR);
        getColumn(getNumberOfColumns() - 1).add_label(getNumberOfColumns());				
//		std::cout << "Added indicator covariate " << (end - begin) << std::endl;
	}

//	std::cout << "Ncol = " << getNumberOfColumns() << std::endl;

	this->nRows = y.size();
	
	// Clean out PIDs
	std::vector<int>& cpid = getPidVectorRef();
	
	if (cpid.size() == 0) {
	    for (int i = 0; i < nRows; ++i) {
	        cpid.push_back(i);
	    }
	    nPatients = nRows;
	} else {
    	int currentCase = 0;
    	int currentPID = cpid[0];
    	cpid[0] = currentCase;
    	for (unsigned int i = 1; i < pid.size(); ++i) {
    	    int nextPID = cpid[i];
    	    if (nextPID != currentPID) {
	            currentCase++;
    	    }
	        cpid[i] = currentCase;
    	}
      nPatients = currentCase + 1;
    }    
}

RcppModelData::~RcppModelData() {
//	std::cout << "~RcppModelData() called." << std::endl;
}

} /* namespace bsccs */
