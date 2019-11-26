#include "modes.h"
#include "bind_options.h"

#include <catboost/private/libs/algo/helpers.h>
#include <catboost/libs/data/load_data.h>
#include <catboost/libs/helpers/exception.h>
#include <catboost/private/libs/options/catboost_options.h>
#include <catboost/private/libs/options/feature_eval_options.h>
#include <catboost/private/libs/options/plain_options_helper.h>
#include <catboost/libs/train_lib/eval_feature.h>
#include <catboost/libs/data/feature_names_converter.h>

#include <library/json/json_reader.h>

#include <util/generic/ptr.h>


using namespace NCB;


int mode_eval_feature(int argc, const char* argv[]) {
    ConfigureMalloc();

    NJson::TJsonValue catBoostFlatJsonOptions;
    TString paramsFile;
    NCatboostOptions::TPoolLoadParams poolLoadParams;
    NJson::TJsonValue featureEvalJsonOptions;
    ParseFeatureEvalCommandLine(
        argc,
        argv,
        &catBoostFlatJsonOptions,
        &featureEvalJsonOptions,
        &paramsFile,
        &poolLoadParams
    );

    CB_ENSURE(poolLoadParams.TestSetPaths.empty(), "Test files are not supported in feature evaluation mode");

    NJson::TJsonValue catBoostJsonOptions;
    NJson::TJsonValue outputOptionsJson;
    InitOptions(paramsFile, &catBoostJsonOptions, &outputOptionsJson);

    ConvertIgnoredFeaturesFromStringToIndices(poolLoadParams, &catBoostFlatJsonOptions);
    NCatboostOptions::PlainJsonToOptions(catBoostFlatJsonOptions, &catBoostJsonOptions, &outputOptionsJson);

    CopyIgnoredFeaturesToPoolParams(catBoostJsonOptions, &poolLoadParams);

    NCatboostOptions::TOutputFilesOptions outputOptions;
    outputOptions.Load(outputOptionsJson);

    const auto taskType = NCatboostOptions::GetTaskType(catBoostJsonOptions);
    NCatboostOptions::TCatBoostOptions catBoostOptions(taskType);
    catBoostOptions.Load(catBoostJsonOptions);
    NCatboostOptions::TFeatureEvalOptions featureEvalOptions;
    featureEvalOptions.Load(featureEvalJsonOptions);

    const auto& featuresToEvaluate = featureEvalOptions.FeaturesToEvaluate.Get();
    for (const auto& featureSet : featuresToEvaluate) {
        for (ui32 feature : featureSet) {
            CB_ENSURE(Count(poolLoadParams.IgnoredFeatures, feature) == 0, "Tested feature " << feature << " should not be ignored");
        }
        CB_ENSURE(Count(featuresToEvaluate, featureSet) == 1, "All tested feature sets must be different");
    }

    NPar::LocalExecutor().RunAdditionalThreads(catBoostOptions.SystemOptions->NumThreads - 1);

    TVector<TString> classNames = catBoostOptions.DataProcessingOptions->ClassNames;
    const auto objectsOrder = EObjectsOrder::Undefined;
    auto pools = NCB::ReadTrainDatasets(
        poolLoadParams,
        objectsOrder,
        /*readTestData*/false,
        TDatasetSubset::MakeColumns(),
        &classNames,
        &NPar::LocalExecutor(),
        /*profile*/nullptr
    );

    const auto featureEvalSummary = EvaluateFeatures(
        catBoostFlatJsonOptions,
        featureEvalOptions,
        /*objectiveDescriptor*/Nothing(),
        /*evalMetricDescriptor*/Nothing(),
        poolLoadParams.CvParams,
        pools.Learn
    );

    if (featureEvalOptions.EvalFeatureFileName->length() > 0) {
        TFileOutput featureEvalFile(featureEvalOptions.EvalFeatureFileName.Get());
        featureEvalFile << ToString(featureEvalSummary);
    } else {
        CATBOOST_DEBUG_LOG << ToString(featureEvalSummary);
    }

    return 0;
}

