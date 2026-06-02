#include "ort-session-utils.h"

#include <cstdio>

#ifdef __APPLE__
#include <coreml_provider_factory.h>
#endif

namespace obm {

std::vector<const char *> OrtModel::inputNamePtrs() const
{
	std::vector<const char *> v;
	v.reserve(inputNames.size());
	for (const auto &n : inputNames)
		v.push_back(n.c_str());
	return v;
}

std::vector<const char *> OrtModel::outputNamePtrs() const
{
	std::vector<const char *> v;
	v.reserve(outputNames.size());
	for (const auto &n : outputNames)
		v.push_back(n.c_str());
	return v;
}

static void populateNamesAndShapes(OrtModel &m)
{
	Ort::AllocatorWithDefaultOptions alloc;
	const size_t nIn = m.session->GetInputCount();
	for (size_t i = 0; i < nIn; ++i) {
		auto name = m.session->GetInputNameAllocated(i, alloc);
		m.inputNames.emplace_back(name.get());
		m.inputShapes.push_back(
			m.session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape());
	}
	const size_t nOut = m.session->GetOutputCount();
	for (size_t i = 0; i < nOut; ++i) {
		auto name = m.session->GetOutputNameAllocated(i, alloc);
		m.outputNames.emplace_back(name.get());
		m.outputShapes.push_back(
			m.session->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape());
	}
}

std::unique_ptr<OrtModel> createOrtModel(Ort::Env &env, const std::string &modelPath,
					 Backend backend, int numThreads)
{
	auto buildOptions = [&](bool withCoreML) {
		Ort::SessionOptions opts;
		opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		opts.SetIntraOpNumThreads(numThreads);
#ifdef __APPLE__
		if (withCoreML) {
			// COREML_FLAG_ENABLE_ON_SUBGRAPH lets unsupported ops fall
			// back to CPU within the graph instead of failing outright.
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(
				opts, COREML_FLAG_ENABLE_ON_SUBGRAPH));
		}
#else
		(void)withCoreML;
#endif
		return opts;
	};

	const bool wantCoreML = (backend == Backend::CoreML);

	for (int attempt = 0; attempt < 2; ++attempt) {
		const bool useCoreML = wantCoreML && attempt == 0;
		try {
			auto m = std::make_unique<OrtModel>();
			auto opts = buildOptions(useCoreML);
			m->session = std::make_unique<Ort::Session>(env, modelPath.c_str(), opts);
			populateNamesAndShapes(*m);
			return m;
		} catch (const Ort::Exception &e) {
			if (useCoreML) {
				fprintf(stderr,
					"[obs-body-mask] CoreML EP unavailable (%s); "
					"retrying on CPU\n",
					e.what());
				continue; // retry CPU-only
			}
			fprintf(stderr, "[obs-body-mask] failed to load model '%s': %s\n",
				modelPath.c_str(), e.what());
			return nullptr;
		}
	}
	return nullptr;
}

} // namespace obm
