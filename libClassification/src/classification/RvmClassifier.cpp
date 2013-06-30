/*
 * RvmClassifier.cpp
 *
 *  Created on: 14.06.2013
 *      Author: Patrik Huber
 */

#include "classification/RvmClassifier.hpp"
#include "classification/PolynomialKernel.hpp"
#include "classification/RbfKernel.hpp"
#include "logging/LoggerFactory.hpp"
#include "mat.h"
#ifdef WIN32
	#define BOOST_ALL_DYN_LINK	// Link against the dynamic boost lib. Seems to be necessary because we use /MD, i.e. link to the dynamic CRT.
	#define BOOST_ALL_NO_LIB	// Don't use the automatic library linking by boost with VS2010 (#pragma ...). Instead, we specify everything in cmake.
#endif
#include "boost/filesystem/path.hpp"
#include "boost/lexical_cast.hpp"
#include <stdexcept>
#include <fstream>

using boost::filesystem::path;
using logging::Logger;
using logging::LoggerFactory;
using boost::lexical_cast;
using std::make_shared;
using std::make_pair;
using std::invalid_argument;
using std::runtime_error;
using std::logic_error;

namespace classification {

RvmClassifier::RvmClassifier(shared_ptr<Kernel> kernel) :
		VectorMachineClassifier(kernel), supportVectors(), coefficients() {}

RvmClassifier::~RvmClassifier() {}

bool RvmClassifier::classify(const Mat& featureVector) const {
	return classify(computeHyperplaneDistance(featureVector));
}

bool RvmClassifier::classify(double hyperplaneDistance) const {
	return hyperplaneDistance >= threshold;
}

double RvmClassifier::computeHyperplaneDistance(const Mat& featureVector) const {
	double distance = -bias;
	for (size_t i = 0; i < supportVectors.size(); ++i)
		distance += coefficients[i] * kernel->compute(featureVector, supportVectors[i]);
	return distance;
}

void RvmClassifier::setSvmParameters(vector<Mat> supportVectors, vector<float> coefficients, double bias) {
	this->supportVectors = supportVectors;
	this->coefficients = coefficients;
	this->bias = bias;
}

shared_ptr<RvmClassifier> RvmClassifier::loadMatlab(const string& classifierFilename, const string& thresholdsFilename)
{
	Logger logger = Loggers->getLogger("classification");
	logger.info("Loading RVM classifier from Matlab file: " + classifierFilename);
	
	MATFile *pmatfile;
	mxArray *pmxarray; // =mat
	double *matdata;
	pmatfile = matOpen(classifierFilename.c_str(), "r");
	if (pmatfile == NULL) {
		throw invalid_argument("RvmClassifier: Could not open the provided classifier filename: " + classifierFilename);
	}

	pmxarray = matGetVariable(pmatfile, "num_hk");
	if (pmxarray == 0) {
		throw runtime_error("RvmClassifier: There is a no num_hk in the classifier file.");
		// TODO (concerns the whole class): I think we leak memory here (all the MATFile and double pointers etc.)?
	}
	matdata = mxGetPr(pmxarray);
	int nfilter = (int)matdata[0];
	mxDestroyArray(pmxarray);
	logger.debug("Found " + lexical_cast<string>(nfilter) + " reduced set vectors (RSVs).");

	float nonlinThreshold;
	int nonLinType;
	float basisParam;
	int polyPower;
	float divisor;
	pmxarray = matGetVariable(pmatfile, "param_nonlin1_rvm");
	if (pmxarray != 0) {
		matdata = mxGetPr(pmxarray);
		nonlinThreshold = (float)matdata[0];
		nonLinType		= (int)matdata[1];
		basisParam		= (float)(matdata[2]/65025.0); // because the training images gray level values were divided by 255
		polyPower		= (int)matdata[3];
		divisor			= (float)matdata[4];
		mxDestroyArray(pmxarray);
	} else {
		pmxarray = matGetVariable(pmatfile, "param_nonlin1");
		if (pmxarray != 0) {
			matdata = mxGetPr(pmxarray);
			nonlinThreshold = (float)matdata[0];
			nonLinType		= (int)matdata[1];
			basisParam		= (float)(matdata[2]/65025.0); // because the training images gray level values were divided by 255
			polyPower		= (int)matdata[3];
			divisor			= (float)matdata[4];
			mxDestroyArray(pmxarray);
		}
	}
	shared_ptr<Kernel> kernel;
	if (nonLinType == 1) { // polynomial kernel
		kernel.reset(new PolynomialKernel(1 / divisor, basisParam / divisor, polyPower));
	} else if (nonLinType == 2) { // RBF kernel
		kernel.reset(new RbfKernel(basisParam));
	} else {
		throw runtime_error("RvmClassifier: Unsupported kernel type. Currently, only polynomial and RBF kernels are supported.");
		// TODO We should also throw/print the unsupported nonLinType value to the user
	}
	// TODO: logger.debug("Loaded kernel with params... ");

	shared_ptr<RvmClassifier> rvm = make_shared<RvmClassifier>(kernel);
	rvm->bias = nonlinThreshold;

	logger.debug("Reading the " + lexical_cast<string>(nfilter) + " non-linear filters support_hk* and weight_hk* ...");
	char str[100];
	sprintf(str, "support_hk%d", 1);
	pmxarray = matGetVariable(pmatfile, str);
	const mwSize *dim = mxGetDimensions(pmxarray);
	// TODO add a check if that variable exists?
	int filterSizeY = (int)dim[0]; // height
	int filterSizeX = (int)dim[1]; // width TODO check if this is right with eg 24x16

/*
	rvm->numLinFilters = nfilter;
	rvm->linFilters = new float* [rvm->numLinFilters];
	for (int i = 0; i < rvm->numLinFilters; ++i)
		rvm->linFilters[i] = new float[w*h];
		
	//hierarchicalThresholds = new float [numLinFilters];
	rvm->hkWeights = new float* [rvm->numLinFilters];
	for (int i = 0; i < rvm->numLinFilters; ++i)
		rvm->hkWeights[i] = new float[rvm->numLinFilters];

	if (pmxarray == 0) {
		throw runtime_error("RvmClassifier: Unable to find the matrix 'support_hk1' in the classifier file.");
	}
	if (mxGetNumberOfDimensions(pmxarray) != 2) {
		throw runtime_error("WvmClassifier: The matrix 'filter1' in the classifier file should have 2 dimensions.");
	}
	mxDestroyArray(pmxarray);

	for (int i = 0; i < wvm->numLinFilters; i++) {

		sprintf(str, "support_hk%d", i+1);
		pmxarray = matGetVariable(pmatfile, str);
		if (pmxarray == 0) {
			throw runtime_error("WvmClassifier: Unable to find the matrix 'support_hk" + lexical_cast<string>(i+1) + "' in the classifier file.");
		}
		if (mxGetNumberOfDimensions(pmxarray) != 2) {
			throw runtime_error("WvmClassifier: The matrix 'filter" + lexical_cast<string>(i+1) + "' in the classifier file should have 2 dimensions.");
		}

		matdata = mxGetPr(pmxarray);
				
		int k = 0;
		for (int x = 0; x < wvm->filter_size_x; ++x)
			for (int y = 0; y < wvm->filter_size_y; ++y)
				wvm->linFilters[i][y*wvm->filter_size_x+x] = 255.0f*(float)matdata[k++];	// because the training images grey level values were divided by 255;
		mxDestroyArray(pmxarray);

		sprintf(str, "weight_hk%d", i+1);
		pmxarray = matGetVariable(pmatfile, str);
		if (pmxarray != 0) {
			const mwSize *dim = mxGetDimensions(pmxarray);
			if ((dim[1] != i+1) && (dim[0] != i+1)) {
				throw runtime_error("WvmClassifier: The matrix " + lexical_cast<string>(str) + " in the classifier file should have a dimensions 1x" + lexical_cast<string>(i+1) + " or " + lexical_cast<string>(i+1) + "x1");
			}
			matdata = mxGetPr(pmxarray);
			for (int j = 0; j <= i; ++j) {
				wvm->hkWeights[i][j] = (float)matdata[j];
			}
			mxDestroyArray(pmxarray);
		}
	}	// end for over numHKs
	logger.debug("Vectors and weights successfully read.");




	wvm->lin_thresholds = new float [wvm->numLinFilters];
	for (int i = 0; i < wvm->numLinFilters; ++i) {			//wrvm_out=treshSVM+sum(beta*kernel)
		wvm->lin_thresholds[i] = (float)wvm->bias;
	}

	// number of filters per level (eg 14)
	pmxarray = matGetVariable(pmatfile, "num_hk_wvm");
	if (pmxarray != 0) {
		matdata = mxGetPr(pmxarray);
		assert(matdata != 0);	// TODO REMOVE
		wvm->numFiltersPerLevel = (int)matdata[0];
		mxDestroyArray(pmxarray);
	} else {
		throw runtime_error("WvmClassifier: Variable 'num_hk_wvm' not found in classifier file.");
	}
	// number of levels with filters (eg 20)
	pmxarray = matGetVariable(pmatfile, "num_lev_wvm");
	if (pmxarray != 0) {
		matdata = mxGetPr(pmxarray);
		assert(matdata != 0);
		wvm->numLevels = (int)matdata[0];
		mxDestroyArray(pmxarray);
	} else {
		throw runtime_error("WvmClassifier: Variable 'num_lev_wvm' not found in classifier file.");
	}

	if (matClose(pmatfile) != 0) {
		logger.warn("RvmClassifier: Could not close file " + classifierFilename);
		// TODO What is this? An error? Info? Throw an exception?
	}
	logger.info("RVM successfully read.");


	//MATFile *mxtFile = matOpen(args->threshold, "r");
	logger.info("Loading WVM thresholds from Matlab file: " + thresholdsFilename);
	pmatfile = matOpen(thresholdsFilename.c_str(), "r");
	if (pmatfile == 0) {
		throw runtime_error("WvmClassifier: Unable to open the thresholds file (wrong format?):" + thresholdsFilename);
	} else {
		pmxarray = matGetVariable(pmatfile, "hierar_thresh");
		if (pmxarray == 0) {
			throw runtime_error("WvmClassifier: Unable to find the matrix hierar_thresh in the thresholds file.");
		} else {
			double* matdata = mxGetPr(pmxarray);
			const mwSize *dim = mxGetDimensions(pmxarray);
			for (int o=0; o<(int)dim[1]; ++o) {
				//TPairIf p(o+1, (float)matdata[o]);
				//std::pair<int, float> p(o+1, (float)matdata[o]); // = std::make_pair<int, float>
				//this->hierarchicalThresholdsFromFile.push_back(p);
				wvm->hierarchicalThresholdsFromFile.push_back((float)matdata[o]);
			}
			mxDestroyArray(pmxarray);
		}
			
		matClose(pmatfile);
	}

	//int i;
	//for (i = 0; i < this->numLinFilters; ++i) {
	//	this->hierarchicalThresholds[i] = 0;
	//}
	//for (i = 0; i < this->hierarchicalThresholdsFromFile.size(); ++i) {
	//	if (this->hierarchicalThresholdsFromFile[i].first <= this->numLinFilters)
	//		this->hierarchicalThresholds[this->hierarchicalThresholdsFromFile[i].first-1] = this->hierarchicalThresholdsFromFile[i].second;
	//}
	////Diffwert fuer W-RSV's-Schwellen 
	//if (this->limitReliabilityFilter!=0.0)
	//	for (i = 0; i < this->numLinFilters; ++i) this->hierarchicalThresholds[i]+=this->limitReliabilityFilter;
	//

	if(wvm->hierarchicalThresholdsFromFile.size() != wvm->numLinFilters) {
		throw runtime_error("RvmClassifier: Something seems to be wrong, hierarchicalThresholdsFromFile.size() != numLinFilters; " + lexical_cast<string>(wvm->hierarchicalThresholdsFromFile.size()) + "!=" + lexical_cast<string>(wvm->numLinFilters));
	}
	wvm->setLimitReliabilityFilter(wvm->limitReliabilityFilter);	// This initializes the vector hierarchicalThresholds

	//for (i = 0; i < this->numLinFilters; ++i) printf("b%d=%g ",i+1,this->hierarchicalThresholds[i]);
	//printf("\n");
	logger.info("RVM thresholds successfully read.");

	wvm->filter_output = new float[wvm->numLinFilters];
	wvm->u_kernel_eval = new float[wvm->numLinFilters];

	wvm->setNumUsedFilters(wvm->numUsedFilters);	// Makes sure that we don't use more filters than the loaded WVM has, and if zero, set to numLinFilters.
*/
	return rvm;
}

shared_ptr<RvmClassifier> RvmClassifier::loadConfig(const ptree& subtree)
{
	path classifierFile = subtree.get<path>("classifierFile");
	if (classifierFile.extension() == ".mat") {
		shared_ptr<RvmClassifier> rvm = loadMatlab(classifierFile.string(), subtree.get<string>("thresholdsFile"));
		// Do some stuff, e.g.
		//int numFiltersToUse = subtree.get<int>("");
		//Number filters to use
		//wvm->numUsedFilters=280;	// Todo make dynamic (from script)
		return rvm;
	} else {
		throw logic_error("ProbabilisticRvmClassifier: Only loading of .mat RVMs is supported. If you want to load a non-cascaded RVM, use an SvmClassifier.");
	}
}

} /* namespace classification */
